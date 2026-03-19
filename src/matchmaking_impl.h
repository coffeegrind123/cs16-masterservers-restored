#pragma once
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>

#define MAX_GAME_SERVERS 16384

typedef void *HServerListRequest;
typedef int HServerQuery;

class servernetadr_t
{
public:
	void Init(unsigned int ip, uint16_t usQueryPort, uint16_t usConnectionPort)
	{
		m_unIP = ip;
		m_usQueryPort = usQueryPort;
		m_usConnectionPort = usConnectionPort;
	}
	uint16_t GetQueryPort() const { return m_usQueryPort; }
	uint16_t GetConnectionPort() const { return m_usConnectionPort; }
	uint32_t GetIP() const { return m_unIP; }
private:
	uint16_t m_usConnectionPort;
	uint16_t m_usQueryPort;
	uint32_t m_unIP;
};

class gameserveritem_t
{
public:
	servernetadr_t m_NetAdr;
	int m_nPing;
	bool m_bHadSuccessfulResponse;
	bool m_bDoNotRefresh;
	char m_szGameDir[32];
	char m_szMap[32];
	char m_szGameDescription[64];
	uint32_t m_nAppID;
	int m_nPlayers;
	int m_nMaxPlayers;
	int m_nBotPlayers;
	bool m_bPassword;
	bool m_bSecure;
	uint32_t m_ulTimeLastPlayed;
	int m_nServerVersion;
private:
	char m_szServerName[64];
public:
	char m_szGameTags[128];
	uint64_t m_steamID;

	const char *GetName() const { return m_szServerName[0] ? m_szServerName : ""; }
	void SetName(const char *pName)
	{
		strncpy(m_szServerName, pName, sizeof(m_szServerName) - 1);
		m_szServerName[sizeof(m_szServerName) - 1] = '\0';
	}
};

struct MatchMakingKeyValuePair_t
{
	char m_szKey[256];
	char m_szValue[256];
};

enum EMatchMakingServerResponse
{
	eServerResponded = 0,
	eServerFailedToRespond,
	eNoServersListedOnMasterServer
};

class ISteamMatchmakingServerListResponse
{
public:
	virtual void ServerResponded(HServerListRequest hRequest, int iServer) = 0;
	virtual void ServerFailedToRespond(HServerListRequest hRequest, int iServer) = 0;
	virtual void RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response) = 0;
};

class ISteamMatchmakingPingResponse
{
public:
	virtual void ServerResponded(gameserveritem_t &server) = 0;
	virtual void ServerFailedToRespond() = 0;
};

class ISteamMatchmakingPlayersResponse
{
public:
	virtual void AddPlayerToList(const char *pchName, int nScore, float flTimePlayed) = 0;
	virtual void PlayersFailedToRespond() = 0;
	virtual void PlayersRefreshComplete() = 0;
};

class ISteamMatchmakingRulesResponse
{
public:
	virtual void RulesResponded(const char *pchRule, const char *pchValue) = 0;
	virtual void RulesFailedToRespond() = 0;
	virtual void RulesRefreshComplete() = 0;
};

class ISteamMatchmakingServers
{
public:
	virtual HServerListRequest RequestInternetServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) = 0;
	virtual HServerListRequest RequestLANServerList(uint32_t iApp, ISteamMatchmakingServerListResponse *pRequestServersResponse) = 0;
	virtual HServerListRequest RequestFriendsServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) = 0;
	virtual HServerListRequest RequestFavoritesServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) = 0;
	virtual HServerListRequest RequestHistoryServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) = 0;
	virtual HServerListRequest RequestSpectatorServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) = 0;
	virtual void ReleaseRequest(HServerListRequest hServerListRequest) = 0;
	virtual gameserveritem_t *GetServerDetails(HServerListRequest hRequest, int iServer) = 0;
	virtual void CancelQuery(HServerListRequest hRequest) = 0;
	virtual void RefreshQuery(HServerListRequest hRequest) = 0;
	virtual bool IsRefreshing(HServerListRequest hRequest) = 0;
	virtual int GetServerCount(HServerListRequest hRequest) = 0;
	virtual void RefreshServer(HServerListRequest hRequest, int iServer) = 0;
	virtual HServerQuery PingServer(uint32_t unIP, uint16_t usPort, ISteamMatchmakingPingResponse *pRequestServersResponse) = 0;
	virtual HServerQuery PlayerDetails(uint32_t unIP, uint16_t usPort, ISteamMatchmakingPlayersResponse *pRequestServersResponse) = 0;
	virtual HServerQuery ServerRules(uint32_t unIP, uint16_t usPort, ISteamMatchmakingRulesResponse *pRequestServersResponse) = 0;
	virtual void CancelServerQuery(HServerQuery hServerQuery) = 0;
};

class CRealMasterMatchmaking : public ISteamMatchmakingServers
{
public:
	CRealMasterMatchmaking();
	~CRealMasterMatchmaking();

	HServerListRequest RequestInternetServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse) override;
	HServerListRequest RequestLANServerList(uint32_t iApp, ISteamMatchmakingServerListResponse *pResponse) override;
	HServerListRequest RequestFriendsServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse) override;
	HServerListRequest RequestFavoritesServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse) override;
	HServerListRequest RequestHistoryServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse) override;
	HServerListRequest RequestSpectatorServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse) override;
	void ReleaseRequest(HServerListRequest hRequest) override;
	gameserveritem_t *GetServerDetails(HServerListRequest hRequest, int iServer) override;
	void CancelQuery(HServerListRequest hRequest) override;
	void RefreshQuery(HServerListRequest hRequest) override;
	bool IsRefreshing(HServerListRequest hRequest) override;
	int GetServerCount(HServerListRequest hRequest) override;
	void RefreshServer(HServerListRequest hRequest, int iServer) override;
	HServerQuery PingServer(uint32_t unIP, uint16_t usPort, ISteamMatchmakingPingResponse *pResponse) override;
	HServerQuery PlayerDetails(uint32_t unIP, uint16_t usPort, ISteamMatchmakingPlayersResponse *pResponse) override;
	HServerQuery ServerRules(uint32_t unIP, uint16_t usPort, ISteamMatchmakingRulesResponse *pResponse) override;
	void CancelServerQuery(HServerQuery hServerQuery) override;

	void DispatchCallbacks();

private:
	static DWORD WINAPI QueryThread(LPVOID param);

	gameserveritem_t m_servers[MAX_GAME_SERVERS];
	volatile int m_serverCount;
	volatile bool m_refreshing;
	ISteamMatchmakingServerListResponse *m_pResponse;
	HANDLE m_hThread;
	uint32_t m_requestCounter;
	volatile bool m_queryDone;
	volatile bool m_cancelRequested;
	volatile int m_lastDispatchedIdx;
	bool m_dispatching;

	ISteamMatchmakingServers *m_pRealSteam;

	friend void SetRealSteamMatchmaking(ISteamMatchmakingServers *pReal);
};

ISteamMatchmakingServers *GetRealMasterMatchmaking();
void SetRealSteamMatchmaking(ISteamMatchmakingServers *pReal);
