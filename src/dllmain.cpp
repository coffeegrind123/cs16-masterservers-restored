#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "matchmaking_impl.h"
#include "master_query.h"
#include "vdf_parser.h"
#include "utils.h"

static HMODULE g_hSelf = NULL;
static HMODULE g_hRealSteamApi = NULL;
static bool g_bRealSteamResolved = false;
static bool g_bWsaInit = false;
static char g_logPath[MAX_PATH] = {0};
static bool g_logTruncated = false;
static CRITICAL_SECTION g_logCS;
static bool g_logCSInit = false;

static char g_selfDir[512] = {0};
static char g_pendingSetmaster[256] = {0};

static void StripQuotes(char *s)
{
	int len = (int)strlen(s);
	if (len >= 2 && s[0] == '"' && s[len-1] == '"')
	{
		memmove(s, s + 1, len - 2);
		s[len - 2] = '\0';
	}
}

void RealMasterLog(const char *fmt, ...)
{
	if (!g_logCSInit) return;
	EnterCriticalSection(&g_logCS);
	if (!g_logPath[0])
	{
		if (g_selfDir[0])
			snprintf(g_logPath, sizeof(g_logPath), "%s\\mastersrv_debug.log", g_selfDir);
		else
			strcpy(g_logPath, "mastersrv_debug.log");
	}
	const char *mode = g_logTruncated ? "a" : "w";
	FILE *f = fopen(g_logPath, mode);
	if (!f) { LeaveCriticalSection(&g_logCS); return; }
	g_logTruncated = true;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fprintf(f, "\n");
	fclose(f);
	LeaveCriticalSection(&g_logCS);
}

typedef int (__cdecl *CmdArgc_t)(void);
typedef const char *(__cdecl *CmdArgv_t)(int n);
typedef void (__cdecl *ConPrintf_t)(const char *fmt, ...);

static CmdArgc_t g_pCmdArgc = NULL;
static CmdArgv_t g_pCmdArgv = NULL;
static ConPrintf_t g_pConPrintf = NULL;
static bool g_engineHooked = false;

extern bool g_MasterListLoaded;

static uint8_t *FindPattern(uint8_t *start, size_t len, const uint8_t *pattern, size_t patLen)
{
	for (size_t i = 0; i + patLen <= len; i++)
	{
		if (memcmp(start + i, pattern, patLen) == 0)
			return start + i;
	}
	return NULL;
}

static void *ResolveCall(uint8_t *callInstr)
{
	if (*callInstr != 0xE8) return NULL;
	int32_t rel = *(int32_t *)(callInstr + 1);
	return (void *)(callInstr + 5 + rel);
}

static void Cmd_SetMaster(void)
{
	if (!g_pCmdArgc || !g_pCmdArgv || !g_pConPrintf) return;

	int argc = g_pCmdArgc();
	if (argc < 2)
	{
		g_pConPrintf("Usage: setmaster <ip[:port]>\n");
		g_pConPrintf("  Sets the master server for Internet server browser.\n");
		g_pConPrintf("  Default port: 27010\n");
		return;
	}

	char full_addr[256];
	full_addr[0] = '\0';
	for (int i = 1; i < argc; i++)
	{
		const char *arg = g_pCmdArgv(i);
		if (strcmp(arg, ":") == 0) continue;
		if (full_addr[0] && !strchr(full_addr, ':'))
		{
			strncat(full_addr, ":", sizeof(full_addr) - strlen(full_addr) - 1);
		}
		strncat(full_addr, arg, sizeof(full_addr) - strlen(full_addr) - 1);
	}
	StripQuotes(full_addr);
	if (!strchr(full_addr, ':'))
		strncat(full_addr, ":27010", sizeof(full_addr) - strlen(full_addr) - 1);

	g_pConPrintf("Verifying master server %s...\n", full_addr);

	if (!master_validate_server(full_addr))
	{
		g_pConPrintf("Error: Master server %s is not responding.\n", full_addr);
		return;
	}
	g_pConPrintf("Master server OK.\n");

	char vdf_path[512];
	snprintf(vdf_path, sizeof(vdf_path), "%s\\platform\\config\\MasterServers.vdf", g_selfDir);
	if (!vdf_write_master_server(vdf_path, full_addr))
	{
		g_pConPrintf("Error: Could not write config file.\n");
		return;
	}

	g_MasterListLoaded = false;
	g_pConPrintf("Master server set to %s\n", full_addr);
}

