#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "matchmaking_impl.h"
#include "master_query.h"
#include "a2s_query.h"
#include "vdf_parser.h"
#include "server_cache.h"
#include "utils.h"

extern void RealMasterLog(const char *fmt, ...);

static CRealMasterMatchmaking g_RealMaster;
static master_list_t g_MasterList;
static bool g_MasterListLoaded = false;

struct QueryThreadData
{
	CRealMasterMatchmaking *self;
	gameserveritem_t *servers;
	volatile int *serverCount;
};

static void load_master_list()
{
	if (g_MasterListLoaded) return;
	g_MasterListLoaded = true;

	find_plugin_dir();

	char vdf_path[512];
	snprintf(vdf_path, sizeof(vdf_path), "%s\\platform\\config\\MasterServers.vdf", g_PluginDir);

	if (!vdf_parse_master_servers(vdf_path, &g_MasterList))
	{
		snprintf(vdf_path, sizeof(vdf_path), "platform\\config\\MasterServers.vdf");
		if (!vdf_parse_master_servers(vdf_path, &g_MasterList))
		{
			strcpy(g_MasterList.entries[0].addr, "ms.cs16.net:27010");
			g_MasterList.count = 1;
		}
	}
}

CRealMasterMatchmaking::CRealMasterMatchmaking()
{
	m_serverCount = 0;
	m_refreshing = false;
	m_pResponse = NULL;
	m_hThread = NULL;
	m_requestCounter = 0;
	m_queryDone = false;
	m_cancelRequested = false;
	m_lastDispatchedIdx = 0;
	m_dispatching = false;
	m_pRealSteam = NULL;
	memset(m_servers, 0, sizeof(m_servers));
}

CRealMasterMatchmaking::~CRealMasterMatchmaking()
{
	if (m_hThread)
	{
		WaitForSingleObject(m_hThread, 10000);
		CloseHandle(m_hThread);
	}
}

static bool IsThreadAlive(HANDLE h)
{
	if (!h) return false;
	DWORD code;
	return GetExitCodeThread(h, &code) && code == STILL_ACTIVE;
}

