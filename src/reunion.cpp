#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "reunion.h"
#include "reunion_auth.h"

extern void RealMasterLog(const char *fmt, ...);

reunion_config_t g_reunionConfig;

static void InitConfigDefaults()
{
	memset(&g_reunionConfig, 0, sizeof(g_reunionConfig));
	g_reunionConfig.AuthVersion = 4;
	g_reunionConfig.SC2009_RevCompatMode = 1;
	g_reunionConfig.EnableSXEIdGeneration = 0;
	g_reunionConfig.EnableGenPrefix2 = 0;
	g_reunionConfig.IDClientsLimit = 1;
	g_reunionConfig.LoggingMode = 3;
	strncpy(g_reunionConfig.HLTVExcept_IP, "127.0.0.1", sizeof(g_reunionConfig.HLTVExcept_IP) - 1);

	g_reunionConfig.cid_RealSteam = 1;
	g_reunionConfig.cid_PendingSteam = 5;
	g_reunionConfig.cid_HLTV = 5;
	g_reunionConfig.cid_NoSteam47 = 5;
	g_reunionConfig.cid_NoSteam48 = 5;
	g_reunionConfig.cid_RevEmu = 1;
	g_reunionConfig.cid_SETTi = 3;
	g_reunionConfig.cid_StmEmu = 1;
	g_reunionConfig.cid_AVSMP = 1;
	g_reunionConfig.cid_SxEI = 1;

	g_reunionConfig.ServerInfoAnswerType = 0;
	g_reunionConfig.FixBuggedQuery = 1;
	g_reunionConfig.EnableQueryLimiter = 1;
	g_reunionConfig.QueryFloodBanLevel = 400;
	g_reunionConfig.QueryFloodBanTime = 10;
	g_reunionConfig.AllowSplitPackets = 0;
}

static int ParseIntClamped(const char *val, int min, int max, int def)
{
	int v = atoi(val);
	if (v < min || v > max) return def;
	return v;
}

