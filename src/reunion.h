#pragma once
#include <windows.h>
#include <stdint.h>

struct reunion_config_t
{
	int AuthVersion;
	char SteamIdHashSalt[65];
	int SC2009_RevCompatMode;
	int EnableSXEIdGeneration;
	int EnableGenPrefix2;
	int IDClientsLimit;
	int LoggingMode;
	char HLTVExcept_IP[64];

	int cid_RealSteam;
	int cid_PendingSteam;
	int cid_HLTV;
	int cid_NoSteam47;
	int cid_NoSteam48;
	int cid_RevEmu;
	int cid_SETTi;
	int cid_StmEmu;
	int cid_AVSMP;
	int cid_SxEI;

	int ServerInfoAnswerType;
	int FixBuggedQuery;
	int EnableQueryLimiter;
	int QueryFloodBanLevel;
	int QueryFloodBanTime;
	int AllowSplitPackets;

	uint32_t hltvExceptIPAddr;

	int IPGen_Prefix1;
	int IPGen_Prefix2;
	int Native_Prefix1;
	int RevEmu_Prefix1;
	int SC2009_Prefix1;
	int RevEmu2013_Prefix1;
	int SteamEmu_Prefix1;
	int OldRevEmu_Prefix1;
	int Setti_Prefix1;
	int AVSMP_Prefix1;
	int SXEI_Prefix1;
};

extern reunion_config_t g_reunionConfig;

void Reunion_LoadConfig(const char *halfLifeDir, const char *gameDir);
bool Reunion_InstallHook(uint8_t *hwBase, size_t hwSize, HMODULE hRealSteamApi, void *pCvarFindVar,
	void *pServerState, uint8_t *pClientArray, int *pMaxPlayers, int clientStride);
void Reunion_PostConnect();