DWORD WINAPI CRealMasterMatchmaking::QueryThread(LPVOID param)
{
	QueryThreadData *data = (QueryThreadData *)param;

	RealMasterLog("QueryThread started");

	load_master_list();
	RealMasterLog("Master list: %d servers configured", g_MasterList.count);

	master_query_result_t master_result;
	memset(&master_result, 0, sizeof(master_result));
	int total = 0;

	for (int m = 0; m < g_MasterList.count && !data->self->m_cancelRequested; m++)
	{
		RealMasterLog("Querying master %s ...", g_MasterList.entries[m].addr);
		master_query_result_t result;
		if (master_query_servers(g_MasterList.entries[m].addr, &result))
		{
			RealMasterLog("  Got %d servers from %s", result.count, g_MasterList.entries[m].addr);
			for (int i = 0; i < result.count && total < MAX_GAME_SERVERS; i++)
			{
				master_result.servers[total] = result.servers[i];
				total++;
			}
		}
		else
		{
			RealMasterLog("  FAILED to query %s", g_MasterList.entries[m].addr);
		}
	}

	RealMasterLog("Total servers from masters: %d", total);

	if (total == 0)
	{
		RealMasterLog("No servers from masters, trying cache");
		char cache_path[512];
		snprintf(cache_path, sizeof(cache_path), "%s\\cache\\servers.dat", g_PluginDir);
		server_cache_t cache;
		if (cache_load(cache_path, &cache))
		{
			for (int i = 0; i < cache.count && i < MAX_GAME_SERVERS; i++)
			{
				master_result.servers[i].ip = cache.servers[i].ip;
				master_result.servers[i].port = cache.servers[i].port;
			}
			total = cache.count;
			RealMasterLog("Loaded %d servers from cache", total);
		}
	}
	else
	{
		char cache_path[512];
		snprintf(cache_path, sizeof(cache_path), "%s\\cache\\servers.dat", g_PluginDir);
		server_cache_t cache;
		cache.count = 0;
		for (int i = 0; i < total && cache.count < MAX_CACHED_SERVERS; i++)
		{
			cache.servers[cache.count].ip = master_result.servers[i].ip;
			cache.servers[cache.count].port = master_result.servers[i].port;
			cache.count++;
		}
		cache_save(cache_path, &cache);
	}

	if (data->self->m_cancelRequested) { data->self->m_queryDone = true; delete data; return 0; }

	int batch_size = 16;
	for (int base = 0; base < total && !data->self->m_cancelRequested; base += batch_size)
	{
		int batch = total - base;
		if (batch > batch_size) batch = batch_size;

		uint32_t ips[16];
		uint16_t ports[16];
		a2s_server_info_t infos[16];

		for (int i = 0; i < batch; i++)
		{
			ips[i] = master_result.servers[base + i].ip;
			ports[i] = master_result.servers[base + i].port;
		}

		int valid = a2s_query_batch(ips, ports, batch, infos, 2000);

		for (int i = 0; i < batch; i++)
		{
			int idx = base + i;
			gameserveritem_t *gs = &data->servers[idx];
			memset(gs, 0, sizeof(*gs));

			gs->m_NetAdr.Init(ntohl(ips[i]), ntohs(ports[i]), ntohs(ports[i]));

			if (infos[i].valid)
			{
				gs->m_nPing = infos[i].ping_ms;
				gs->m_bHadSuccessfulResponse = true;
				gs->SetName(infos[i].name);
				strncpy(gs->m_szMap, infos[i].map, sizeof(gs->m_szMap) - 1);
				strncpy(gs->m_szGameDir, infos[i].gamedir, sizeof(gs->m_szGameDir) - 1);
				strncpy(gs->m_szGameDescription, infos[i].gamedesc, sizeof(gs->m_szGameDescription) - 1);
				gs->m_nAppID = infos[i].appid;
				gs->m_nPlayers = infos[i].players;
				gs->m_nMaxPlayers = infos[i].max_players;
				gs->m_nBotPlayers = infos[i].bots;
				gs->m_bPassword = infos[i].password != 0;
				gs->m_bSecure = infos[i].secure != 0;
			}
			else
			{
				gs->m_bHadSuccessfulResponse = false;
			}

			*data->serverCount = idx + 1;
		}

		RealMasterLog("A2S batch %d-%d: %d/%d responded", base, base+batch-1, valid, batch);
	}

	int responded = 0;
	for (int i = 0; i < *data->serverCount; i++)
		if (data->servers[i].m_bHadSuccessfulResponse) responded++;

	RealMasterLog("QueryThread finished: %d total, %d responded", *data->serverCount, responded);
	data->self->m_queryDone = true;
	delete data;
	return 0;
}

HServerListRequest CRealMasterMatchmaking::RequestInternetServerList(
	uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters,
	ISteamMatchmakingServerListResponse *pResponse)
{
	RealMasterLog("RequestInternetServerList(appID=%u, nFilters=%u, pResponse=%p)", iApp, nFilters, pResponse);

	if (m_hThread)
	{
		if (IsThreadAlive(m_hThread))
		{
			RealMasterLog("  Cancelling previous query");
			m_cancelRequested = true;
			WaitForSingleObject(m_hThread, 500);
		}
		CloseHandle(m_hThread);
		m_hThread = NULL;
	}

	m_serverCount = 0;
	m_refreshing = true;
	m_queryDone = false;
	m_cancelRequested = false;
	m_lastDispatchedIdx = 0;
	m_pResponse = pResponse;
	m_requestCounter++;

	QueryThreadData *data = new QueryThreadData;
	data->self = this;
	data->servers = m_servers;
	data->serverCount = &m_serverCount;

	m_hThread = CreateThread(NULL, 0, QueryThread, data, 0, NULL);

	return (HServerListRequest)(uintptr_t)m_requestCounter;
}

HServerListRequest CRealMasterMatchmaking::RequestLANServerList(uint32_t iApp, ISteamMatchmakingServerListResponse *pResponse)
{
	if (m_pRealSteam) return m_pRealSteam->RequestLANServerList(iApp, pResponse);
	return NULL;
}

