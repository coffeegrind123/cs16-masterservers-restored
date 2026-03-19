#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "matchmaking_impl.h"

static HMODULE g_hSelf = NULL;
static HMODULE g_hRealSteamApi = NULL;
static bool g_bRealSteamResolved = false;
static bool g_bWsaInit = false;
static char g_logPath[MAX_PATH] = {0};
static bool g_logTruncated = false;
static CRITICAL_SECTION g_logCS;
static bool g_logCSInit = false;

static char g_selfDir[512] = {0};

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

FORWARD_FUNC(SteamAPI_Init)
FORWARD_FUNC(SteamAPI_Shutdown)
FORWARD_FUNC(SteamFriends)
FORWARD_FUNC(SteamApps)
FORWARD_FUNC(SteamMatchmaking)

extern "C" __declspec(dllexport) void __cdecl SteamAPI_RegisterCallback(void *pCallback, int iCallback)
{
	EnsureRealSteamApi();
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
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		if (g_logCSInit) { DeleteCriticalSection(&g_logCS); g_logCSInit = false; }
	}
	return TRUE;
}
