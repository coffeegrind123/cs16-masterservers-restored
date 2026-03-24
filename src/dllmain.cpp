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
#include "reunion.h"

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
static void *g_pServerState = NULL;
static SOCKET *g_pServerSocket = NULL;
static char *g_pMapName = NULL;
static int *g_pMaxPlayers = NULL;
static uint8_t *g_pClientArray = NULL;
static int g_clientStride = 0;
static char *g_pGameDir = NULL;
static DWORD g_lastHeartbeat = 0;
static bool g_heartbeatActive = false;
static char g_heartbeatMaster[256] = {0};

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

struct cvar_t { char *name; char *string; int flags; float value; cvar_t *next; };
typedef cvar_t *(__cdecl *CvarFindVar_t)(const char *name);

static CmdArgc_t g_pCmdArgc = NULL;
static CmdArgv_t g_pCmdArgv = NULL;
static ConPrintf_t g_pConPrintf = NULL;
static CvarFindVar_t g_pCvarFindVar = NULL;
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

static const char *GetCvarString(const char *name)
{
	if (!g_pCvarFindVar) return "";
	cvar_t *cv = g_pCvarFindVar(name);
	return (cv && cv->string) ? cv->string : "";
}

static float GetCvarFloat(const char *name)
{
	if (!g_pCvarFindVar) return 0.0f;
	cvar_t *cv = g_pCvarFindVar(name);
	return cv ? cv->value : 0.0f;
}