static void InitEngineHook()
{
	if (g_engineHooked) return;

	HMODULE hHw = GetModuleHandleA("hw.dll");
	if (!hHw) return;

	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)hHw;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)hHw + dos->e_lfanew);
	uint8_t *base = (uint8_t *)hHw;
	size_t imageSize = nt->OptionalHeader.SizeOfImage;

	const char *needle = "Cmd_AddCommand: %s already defined as a var";
	size_t needleLen = strlen(needle);
	uint8_t *strAddr = FindPattern(base, imageSize, (const uint8_t *)needle, needleLen);
	if (!strAddr)
	{
		RealMasterLog("Engine hook: could not find Cmd_AddCommand string");
		return;
	}

	uint8_t pushBytes[5];
	pushBytes[0] = 0x68;
	memcpy(pushBytes + 1, &strAddr, 4);
	uint8_t *pushRef = FindPattern(base, imageSize, pushBytes, 5);
	if (!pushRef)
	{
		RealMasterLog("Engine hook: could not find xref to Cmd_AddCommand string");
		return;
	}

	uint8_t *fnStart = pushRef;
	while (fnStart > base && !(fnStart[0] == 0x55 && fnStart[1] == 0x8B && fnStart[2] == 0xEC))
	{
		if (fnStart[0] == 0xCC && fnStart[1] != 0xCC)
		{
			fnStart = fnStart + 1;
			break;
		}
		fnStart--;
	}
	RealMasterLog("Engine hook: Cmd_AddCommand at %p", fnStart);

	const char *smNeedle = "setmaster";
	size_t smLen = strlen(smNeedle) + 1;
	uint8_t *smStr = FindPattern(base, imageSize, (const uint8_t *)smNeedle, smLen);
	if (!smStr)
	{
		RealMasterLog("Engine hook: could not find 'setmaster' string");
		return;
	}

	uint8_t smPush[5];
	smPush[0] = 0x68;
	memcpy(smPush + 1, &smStr, 4);
	uint8_t *smPushRef = FindPattern(base, imageSize, smPush, 5);
	if (!smPushRef)
	{
		RealMasterLog("Engine hook: could not find xref to 'setmaster'");
		return;
	}

	uint8_t *handlerPush = smPushRef - 5;
	if (handlerPush[0] != 0x68)
	{
		RealMasterLog("Engine hook: unexpected instruction before setmaster push");
		return;
	}
	void *origHandler = *(void **)(handlerPush + 1);
	RealMasterLog("Engine hook: original setmaster handler at %p", origHandler);

	uint8_t *handler = (uint8_t *)origHandler;
	uint8_t *scan = handler;
	uint8_t *scanEnd = handler + 64;

	while (scan < scanEnd)
	{
		if (scan[0] == 0xE8)
		{
			g_pCmdArgc = (CmdArgc_t)ResolveCall(scan);
			RealMasterLog("Engine hook: Cmd_Argc at %p", g_pCmdArgc);
			break;
		}
		scan++;
	}
	while (scan < scanEnd)
	{
		if (scan[0] == 0x68 && scan[5] == 0xE8)
		{
			g_pConPrintf = (ConPrintf_t)ResolveCall(scan + 5);
			RealMasterLog("Engine hook: Con_Printf at %p", g_pConPrintf);
			break;
		}
		scan++;
	}

	scan = handler + 5;
	while (scan < scanEnd)
	{
		if (scan[0] == 0x6A && scan[1] == 0x01 && scan[2] == 0xE8)
		{
			g_pCmdArgv = (CmdArgv_t)ResolveCall(scan + 2);
			RealMasterLog("Engine hook: Cmd_Argv at %p", g_pCmdArgv);
			break;
		}
		scan++;
	}

	if (!g_pCmdArgc || !g_pCmdArgv || !g_pConPrintf)
	{
		RealMasterLog("Engine hook: failed to resolve all functions (argc=%p argv=%p printf=%p)",
			g_pCmdArgc, g_pCmdArgv, g_pConPrintf);
		return;
	}

	struct cmd_node_t { cmd_node_t *next; const char *name; void (*handler)(); int flags; };

	void **ppCmdHead = NULL;
	scan = fnStart;
	scanEnd = fnStart + 128;
	while (scan < scanEnd)
	{
		if (scan[0] == 0xA1)
		{
			void **candidate = *(void ***)(scan + 1);
			if (!IsBadReadPtr(candidate, 4) && !IsBadReadPtr(*candidate, 16))
			{
				cmd_node_t *test = (cmd_node_t *)*candidate;
				if (!IsBadReadPtr(test, 16) && !IsBadReadPtr(test->name, 4))
				{
					ppCmdHead = candidate;
					RealMasterLog("Engine hook: command list head at %p", ppCmdHead);
					break;
				}
			}
		}
		if (scan[0] == 0x8B && (scan[1] == 0x35 || scan[1] == 0x0D || scan[1] == 0x15))
		{
			void **candidate = *(void ***)(scan + 2);
			if (!IsBadReadPtr(candidate, 4) && !IsBadReadPtr(*candidate, 16))
			{
				cmd_node_t *test = (cmd_node_t *)*candidate;
				if (!IsBadReadPtr(test, 16) && !IsBadReadPtr(test->name, 4))
				{
					ppCmdHead = candidate;
					RealMasterLog("Engine hook: command list head at %p (via MOV reg)", ppCmdHead);
					break;
				}
			}
		}
		scan++;
	}

	if (!ppCmdHead)
	{
		RealMasterLog("Engine hook: could not find command list head pointer");
		return;
	}

	for (cmd_node_t *node = (cmd_node_t *)*ppCmdHead; node; node = node->next)
	{
		if (node->name && stricmp(node->name, "setmaster") == 0)
		{
			node->handler = Cmd_SetMaster;
			RealMasterLog("Engine hook: replaced setmaster handler at node %p", node);
			g_engineHooked = true;
			return;
		}
	}

	RealMasterLog("Engine hook: setmaster command not found in command list");
}