HServerListRequest CRealMasterMatchmaking::RequestFriendsServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse)
{
	if (m_pRealSteam) return m_pRealSteam->RequestFriendsServerList(iApp, ppchFilters, nFilters, pResponse);
	return NULL;
}

HServerListRequest CRealMasterMatchmaking::RequestFavoritesServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse)
{
	if (m_pRealSteam) return m_pRealSteam->RequestFavoritesServerList(iApp, ppchFilters, nFilters, pResponse);
	return NULL;
}

HServerListRequest CRealMasterMatchmaking::RequestHistoryServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse)
{
	if (m_pRealSteam) return m_pRealSteam->RequestHistoryServerList(iApp, ppchFilters, nFilters, pResponse);
	return NULL;
}

HServerListRequest CRealMasterMatchmaking::RequestSpectatorServerList(uint32_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32_t nFilters, ISteamMatchmakingServerListResponse *pResponse)
{
	RealMasterLog("RequestSpectatorServerList called (returning NULL)");
	if (pResponse)
		pResponse->RefreshComplete(NULL, eNoServersListedOnMasterServer);
	return NULL;
}

static bool IsOurRequest(HServerListRequest hRequest, uint32_t counter)
{
	return hRequest == (HServerListRequest)(uintptr_t)counter && counter != 0;
}

void CRealMasterMatchmaking::ReleaseRequest(HServerListRequest hRequest)
{
	if (IsOurRequest(hRequest, m_requestCounter))
	{
		RealMasterLog("ReleaseRequest called");
		m_cancelRequested = true;
		m_refreshing = false;
		if (m_hThread)
		{
			WaitForSingleObject(m_hThread, 500);
			CloseHandle(m_hThread);
			m_hThread = NULL;
		}
		m_pResponse = NULL;
		return;
	}
	if (m_pRealSteam) m_pRealSteam->ReleaseRequest(hRequest);
}

gameserveritem_t *CRealMasterMatchmaking::GetServerDetails(HServerListRequest hRequest, int iServer)
{
	if (IsOurRequest(hRequest, m_requestCounter))
	{
		if (iServer < 0 || iServer >= m_serverCount) return NULL;
		return &m_servers[iServer];
	}
	if (m_pRealSteam) return m_pRealSteam->GetServerDetails(hRequest, iServer);
	return NULL;
}

void CRealMasterMatchmaking::CancelQuery(HServerListRequest hRequest)
{
	if (IsOurRequest(hRequest, m_requestCounter))
	{
		RealMasterLog("CancelQuery called");
		m_cancelRequested = true;
		if (m_pResponse)
		{
			m_pResponse->RefreshComplete(hRequest, eNoServersListedOnMasterServer);
			RealMasterLog("Dispatched RefreshComplete after cancel");
		}
		m_refreshing = false;
		return;
	}
	if (m_pRealSteam) m_pRealSteam->CancelQuery(hRequest);
}

void CRealMasterMatchmaking::RefreshQuery(HServerListRequest hRequest)
{
	if (IsOurRequest(hRequest, m_requestCounter)) return;
	if (m_pRealSteam) m_pRealSteam->RefreshQuery(hRequest);
}

void CRealMasterMatchmaking::DispatchCallbacks()
{
	if (!m_pResponse) return;
	if (m_dispatching) return;
	m_dispatching = true;

	if (m_cancelRequested)
	{
		m_dispatching = false;
		return;
	}

	HServerListRequest hReq = (HServerListRequest)(uintptr_t)m_requestCounter;
	int current = m_serverCount;

	if (m_lastDispatchedIdx < current && m_lastDispatchedIdx == 0)
		RealMasterLog("DispatchCallbacks: dispatching %d-%d", m_lastDispatchedIdx, current - 1);

	int dispatched = 0;
	int maxPerFrame = 20;
	while (m_lastDispatchedIdx < current && dispatched < maxPerFrame)
	{
		int idx = m_lastDispatchedIdx;
		if (m_servers[idx].m_bHadSuccessfulResponse)
		{
			m_pResponse->ServerResponded(hReq, idx);
			dispatched++;
		}
		else
		{
			m_pResponse->ServerFailedToRespond(hReq, idx);
		}
		m_lastDispatchedIdx++;
	}

	if (dispatched > 0)
		RealMasterLog("Dispatched %d ServerResponded (dispatched %d/%d)", dispatched, m_lastDispatchedIdx, current);

	if (m_queryDone && !m_cancelRequested && m_lastDispatchedIdx >= m_serverCount)
	{
		int responded = 0;
		for (int i = 0; i < m_serverCount; i++)
			if (m_servers[i].m_bHadSuccessfulResponse) responded++;
		RealMasterLog("Dispatching RefreshComplete (%d total, %d responded)", m_serverCount, responded);
		EMatchMakingServerResponse resp = (m_serverCount > 0) ?
			eServerResponded : eNoServersListedOnMasterServer;
		m_pResponse->RefreshComplete(hReq, resp);
		m_refreshing = false;
		m_queryDone = false;
	}

	m_dispatching = false;
}

