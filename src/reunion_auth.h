#pragma once
#include <stdint.h>

#define IPGEN_KEY           0xA95CE2B9
#define REVEMU_SIGNATURE    0x72657620
#define REVEMU_SIGNATURE_ALT 0x00726576
#define REVEMU_SIGNATURE2   0
#define STEAM_ID_LAN        0
#define STEAM_ID_PENDING    1
#define STEAMEMU_HASH_KEY   0xC9710266
#define MAX_AUTHKEY_LEN     128

enum client_auth_kind
{
	CA_UNKNOWN = 0,
	CA_HLTV,
	CA_NO_STEAM_47,
	CA_NO_STEAM_48,
	CA_SETTI,
	CA_STEAM,
	CA_STEAM_PENDING,
	CA_STEAM_EMU,
	CA_OLD_REVEMU,
	CA_REVEMU,
	CA_STEAMCLIENT_2009,
	CA_REVEMU_2013,
	CA_AVSMP,
	CA_SXEI,
	CA_MAX
};

enum auth_key_kind
{
	AK_STEAM,
	AK_VOLUMEID,
	AK_HDDSN,
	AK_FILEID,
	AK_SXEID,
	AK_OTHER,
	AK_MAX
};

enum client_id_kind
{
	CI_UNKNOWN = 0,
	CI_REAL_STEAM = 1,
	CI_REAL_VALVE = 2,
	CI_STEAM_BY_IP = 3,
	CI_VALVE_BY_IP = 4,
	CI_DEPRECATED = 5,
	CI_RESERVED = 6,
	CI_HLTV = 7,
	CI_STEAM_ID_LAN = 8,
	CI_STEAM_ID_PENDING = 9,
	CI_VALVE_ID_LAN = 10,
	CI_VALVE_ID_PENDING = 11,
	CI_STEAM_666_88_666 = 12,
	CI_MAX = 13
};

struct authdata_t
{
	uint8_t *authTicket;
	int ticketLen;
	int protocol;
	char *userinfo;
	uint32_t ipaddr;

	int idtype;
	uint32_t steamId;
	auth_key_kind authKeyKind;
	int authKeyLen;
	char authKey[MAX_AUTHKEY_LEN + 1];
};

void MurmurHash3_x86_32(const void *key, int len, uint32_t seed, void *out);

client_auth_kind Reunion_Authorize_Client(authdata_t *authdata);
uint32_t Reunion_ProcessAuth(authdata_t *authdata, client_auth_kind authkind);
uint32_t Reunion_SteamByIp(uint32_t ip);
int Reunion_GetCidForAuthKind(client_auth_kind kind);