typedef void *(*GenericSteamFunc_t)();
typedef ISteamMatchmakingServers *(*SteamMatchmakingServers_t)();
static SteamMatchmakingServers_t g_pfnRealSteamMatchmakingServers = NULL;

static void EnsureWsa()
{
	if (!g_bWsaInit)
	{
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
		g_bWsaInit = true;
	}
}

static void EnsureRealSteamApi()
{
	if (!g_hRealSteamApi)
	{
		g_hRealSteamApi = GetModuleHandleA("steam_api.dll");
		if (!g_hRealSteamApi)
		{
			char path[512];
			snprintf(path, sizeof(path), "%s\\steam_api.dll", g_selfDir);
			g_hRealSteamApi = LoadLibraryA(path);
		}
		if (!g_hRealSteamApi)
			g_hRealSteamApi = LoadLibraryA("steam_api.dll");

		if (g_hRealSteamApi)
		{
			g_pfnRealSteamMatchmakingServers = (SteamMatchmakingServers_t)
				GetProcAddress(g_hRealSteamApi, "SteamMatchmakingServers");
			if (!g_pfnRealSteamMatchmakingServers)
				g_pfnRealSteamMatchmakingServers = (SteamMatchmakingServers_t)
					GetProcAddress(g_hRealSteamApi, "SteamAPI_SteamMatchmakingServers_v002");
			RealMasterLog("Real steam_api.dll loaded at %p, SteamMatchmakingServers=%p",
				g_hRealSteamApi, g_pfnRealSteamMatchmakingServers);
		}
		else
		{
			RealMasterLog("WARNING: Could not find real steam_api.dll");
		}
	}

	if (g_pfnRealSteamMatchmakingServers && !g_bRealSteamResolved)
	{
		ISteamMatchmakingServers *pReal = g_pfnRealSteamMatchmakingServers();
		if (pReal)
		{
			SetRealSteamMatchmaking(pReal);
			g_bRealSteamResolved = true;
			RealMasterLog("Real ISteamMatchmakingServers resolved at %p", pReal);
		}
	}
}