static void ParseConfigLine(const char *key, const char *val)
{
	if (stricmp(key, "AuthVersion") == 0)
		g_reunionConfig.AuthVersion = ParseIntClamped(val, 1, 4, 4);
	else if (stricmp(key, "SteamIdHashSalt") == 0)
		strncpy(g_reunionConfig.SteamIdHashSalt, val, sizeof(g_reunionConfig.SteamIdHashSalt) - 1);
	else if (stricmp(key, "SC2009_RevCompatMode") == 0)
		g_reunionConfig.SC2009_RevCompatMode = ParseIntClamped(val, 0, 1, 1);
	else if (stricmp(key, "EnableSXEIdGeneration") == 0)
		g_reunionConfig.EnableSXEIdGeneration = ParseIntClamped(val, 0, 1, 0);
	else if (stricmp(key, "EnableGenPrefix2") == 0)
		g_reunionConfig.EnableGenPrefix2 = ParseIntClamped(val, 0, 1, 0);
	else if (stricmp(key, "IDClientsLimit") == 0)
		g_reunionConfig.IDClientsLimit = ParseIntClamped(val, 0, 32, 1);
	else if (stricmp(key, "LoggingMode") == 0)
		g_reunionConfig.LoggingMode = ParseIntClamped(val, 0, 3, 0);
	else if (stricmp(key, "HLTVExcept_IP") == 0)
		strncpy(g_reunionConfig.HLTVExcept_IP, val, sizeof(g_reunionConfig.HLTVExcept_IP) - 1);
	else if (stricmp(key, "cid_Steam") == 0 || stricmp(key, "cid_RealSteam") == 0)
		g_reunionConfig.cid_RealSteam = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "cid_SteamPending") == 0 || stricmp(key, "cid_PendingSteam") == 0)
		g_reunionConfig.cid_PendingSteam = ParseIntClamped(val, 0, 12, 3);
	else if (stricmp(key, "cid_HLTV") == 0)
		g_reunionConfig.cid_HLTV = ParseIntClamped(val, 0, 12, 7);
	else if (stricmp(key, "cid_NoSteam47") == 0)
		g_reunionConfig.cid_NoSteam47 = ParseIntClamped(val, 0, 12, 3);
	else if (stricmp(key, "cid_NoSteam48") == 0)
		g_reunionConfig.cid_NoSteam48 = ParseIntClamped(val, 0, 12, 3);
	else if (stricmp(key, "cid_RevEmu") == 0)
		g_reunionConfig.cid_RevEmu = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "cid_RevEmu2013") == 0)
		g_reunionConfig.cid_RevEmu = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "cid_SC2009") == 0)
		g_reunionConfig.cid_RevEmu = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "cid_OldRevEmu") == 0)
		g_reunionConfig.cid_RevEmu = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "cid_Setti") == 0 || stricmp(key, "cid_SETTi") == 0)
		g_reunionConfig.cid_SETTi = ParseIntClamped(val, 0, 12, 3);
	else if (stricmp(key, "cid_SteamEmu") == 0 || stricmp(key, "cid_StmEmu") == 0)
		g_reunionConfig.cid_StmEmu = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "cid_AVSMP") == 0)
		g_reunionConfig.cid_AVSMP = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "cid_SXEI") == 0 || stricmp(key, "cid_SxEI") == 0)
		g_reunionConfig.cid_SxEI = ParseIntClamped(val, 0, 12, 1);
	else if (stricmp(key, "IPGen_Prefix1") == 0)
		g_reunionConfig.IPGen_Prefix1 = atoi(val);
	else if (stricmp(key, "IPGen_Prefix2") == 0)
		g_reunionConfig.IPGen_Prefix2 = atoi(val);
	else if (stricmp(key, "Native_Prefix1") == 0)
		g_reunionConfig.Native_Prefix1 = atoi(val);
	else if (stricmp(key, "RevEmu_Prefix1") == 0)
		g_reunionConfig.RevEmu_Prefix1 = atoi(val);
	else if (stricmp(key, "SC2009_Prefix1") == 0)
		g_reunionConfig.SC2009_Prefix1 = atoi(val);
	else if (stricmp(key, "RevEmu2013_Prefix1") == 0)
		g_reunionConfig.RevEmu2013_Prefix1 = atoi(val);
	else if (stricmp(key, "SteamEmu_Prefix1") == 0)
		g_reunionConfig.SteamEmu_Prefix1 = atoi(val);
	else if (stricmp(key, "OldRevEmu_Prefix1") == 0)
		g_reunionConfig.OldRevEmu_Prefix1 = atoi(val);
	else if (stricmp(key, "Setti_Prefix1") == 0)
		g_reunionConfig.Setti_Prefix1 = atoi(val);
	else if (stricmp(key, "AVSMP_Prefix1") == 0)
		g_reunionConfig.AVSMP_Prefix1 = atoi(val);
	else if (stricmp(key, "SXEI_Prefix1") == 0)
		g_reunionConfig.SXEI_Prefix1 = atoi(val);
	else if (stricmp(key, "ServerInfoAnswerType") == 0)
		g_reunionConfig.ServerInfoAnswerType = ParseIntClamped(val, 0, 2, 0);
	else if (stricmp(key, "FixBuggedQuery") == 0)
		g_reunionConfig.FixBuggedQuery = ParseIntClamped(val, 0, 1, 1);
	else if (stricmp(key, "EnableQueryLimiter") == 0)
		g_reunionConfig.EnableQueryLimiter = ParseIntClamped(val, 0, 1, 1);
	else if (stricmp(key, "QueryFloodBanLevel") == 0)
		g_reunionConfig.QueryFloodBanLevel = ParseIntClamped(val, 0, 8192, 400);
	else if (stricmp(key, "QueryFloodBanTime") == 0)
		g_reunionConfig.QueryFloodBanTime = ParseIntClamped(val, 0, 60, 10);
	else if (stricmp(key, "AllowSplitPackets") == 0)
		g_reunionConfig.AllowSplitPackets = ParseIntClamped(val, 0, 1, 0);
}