bool CRealMasterMatchmaking::IsRefreshing(HServerListRequest hRequest)
{
	if (IsOurRequest(hRequest, m_requestCounter))
	{
		DispatchCallbacks();
		return m_refreshing || IsThreadAlive(m_hThread);
	}
	if (m_pRealSteam) return m_pRealSteam->IsRefreshing(hRequest);
	return false;
}

int CRealMasterMatchmaking::GetServerCount(HServerListRequest hRequest)
{
	if (IsOurRequest(hRequest, m_requestCounter))
	{
		DispatchCallbacks();
		return m_serverCount;
	}
	if (m_pRealSteam) return m_pRealSteam->GetServerCount(hRequest);
	return 0;
}

void CRealMasterMatchmaking::RefreshServer(HServerListRequest hRequest, int iServer)
{
	if (!IsOurRequest(hRequest, m_requestCounter))
	{
		if (m_pRealSteam) m_pRealSteam->RefreshServer(hRequest, iServer);
		return;
	}
	if (iServer < 0 || iServer >= m_serverCount) return;

	gameserveritem_t *gs = &m_servers[iServer];
	uint32_t ip_net = htonl(gs->m_NetAdr.GetIP());
	uint16_t port_net = htons(gs->m_NetAdr.GetQueryPort());

	a2s_server_info_t info;
	if (a2s_query_server(ip_net, port_net, &info))
	{
		gs->m_nPing = info.ping_ms;
		gs->m_bHadSuccessfulResponse = true;
		gs->SetName(info.name);
		strncpy(gs->m_szMap, info.map, sizeof(gs->m_szMap) - 1);
		gs->m_nPlayers = info.players;
		gs->m_nMaxPlayers = info.max_players;
		gs->m_nBotPlayers = info.bots;
		gs->m_bPassword = info.password != 0;
		gs->m_bSecure = info.secure != 0;
	}
}

HServerQuery CRealMasterMatchmaking::PingServer(uint32_t unIP, uint16_t usPort, ISteamMatchmakingPingResponse *pResponse)
{
	if (m_pRealSteam) return m_pRealSteam->PingServer(unIP, usPort, pResponse);
	return -1;
}

HServerQuery CRealMasterMatchmaking::PlayerDetails(uint32_t unIP, uint16_t usPort, ISteamMatchmakingPlayersResponse *pResponse)
{
	if (m_pRealSteam) return m_pRealSteam->PlayerDetails(unIP, usPort, pResponse);
	return -1;
}

HServerQuery CRealMasterMatchmaking::ServerRules(uint32_t unIP, uint16_t usPort, ISteamMatchmakingRulesResponse *pResponse)
{
	if (m_pRealSteam) return m_pRealSteam->ServerRules(unIP, usPort, pResponse);
	return -1;
}

void CRealMasterMatchmaking::CancelServerQuery(HServerQuery hServerQuery)
{
	if (m_pRealSteam) m_pRealSteam->CancelServerQuery(hServerQuery);
}

ISteamMatchmakingServers *GetRealMasterMatchmaking()
{
	return &g_RealMaster;
}

void SetRealSteamMatchmaking(ISteamMatchmakingServers *pReal)
{
	g_RealMaster.m_pRealSteam = pReal;
}