#define LOAD_REAL_FUNC(name) \
	EnsureRealSteamApi(); \
	static GenericSteamFunc_t pfn_##name = NULL; \
	if (!pfn_##name && g_hRealSteamApi) \
		pfn_##name = (GenericSteamFunc_t)GetProcAddress(g_hRealSteamApi, #name);

extern "C" __declspec(dllexport) ISteamMatchmakingServers * __cdecl SteamMatchmakingServers()
{
	EnsureWsa();
	EnsureRealSteamApi();
	return GetRealMasterMatchmaking();
}

extern "C" __declspec(dllexport) void * __cdecl SteamAPI_RunCallbacks()
{
	LOAD_REAL_FUNC(SteamAPI_RunCallbacks)
	if (pfn_SteamAPI_RunCallbacks) pfn_SteamAPI_RunCallbacks();

	InitEngineHook();

	static bool g_pendingSetmasterDone = false;
	if (g_engineHooked && !g_pendingSetmasterDone && g_pendingSetmaster[0])
	{
		g_pendingSetmasterDone = true;
		RealMasterLog("Executing deferred +setmaster %s", g_pendingSetmaster);

		char full_addr[256];
		if (!strchr(g_pendingSetmaster, ':'))
			snprintf(full_addr, sizeof(full_addr), "%s:27010", g_pendingSetmaster);
		else
		{
			strncpy(full_addr, g_pendingSetmaster, sizeof(full_addr) - 1);
			full_addr[sizeof(full_addr) - 1] = '\0';
		}

		if (g_pConPrintf)
			g_pConPrintf("Verifying master server %s...\n", full_addr);

		if (master_validate_server(full_addr))
		{
			char vdf_path[512];
			snprintf(vdf_path, sizeof(vdf_path), "%s\\platform\\config\\MasterServers.vdf", g_selfDir);
			vdf_write_master_server(vdf_path, full_addr);
			g_MasterListLoaded = false;
			if (g_pConPrintf)
				g_pConPrintf("Master server set to %s\n", full_addr);
		}
		else
		{
			if (g_pConPrintf)
				g_pConPrintf("Error: Master server %s is not responding.\n", full_addr);
		}
	}

	CRealMasterMatchmaking *p = (CRealMasterMatchmaking *)GetRealMasterMatchmaking();
	p->DispatchCallbacks();

	return NULL;
}

#define FORWARD_FUNC(name) \
	extern "C" __declspec(dllexport) void * __cdecl name() \
	{ \
		LOAD_REAL_FUNC(name) \
		if (pfn_##name) return pfn_##name(); \
		return NULL; \
	}

extern "C" __declspec(dllexport) void * __cdecl SteamAPI_Init()
{
	LOAD_REAL_FUNC(SteamAPI_Init)
	void *ret = NULL;
	if (pfn_SteamAPI_Init) ret = pfn_SteamAPI_Init();
	if (g_pendingSetmaster[0])
		InitEngineHook();
	return ret;
}
FORWARD_FUNC(SteamAPI_Shutdown)
FORWARD_FUNC(SteamFriends)
FORWARD_FUNC(SteamApps)
FORWARD_FUNC(SteamMatchmaking)

extern "C" __declspec(dllexport) void __cdecl SteamAPI_RegisterCallback(void *pCallback, int iCallback)
{
	EnsureRealSteamApi();
	if (g_pendingSetmaster[0] && !g_engineHooked)
		InitEngineHook();
	typedef void (__cdecl *Func_t)(void *, int);
	static Func_t pfn = NULL;
	if (!pfn && g_hRealSteamApi)
		pfn = (Func_t)GetProcAddress(g_hRealSteamApi, "SteamAPI_RegisterCallback");
	if (pfn) pfn(pCallback, iCallback);
}

extern "C" __declspec(dllexport) void __cdecl SteamAPI_UnregisterCallback(void *pCallback)
{
	EnsureRealSteamApi();
	typedef void (__cdecl *Func_t)(void *);
	static Func_t pfn = NULL;
	if (!pfn && g_hRealSteamApi)
		pfn = (Func_t)GetProcAddress(g_hRealSteamApi, "SteamAPI_UnregisterCallback");
	if (pfn) pfn(pCallback);
}

FORWARD_FUNC(SteamAPI_GetHSteamUser)

extern "C" __declspec(dllexport) void * __cdecl SteamInternal_ContextInit(void *pCtx)
{
	EnsureRealSteamApi();
	typedef void *(__cdecl *Func_t)(void *);
	static Func_t pfn = NULL;
	if (!pfn && g_hRealSteamApi)
		pfn = (Func_t)GetProcAddress(g_hRealSteamApi, "SteamInternal_ContextInit");
	if (pfn) return pfn(pCtx);
	return NULL;
}

extern "C" __declspec(dllexport) void * __cdecl SteamInternal_FindOrCreateUserInterface(int hSteamUser, const char *pszVersion)
{
	EnsureWsa();
	EnsureRealSteamApi();

	if (pszVersion && strcmp(pszVersion, "SteamMatchMakingServers002") == 0)
	{
		RealMasterLog("SteamInternal_FindOrCreateUserInterface('%s') -> our impl", pszVersion);
		return GetRealMasterMatchmaking();
	}

	typedef void *(__cdecl *Func_t)(int, const char *);
	static Func_t pfn = NULL;
	if (!pfn && g_hRealSteamApi)
		pfn = (Func_t)GetProcAddress(g_hRealSteamApi, "SteamInternal_FindOrCreateUserInterface");
	if (pfn) return pfn(hSteamUser, pszVersion);
	return NULL;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		g_hSelf = hinstDLL;
		DisableThreadLibraryCalls(hinstDLL);
		InitializeCriticalSection(&g_logCS);
		g_logCSInit = true;

		GetModuleFileNameA(hinstDLL, g_selfDir, sizeof(g_selfDir));
		char *slash = strrchr(g_selfDir, '\\');
		if (!slash) slash = strrchr(g_selfDir, '/');
		if (slash) *slash = '\0';

		RealMasterLog("=== realmastr.dll loaded from %s ===", g_selfDir);

		const char *cmdLine = GetCommandLineA();
		const char *sm = strstr(cmdLine, "+setmaster");
		if (sm)
		{
			sm += 10;
			while (*sm == ' ') sm++;
			int j = 0;
			while (*sm && *sm != ' ' && *sm != '+' && *sm != '-' && j < 255)
				g_pendingSetmaster[j++] = *sm++;
			g_pendingSetmaster[j] = '\0';
			StripQuotes(g_pendingSetmaster);
			if (g_pendingSetmaster[0])
				RealMasterLog("Pending +setmaster: %s", g_pendingSetmaster);
		}

		if (!g_pendingSetmaster[0])
		{
			char cfgPath[512];
			const char *cfgNames[] = { "cstrike\\config.cfg", "cstrike\\autoexec.cfg",
				"cstrike\\userconfig.cfg", "valve\\config.cfg", "valve\\autoexec.cfg", NULL };
			for (int c = 0; cfgNames[c] && !g_pendingSetmaster[0]; c++)
			{
				snprintf(cfgPath, sizeof(cfgPath), "%s\\%s", g_selfDir, cfgNames[c]);
				FILE *cfg = fopen(cfgPath, "r");
				if (!cfg) { RealMasterLog("Config scan: %s not found", cfgPath); continue; }
				char line[512];
				while (fgets(line, sizeof(line), cfg))
				{
					char *p = line;
					while (*p == ' ' || *p == '\t') p++;
					if (strnicmp(p, "setmaster", 9) == 0 && (p[9] == ' ' || p[9] == '\t'))
					{
						p += 9;
						while (*p == ' ' || *p == '\t') p++;
						int j = 0;
						while (*p && *p != '\r' && *p != '\n' && *p != ';' && j < 255)
							g_pendingSetmaster[j++] = *p++;
						while (j > 0 && g_pendingSetmaster[j-1] == ' ') j--;
						g_pendingSetmaster[j] = '\0';
						StripQuotes(g_pendingSetmaster);
						if (g_pendingSetmaster[0])
							RealMasterLog("Pending setmaster from %s: %s", cfgNames[c], g_pendingSetmaster);
					}
				}
				fclose(cfg);
			}
		}

		if (g_pendingSetmaster[0])
		{
			HMODULE hHw = GetModuleHandleA("hw.dll");
			RealMasterLog("Early patch: hw.dll=%p, pending=%s", hHw, g_pendingSetmaster);
			if (hHw)
			{
				IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)hHw;
				IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)hHw + dos->e_lfanew);
				uint8_t *base = (uint8_t *)hHw;
				size_t imageSize = nt->OptionalHeader.SizeOfImage;

				const char *smNeedle = "setmaster";
				uint8_t *smStr = FindPattern(base, imageSize, (const uint8_t *)smNeedle, 10);
				if (smStr)
				{
					uint8_t smPush[5] = { 0x68 };
					memcpy(smPush + 1, &smStr, 4);
					uint8_t *smPushRef = FindPattern(base, imageSize, smPush, 5);
					if (smPushRef && smPushRef[-5] == 0x68)
					{
						uint8_t *handler = *(uint8_t **)(smPushRef - 4);
						if (handler >= base && handler < base + imageSize)
						{
							DWORD oldProt;
							VirtualProtect(handler, 1, PAGE_EXECUTE_READWRITE, &oldProt);
							handler[0] = 0xC3;
							VirtualProtect(handler, 1, oldProt, &oldProt);
							RealMasterLog("Patched original setmaster handler at %p to RET", handler);
						}
					}
				}
			}
		}
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		if (g_logCSInit) { DeleteCriticalSection(&g_logCS); g_logCSInit = false; }
	}
	return TRUE;
}