static bool ParseConfigFile(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return false;

	RealMasterLog("Reunion: loading config from %s", path);

	char line[512];
	if (fgets(line, sizeof(line), f))
	{
		if ((uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF)
			memmove(line, line + 3, strlen(line + 3) + 1);
		fseek(f, 0, SEEK_SET);
	}

	while (fgets(line, sizeof(line), f))
	{
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == ';' || *p == '/' || *p == '\\' || *p == '\0' || *p == '\r' || *p == '\n')
			continue;
		if (*p == '[') continue;

		char *eq = strchr(p, '=');
		if (!eq) continue;

		*eq = '\0';
		char *key = p;
		char *val = eq + 1;

		while (*key && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t'))
			key[strlen(key) - 1] = '\0';
		while (*val == ' ' || *val == '\t') val++;

		char *end = val + strlen(val) - 1;
		while (end > val && (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t'))
			*end-- = '\0';

		char *comment = strstr(val, "//");
		if (comment) { *comment = '\0'; end = comment - 1; while (end > val && *end == ' ') *end-- = '\0'; }
		comment = strchr(val, '#');
		if (comment) { *comment = '\0'; end = comment - 1; while (end > val && *end == ' ') *end-- = '\0'; }

		if (*key && *val)
			ParseConfigLine(key, val);
	}

	fclose(f);
	return true;
}

void Reunion_LoadConfig(const char *halfLifeDir, const char *gameDir)
{
	InitConfigDefaults();

	char path[512];
	snprintf(path, sizeof(path), "%s\\%s\\reunion.cfg", halfLifeDir, gameDir);
	if (ParseConfigFile(path)) goto loaded;

	snprintf(path, sizeof(path), "%s\\platform\\config\\reunion.cfg", halfLifeDir);
	if (ParseConfigFile(path)) goto loaded;

	snprintf(path, sizeof(path), "%s\\reunion.cfg", halfLifeDir);
	if (ParseConfigFile(path)) goto loaded;

	RealMasterLog("Reunion: no reunion.cfg found, using defaults");
	return;

loaded:
	if (g_reunionConfig.HLTVExcept_IP[0])
		g_reunionConfig.hltvExceptIPAddr = inet_addr(g_reunionConfig.HLTVExcept_IP);

	RealMasterLog("Reunion: config: AuthVersion=%d LoggingMode=%d IDClientsLimit=%d",
		g_reunionConfig.AuthVersion, g_reunionConfig.LoggingMode, g_reunionConfig.IDClientsLimit);
	if (g_reunionConfig.SteamIdHashSalt[0])
		RealMasterLog("Reunion: config: SteamIdHashSalt is set (%d chars)", (int)strlen(g_reunionConfig.SteamIdHashSalt));
}

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

static void PatchBytes(uint8_t *addr, const uint8_t *bytes, int len)
{
	DWORD oldProt;
	VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt);
	memcpy(addr, bytes, len);
	VirtualProtect(addr, len, oldProt, &oldProt);
}

typedef int (__cdecl *SteamAuthValidate_t)(int client, void *cert, int certLen);
typedef int (__cdecl *GetHSteamUser_t)();
typedef void *(__cdecl *FindOrCreateInterface_t)(int hUser, const char *version);

static SteamAuthValidate_t g_pfnOrigValidate = NULL;
static uint8_t *g_pValidateFunc = NULL;
static uint8_t *g_pTrampoline = NULL;

static GetHSteamUser_t g_pfnGetHSteamUser = NULL;
static FindOrCreateInterface_t g_pfnFindOrCreate = NULL;

static int g_steamIdLowOffset = 0;
static int g_steamIdHighOffset = 0;

typedef int (__cdecl *HltvUnauthFunc_t)(int client);
static HltvUnauthFunc_t g_pfnHltvUnauth = NULL;

struct cvar_s { char *name; char *string; int flags; float value; cvar_s *next; };
typedef cvar_s *(__cdecl *CvarFindVar_t)(const char *name);
static CvarFindVar_t g_pCvarFind = NULL;
static bool g_svLanOverride = false;
static float g_svLanOrigValue = 0.0f;
static char g_svLanOrigString[32] = {0};
static void **g_ppServerState = NULL;
static uint8_t *g_pClientArray = NULL;
static int *g_pMaxPlayers = NULL;
static int g_clientStride = 0;
static int g_authTypeOffset = 0;

static void *GetGameServerInterface()
{
	if (!g_pfnFindOrCreate) return NULL;
	if (g_pfnGetHSteamUser)
	{
		int hUser = g_pfnGetHSteamUser();
		if (hUser == 0) return NULL;
		return g_pfnFindOrCreate(hUser, "SteamGameServer015");
	}
	return g_pfnFindOrCreate(0, NULL);
}

static int __cdecl Reunion_ValidateClient(int client, void *cert, int certLen)
{
	if (g_reunionConfig.LoggingMode > 0)
		RealMasterLog("Reunion: ValidateClient called (client=%p, certLen=%d)", (void *)client, certLen);

	SteamAuthValidate_t pfnTrampoline = (SteamAuthValidate_t)g_pTrampoline;
	int result = pfnTrampoline(client, cert, certLen);

	if (result != 0)
	{
		RealMasterLog("Reunion: Steam validation OK for client %p", (void *)client);
		return result;
	}

	authdata_t authdata;
	memset(&authdata, 0, sizeof(authdata));
	authdata.authTicket = (uint8_t *)cert;
	authdata.ticketLen = certLen;
	authdata.protocol = 48;
	if (client)
	{
		uint8_t *cl = (uint8_t *)client;
		if (!IsBadReadPtr(cl + 0x28, 4))
			authdata.ipaddr = *(uint32_t *)(cl + 0x28);
		if (!IsBadReadPtr(cl + 0x3598, 4))
			authdata.userinfo = (char *)(cl + 0x3598);
	}

	if (client)
	{
		uint8_t *cl = (uint8_t *)client;
		RealMasterLog("Reunion: client+0x20: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
			cl[0x20], cl[0x21], cl[0x22], cl[0x23], cl[0x24], cl[0x25], cl[0x26], cl[0x27],
			cl[0x28], cl[0x29], cl[0x2A], cl[0x2B], cl[0x2C], cl[0x2D], cl[0x2E], cl[0x2F],
			cl[0x30], cl[0x31], cl[0x32], cl[0x33]);

		uint8_t *ticket = (uint8_t *)cert;
		if (certLen >= 16)
			RealMasterLog("Reunion: ticket[0..15]: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
				ticket[0], ticket[1], ticket[2], ticket[3], ticket[4], ticket[5], ticket[6], ticket[7],
				ticket[8], ticket[9], ticket[10], ticket[11], ticket[12], ticket[13], ticket[14], ticket[15]);
	}

	RealMasterLog("Reunion: authdata ip=0x%08X, ticketLen=%d, userinfo=%s",
		authdata.ipaddr, authdata.ticketLen,
		authdata.userinfo ? "(set)" : "(null)");

	client_auth_kind authkind = Reunion_Authorize_Client(&authdata);
	if (authkind == CA_UNKNOWN)
		authkind = CA_NO_STEAM_48;

	uint32_t steamId = Reunion_ProcessAuth(&authdata, authkind);
	RealMasterLog("Reunion: ProcessAuth returned steamId=%u (0x%08X)", steamId, steamId);
	if (steamId == 0)
		return 0;

	if (g_pfnHltvUnauth)
	{
		int unauthResult = g_pfnHltvUnauth(client);
		if (unauthResult && g_authTypeOffset > 0 && g_steamIdLowOffset > 0)
		{
			*(int *)((uint8_t *)client + g_authTypeOffset) = 1;
			*(uint32_t *)((uint8_t *)client + g_steamIdLowOffset) = steamId;
			*(uint32_t *)((uint8_t *)client + g_steamIdHighOffset) = 0;

			uint32_t verifyLow = *(uint32_t *)((uint8_t *)client + g_steamIdLowOffset);
			uint32_t verifyHigh = *(uint32_t *)((uint8_t *)client + g_steamIdHighOffset);
			int verifyAuth = *(int *)((uint8_t *)client + g_authTypeOffset);
			RealMasterLog("Reunion: client accepted STEAM_0:%d:%d (verify: auth=%d id=%08X:%08X)",
				steamId & 1, steamId >> 1, verifyAuth, verifyHigh, verifyLow);
			return 1;
		}
	}

	return 0;
}

void Reunion_PostConnect()
{
}

bool Reunion_InstallHook(uint8_t *hwBase, size_t hwSize, HMODULE hRealSteamApi, void *pCvarFindVar,
	void *pServerState, uint8_t *pClientArray, int *pMaxPlayers, int clientStride)
{
	g_pCvarFind = (CvarFindVar_t)pCvarFindVar;
	g_ppServerState = (void **)pServerState;
	g_pClientArray = pClientArray;
	g_pMaxPlayers = pMaxPlayers;
	g_clientStride = clientStride;

	const uint8_t authFlagPattern[] = { 0x8A, 0x81, 0x86, 0x00, 0x00, 0x00, 0x84, 0xC0 };
	int authPatches = 0;
	uint8_t *searchStart = hwBase;
	for (int i = 0; i < 10; i++)
	{
		uint8_t *match = FindPattern(searchStart, hwSize - (searchStart - hwBase), authFlagPattern, sizeof(authFlagPattern));
		if (!match) break;

		uint8_t *jccAddr = match + 8;
		if (jccAddr[0] == 0x0F && jccAddr[1] == 0x85)
		{
			RealMasterLog("Reunion: patching near JNZ at %p (auth flag check) -> JMP", jccAddr);
			int32_t offset = *(int32_t *)(jccAddr + 2);
			uint8_t *target = jccAddr + 6 + offset;
			uint8_t patch[6];
			patch[0] = 0xE9;
			int32_t newRel = (int32_t)(target - (jccAddr + 5));
			memcpy(patch + 1, &newRel, 4);
			patch[5] = 0x90;
			PatchBytes(jccAddr, patch, 6);
			authPatches++;
		}
		else if (jccAddr[0] == 0x75)
		{
			RealMasterLog("Reunion: patching short JNZ at %p (auth flag check) -> JMP", jccAddr);
			uint8_t patch = 0xEB;
			PatchBytes(jccAddr, &patch, 1);
			authPatches++;
		}

		searchStart = match + sizeof(authFlagPattern);
	}
	if (authPatches > 0)
		RealMasterLog("Reunion: patched %d auth callback flag checks (non-Steam clients won't be kicked)", authPatches);

	const char *certNeedle = "STEAM certificate length error!";
	uint8_t *certStr = FindPattern(hwBase, hwSize, (const uint8_t *)certNeedle, strlen(certNeedle));
	if (certStr)
	{
		uint8_t certPush[5] = { 0x68 };
		memcpy(certPush + 1, &certStr, 4);
		uint8_t *certPushRef = FindPattern(hwBase, hwSize, certPush, 5);
		if (certPushRef)
		{
			RealMasterLog("Reunion: cert error PUSH at %p, scanning for JLE...", certPushRef);
			for (uint8_t *p = certPushRef - 256; p < certPushRef; p++)
			{
				if (p[0] == 0x0F && p[1] == 0x8E)
				{
					int32_t offset = *(int32_t *)(p + 2);
					uint8_t *target = p + 6 + offset;
					int diff = (int)(certPushRef - target);
					if (diff >= -16 && diff <= 32)
					{
						RealMasterLog("Reunion: found JLE at %p -> %p (cert <= 0 check)", p, target);
						uint8_t nops[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
						PatchBytes(p, nops, 6);
						RealMasterLog("Reunion: NOPed cert length <= 0 check (empty certs allowed)");
						break;
					}
				}
				if (p[0] == 0x7E)
				{
					int8_t offset = (int8_t)p[1];
					uint8_t *target = p + 2 + offset;
					int diff = (int)(certPushRef - target);
					if (diff >= -16 && diff <= 32)
					{
						RealMasterLog("Reunion: found short JLE at %p -> %p (cert <= 0 check)", p, target);
						uint8_t nops[2] = { 0x90, 0x90 };
						PatchBytes(p, nops, 2);
						RealMasterLog("Reunion: NOPed cert length <= 0 check (empty certs allowed)");
						break;
					}
				}
			}
		}
	}

	const char *needle = "STEAM validation rejected\n";
	uint8_t *strAddr = FindPattern(hwBase, hwSize, (const uint8_t *)needle, strlen(needle));
	if (!strAddr)
	{
		RealMasterLog("Reunion: could not find 'STEAM validation rejected' string");
		return false;
	}

	uint8_t pushBytes[5];
	pushBytes[0] = 0x68;
	memcpy(pushBytes + 1, &strAddr, 4);
	uint8_t *pushRef = FindPattern(hwBase, hwSize, pushBytes, 5);
	if (!pushRef)
	{
		RealMasterLog("Reunion: could not find xref to validation rejected string");
		return false;
	}

	RealMasterLog("Reunion: rejection PUSH at %p", pushRef);

	uint8_t *jccTarget = NULL;
	for (uint8_t *p = pushRef - 16; p < pushRef; p++)
	{
		if (p[0] >= 0x70 && p[0] <= 0x7F)
		{
			int8_t offset = (int8_t)p[1];
			jccTarget = p + 2 + offset;
			RealMasterLog("Reunion: found short Jcc 0x%02X at %p -> %p", p[0], p, jccTarget);
			uint8_t patch = 0xEB;
			PatchBytes(p, &patch, 1);
			RealMasterLog("Reunion: patched Jcc -> JMP short (safety net)");
			break;
		}
		if (p[0] == 0x0F && p[1] >= 0x80 && p[1] <= 0x8F)
		{
			int32_t offset = *(int32_t *)(p + 2);
			jccTarget = p + 6 + offset;
			uint8_t jmpPatch[6];
			jmpPatch[0] = 0xE9;
			int32_t newRel = (int32_t)(jccTarget - (p + 5));
			memcpy(jmpPatch + 1, &newRel, 4);
			jmpPatch[5] = 0x90;
			PatchBytes(p, jmpPatch, 6);
			RealMasterLog("Reunion: patched near Jcc -> JMP near (safety net), target %p", jccTarget);
			break;
		}
	}

	if (jccTarget)
	{
		RealMasterLog("Reunion: LAN acceptance at %p, bytes:", jccTarget);
		RealMasterLog("  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			jccTarget[0], jccTarget[1], jccTarget[2], jccTarget[3], jccTarget[4], jccTarget[5],
			jccTarget[6], jccTarget[7], jccTarget[8], jccTarget[9], jccTarget[10], jccTarget[11],
			jccTarget[12], jccTarget[13], jccTarget[14], jccTarget[15], jccTarget[16], jccTarget[17],
			jccTarget[18], jccTarget[19]);
		RealMasterLog("  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			jccTarget[20], jccTarget[21], jccTarget[22], jccTarget[23], jccTarget[24], jccTarget[25],
			jccTarget[26], jccTarget[27], jccTarget[28], jccTarget[29], jccTarget[30], jccTarget[31],
			jccTarget[32], jccTarget[33], jccTarget[34], jccTarget[35], jccTarget[36], jccTarget[37],
			jccTarget[38], jccTarget[39]);

		int authFieldCount = 0;
		for (uint8_t *p = jccTarget; p < jccTarget + 64; p++)
		{
			int offset = 0;
			bool isZeroWrite = false;

			if (p[0] == 0xC7 && (p[1] & 0xC7) == 0x80)
			{
				offset = *(int *)(p + 2);
				isZeroWrite = (*(int *)(p + 6) == 0);
			}
			else if (p[0] == 0x89 && (p[1] & 0xC7) == 0x80)
			{
				offset = *(int *)(p + 2);
				isZeroWrite = true;
			}

			if (offset > 0x1000)
			{
				authFieldCount++;
				if (authFieldCount == 1)
				{
					g_authTypeOffset = offset;
					RealMasterLog("Reunion: LAN auth type offset = +0x%X", offset);
				}
				else if (authFieldCount == 2 && isZeroWrite)
				{
					g_steamIdLowOffset = offset;
					RealMasterLog("Reunion: SteamID low offset = +0x%X", offset);
				}
				else if (authFieldCount == 3 && isZeroWrite)
				{
					g_steamIdHighOffset = offset;
					RealMasterLog("Reunion: SteamID high offset = +0x%X", offset);
				}
			}
		}
	}

	if (hRealSteamApi)
	{
		g_pfnGetHSteamUser = (GetHSteamUser_t)GetProcAddress(hRealSteamApi, "SteamGameServer_GetHSteamUser");
		g_pfnFindOrCreate = (FindOrCreateInterface_t)GetProcAddress(hRealSteamApi, "SteamInternal_FindOrCreateUserInterface");
		if (!g_pfnFindOrCreate)
		{
			typedef void *(__cdecl *SteamGameServer_t)();
			SteamGameServer_t pfnDirect = (SteamGameServer_t)GetProcAddress(hRealSteamApi, "SteamGameServer");
			if (pfnDirect)
			{
				g_pfnGetHSteamUser = NULL;
				g_pfnFindOrCreate = (FindOrCreateInterface_t)pfnDirect;
				RealMasterLog("Reunion: using SteamGameServer() direct export at %p", pfnDirect);
			}
		}
		RealMasterLog("Reunion: Steam API resolved: GetHSteamUser=%p, FindOrCreate=%p",
			g_pfnGetHSteamUser, g_pfnFindOrCreate);
	}

	uint8_t *validationCall = NULL;
	for (uint8_t *p = pushRef - 48; p < pushRef - 16; p++)
	{
		if (p[0] == 0xE8)
		{
			void *target = ResolveCall(p);
			if (target && (uint8_t *)target >= hwBase && (uint8_t *)target < hwBase + hwSize)
			{
				if (p[5] == 0x83 && p[6] == 0xC4)
				{
					validationCall = p;
				}
			}
		}
	}

	if (!validationCall)
	{
		RealMasterLog("Reunion: could not find validation CALL, detour not installed");
		return true;
	}

	g_pValidateFunc = (uint8_t *)ResolveCall(validationCall);
	g_pfnOrigValidate = (SteamAuthValidate_t)g_pValidateFunc;
	RealMasterLog("Reunion: validation function at %p", g_pValidateFunc);

	int hltvOffsets[] = { 0x30, -0x70, 0x40, -0x60, -0x80, 0x50 };
	for (int i = 0; i < 6; i++)
	{
		uint8_t *candidate = g_pValidateFunc + hltvOffsets[i];
		if (candidate >= hwBase && candidate + 32 < hwBase + hwSize &&
			candidate[0] == 0x55 && candidate[1] == 0x8B && candidate[2] == 0xEC)
		{
			uint8_t *fnEnd = candidate + 64;
			bool hasServerStateCheck = false;
			for (uint8_t *p = candidate + 3; p < fnEnd; p++)
			{
				if (p[0] == 0xA1)
				{
					uint32_t addr = *(uint32_t *)(p + 1);
					if (addr == *(uint32_t *)(g_pValidateFunc + 4))
					{
						hasServerStateCheck = true;
						break;
					}
				}
			}
			if (hasServerStateCheck)
			{
				g_pfnHltvUnauth = (HltvUnauthFunc_t)candidate;
				RealMasterLog("Reunion: HLTV unauth function at %p (validate%+d)", candidate, hltvOffsets[i]);
				break;
			}
		}
	}

	bool canDetour = (g_pfnHltvUnauth != NULL);

	if (!canDetour)
	{
		RealMasterLog("Reunion: detour prerequisites missing, Jcc patch only");
		return true;
	}

	int copyLen = 0;
	uint8_t *p = g_pValidateFunc;
	while (copyLen < 5)
	{
		if (p[0] == 0x55) copyLen += 1;
		else if (p[0] == 0x8B && p[1] == 0xEC) copyLen += 2;
		else if (p[0] == 0xA1) copyLen += 5;
		else if (p[0] == 0x83 && p[1] == 0xEC) copyLen += 3;
		else if (p[0] == 0x56) copyLen += 1;
		else if (p[0] == 0x57) copyLen += 1;
		else if (p[0] == 0x53) copyLen += 1;
		else copyLen += 1;
		p = g_pValidateFunc + copyLen;
	}

	g_pTrampoline = (uint8_t *)VirtualAlloc(NULL, copyLen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!g_pTrampoline)
	{
		RealMasterLog("Reunion: VirtualAlloc for trampoline failed");
		return true;
	}

	memcpy(g_pTrampoline, g_pValidateFunc, copyLen);
	g_pTrampoline[copyLen] = 0xE9;
	int32_t trampolineRel = (int32_t)((g_pValidateFunc + copyLen) - (g_pTrampoline + copyLen + 5));
	memcpy(g_pTrampoline + copyLen + 1, &trampolineRel, 4);

	uint8_t jmpBytes[5];
	jmpBytes[0] = 0xE9;
	int32_t jmpRel = (int32_t)((uint8_t *)Reunion_ValidateClient - (g_pValidateFunc + 5));
	memcpy(jmpBytes + 1, &jmpRel, 4);
	PatchBytes(g_pValidateFunc, jmpBytes, 5);

	RealMasterLog("Reunion: trampoline at %p (%d bytes copied), detour installed", g_pTrampoline, copyLen);
	return true;
}