static void GatherHeartbeatInfo(heartbeat_info_t *info)
{
	memset(info, 0, sizeof(*info));

	strncpy(info->hostname, GetCvarString("hostname"), sizeof(info->hostname) - 1);

	if (g_pMapName && !IsBadReadPtr(g_pMapName, 4) && g_pMapName[0])
		strncpy(info->map, g_pMapName, sizeof(info->map) - 1);

	if (g_pGameDir && !IsBadReadPtr(g_pGameDir, 4) && g_pGameDir[0])
	{
		strncpy(info->gamedir, g_pGameDir, sizeof(info->gamedir) - 1);
		char *lastSlash = strrchr(info->gamedir, '\\');
		if (lastSlash && lastSlash[1])
			memmove(info->gamedir, lastSlash + 1, strlen(lastSlash + 1) + 1);
	}
	else
		strncpy(info->gamedir, "cstrike", sizeof(info->gamedir) - 1);

	if (g_pMaxPlayers && !IsBadReadPtr(g_pMaxPlayers, 4) && *g_pMaxPlayers > 0)
		info->max_players = *g_pMaxPlayers;
	else
		info->max_players = 16;

	info->password = (GetCvarString("sv_password")[0] && strcmp(GetCvarString("sv_password"), "none") != 0) ? 1 : 0;
	info->lan = (int)GetCvarFloat("sv_lan");
	info->secure = strstr(GetCommandLineA(), "-insecure") ? 0 : 1;

	const char *sv_ver = GetCvarString("sv_version");
	if (sv_ver[0])
	{
		strncpy(info->version, sv_ver, sizeof(info->version) - 1);
		char *comma = strchr(info->version, ',');
		if (comma) *comma = '\0';
	}
	else
		strncpy(info->version, "1.1.2.7/Stdio", sizeof(info->version) - 1);

	const char *sv_ver_full = GetCvarString("sv_version");
	if (sv_ver_full[0])
	{
		const char *c1 = strchr(sv_ver_full, ',');
		if (c1) info->protocol = atoi(c1 + 1);
	}
	if (info->protocol <= 0) info->protocol = 48;

	info->is_dedicated = (GetModuleHandleA("swds.dll") != NULL) ? 1 : 0;

	if (g_pClientArray && g_pMaxPlayers && !IsBadReadPtr(g_pMaxPlayers, 4) && g_clientStride > 0)
	{
		int max = *g_pMaxPlayers;
		if (max > 64) max = 64;
		for (int i = 0; i < max; i++)
		{
			uint8_t *client = g_pClientArray + (i * g_clientStride);
			if (IsBadReadPtr(client, g_clientStride)) break;
			if (*(int *)client == 0) continue;
			info->players++;
			if (!IsBadReadPtr(client + 0x222, 1) && (*(uint8_t *)(client + 0x222) & 0x20))
				info->bots++;
		}
	}

	RealMasterLog("Heartbeat info: hostname='%s' map='%s' gamedir='%s' players=%d/%d bots=%d password=%d lan=%d secure=%d version='%s' protocol=%d type=%c",
		info->hostname, info->map, info->gamedir, info->players, info->max_players,
		info->bots, info->password, info->lan, info->secure, info->version,
		info->protocol, info->is_dedicated ? 'd' : 'l');
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

	const char *firstArg = g_pCmdArgv(1);
	if (stricmp(firstArg, "enable") == 0 || stricmp(firstArg, "disable") == 0)
		return;

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

	strncpy(g_heartbeatMaster, full_addr, sizeof(g_heartbeatMaster) - 1);
	if (g_pServerState && *(int *)g_pServerState != 0)
	{
		g_heartbeatActive = true;
		g_lastHeartbeat = 0;
		heartbeat_info_t hbinfo;
		GatherHeartbeatInfo(&hbinfo);
		SOCKET svSock = (g_pServerSocket && !IsBadReadPtr(g_pServerSocket, 4)) ? *g_pServerSocket : INVALID_SOCKET;
		if (master_send_heartbeat(full_addr, &hbinfo, svSock))
		{
			g_pConPrintf("Server registered with master %s\n", full_addr);
			g_lastHeartbeat = GetTickCount();
		}
		else
		{
			g_pConPrintf("Warning: Heartbeat to %s failed.\n", full_addr);
		}
	}
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
	for (int i = 0; i < 20; i++)
	{
		if (handler[i] == 0x8B && handler[i + 1] == 0x0D)
		{
			g_pServerState = *(void **)(handler + i + 2);
			RealMasterLog("Engine hook: server state at %p", g_pServerState);
			break;
		}
		if (handler[i] == 0x83 && handler[i + 1] == 0x3D)
		{
			g_pServerState = *(void **)(handler + i + 2);
			RealMasterLog("Engine hook: server state at %p (via CMP)", g_pServerState);
			break;
		}
	}
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

	const char *cvarNeedle = "Cvar_RegisterVariable: %s is a command";
	uint8_t *cvarStr = FindPattern(base, imageSize, (const uint8_t *)cvarNeedle, strlen(cvarNeedle));
	if (cvarStr)
	{
		uint8_t cvarPush[5] = { 0x68 };
		memcpy(cvarPush + 1, &cvarStr, 4);
		uint8_t *cvarRef = FindPattern(base, imageSize, cvarPush, 5);
		if (cvarRef)
		{
			uint8_t *cvarFn = cvarRef;
			while (cvarFn > base && !(cvarFn[0] == 0x55 && cvarFn[1] == 0x8B && cvarFn[2] == 0xEC))
			{
				if (cvarFn[0] == 0xCC && cvarFn[1] != 0xCC) { cvarFn++; break; }
				cvarFn--;
			}
			scan = cvarFn;
			scanEnd = cvarFn + 32;
			while (scan < scanEnd)
			{
				if (scan[0] == 0xE8)
				{
					g_pCvarFindVar = (CvarFindVar_t)ResolveCall(scan);
					RealMasterLog("Engine hook: Cvar_FindVar at %p", g_pCvarFindVar);
					break;
				}
				scan++;
			}
		}
	}

	const char *mapFmt = "map     :  %s at";
	uint8_t *mapFmtAddr = FindPattern(base, imageSize, (const uint8_t *)mapFmt, strlen(mapFmt));
	if (mapFmtAddr)
	{
		uint8_t mapPush[5] = { 0x68 };
		memcpy(mapPush + 1, &mapFmtAddr, 4);
		uint8_t *mapRef = FindPattern(base, imageSize, mapPush, 5);
		if (mapRef)
		{
			for (uint8_t *p = mapRef - 20; p < mapRef; p++)
			{
				if (p[0] == 0x68)
				{
					char *candidate = *(char **)(p + 1);
					if (!IsBadReadPtr(candidate, 4))
					{
						g_pMapName = candidate;
						RealMasterLog("Engine hook: map name buffer at %p", g_pMapName);
						break;
					}
				}
			}
		}
	}

	const char *playersFmt = "players :  %i active (%i max)";
	uint8_t *playersStr = FindPattern(base, imageSize, (const uint8_t *)playersFmt, strlen(playersFmt));
	if (playersStr)
	{
		uint8_t playersPush[5] = { 0x68 };
		memcpy(playersPush + 1, &playersStr, 4);
		uint8_t *playersRef = FindPattern(base, imageSize, playersPush, 5);
		if (playersRef)
		{
			for (uint8_t *p = playersRef - 20; p < playersRef; p++)
			{
				if (p[0] == 0x8B && (p[1] == 0x0D || p[1] == 0x15 || p[1] == 0x35))
				{
					int *candidate = *(int **)(p + 2);
					if (!IsBadReadPtr(candidate, 4))
					{
						g_pMaxPlayers = candidate;
						RealMasterLog("Engine hook: maxplayers at %p (value=%d)", g_pMaxPlayers, *g_pMaxPlayers);
						break;
					}
				}
				if (p[0] == 0xFF && p[1] == 0x35)
				{
					int *candidate = *(int **)(p + 2);
					if (!IsBadReadPtr(candidate, 4))
					{
						g_pMaxPlayers = candidate;
						RealMasterLog("Engine hook: maxplayers at %p (value=%d, via PUSH)", g_pMaxPlayers, *g_pMaxPlayers);
						break;
					}
				}
			}
		}
	}

	if (g_pMaxPlayers)
	{
		uint8_t **ppClientBase = (uint8_t **)(((uint8_t *)g_pMaxPlayers) - 4);
		if (!IsBadReadPtr(ppClientBase, 4) && !IsBadReadPtr(*ppClientBase, 4))
		{
			g_pClientArray = *ppClientBase;
			RealMasterLog("Engine hook: client array at %p", g_pClientArray);
		}
	}

	if (playersStr)
	{
		uint8_t playersPush2[5] = { 0x68 };
		memcpy(playersPush2 + 1, &playersStr, 4);
		uint8_t *ref = FindPattern(base, imageSize, playersPush2, 5);
		if (ref)
		{
			for (uint8_t *p = ref; p < ref + 1024; p++)
			{
				if (p[0] == 0x81 && (p[1] & 0xF8) == 0xC0)
				{
					int val = *(int *)(p + 2);
					if (val > 0x1000 && val < 0x10000)
					{
						g_clientStride = val;
						RealMasterLog("Engine hook: client stride = 0x%x (%d)", g_clientStride, g_clientStride);
						break;
					}
				}
			}
		}
	}
	if (g_clientStride == 0) g_clientStride = 0x5018;

	const char *gdNeedle = "*gamedir";
	uint8_t *gdStr = FindPattern(base, imageSize, (const uint8_t *)gdNeedle, strlen(gdNeedle) + 1);
	if (gdStr)
	{
		uint8_t gdPush[5] = { 0x68 };
		memcpy(gdPush + 1, &gdStr, 4);
		uint8_t *gdRef = FindPattern(base, imageSize, gdPush, 5);
		if (gdRef)
		{
			for (uint8_t *p = gdRef + 5; p < gdRef + 30; p++)
			{
				if (p[0] == 0x68)
				{
					char *candidate = *(char **)(p + 1);
					if (!IsBadReadPtr(candidate, 4) && candidate >= (char*)base && candidate < (char*)(base + imageSize))
					{
						g_pGameDir = candidate;
						RealMasterLog("Engine hook: gamedir at %p ('%s')", g_pGameDir, g_pGameDir);
						break;
					}
				}
			}
		}
	}

	const char *netNeedle = "NET_SendPacket: bad address type";
	uint8_t *netStr = FindPattern(base, imageSize, (const uint8_t *)netNeedle, strlen(netNeedle));
	if (netStr)
	{
		uint8_t netPush[5] = { 0x68 };
		memcpy(netPush + 1, &netStr, 4);
		uint8_t *netRef = FindPattern(base, imageSize, netPush, 5);
		if (netRef)
		{
			uint8_t *fn = netRef;
			while (fn > base && !(fn[0] == 0x55 && fn[1] == 0x8B && fn[2] == 0xEC))
			{
				if (fn[0] == 0xCC && fn[1] != 0xCC) { fn++; break; }
				fn--;
			}
			for (uint8_t *p = fn; p < netRef; p++)
			{
				if (p[0] == 0x8B && p[1] == 0x34 && (p[2] & 0xC7) == 0x85)
				{
					SOCKET *sockArray = *(SOCKET **)(p + 3);
					if (!IsBadReadPtr(sockArray, 8))
					{
						g_pServerSocket = &sockArray[1];
						RealMasterLog("Engine hook: ip_sockets at %p, server socket ptr at %p",
							sockArray, g_pServerSocket);
						break;
					}
				}
			}
		}
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
			Reunion_InstallHook(base, imageSize, g_hRealSteamApi, (void *)g_pCvarFindVar,
				g_pServerState, g_pClientArray, g_pMaxPlayers, g_clientStride);
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

	Reunion_PostConnect();
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

	if (g_heartbeatActive && g_heartbeatMaster[0])
	{
		if (g_pServerState && *(int *)g_pServerState != 0)
		{
			DWORD now = GetTickCount();
			if (now - g_lastHeartbeat > 30000)
			{
				g_lastHeartbeat = now;
				heartbeat_info_t hbinfo;
				GatherHeartbeatInfo(&hbinfo);
				SOCKET svSock = (g_pServerSocket && !IsBadReadPtr(g_pServerSocket, 4)) ? *g_pServerSocket : INVALID_SOCKET;
				master_send_heartbeat(g_heartbeatMaster, &hbinfo, svSock);
			}
		}
		else
		{
			g_heartbeatActive = false;
			RealMasterLog("Server stopped, disabling heartbeat");
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
