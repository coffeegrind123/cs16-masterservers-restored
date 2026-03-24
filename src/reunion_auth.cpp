#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "reunion_auth.h"
#include "reunion.h"

extern void RealMasterLog(const char *fmt, ...);

static inline uint32_t rotl32(uint32_t x, int8_t r) { return (x << r) | (x >> (32 - r)); }

void MurmurHash3_x86_32(const void *key, int len, uint32_t seed, void *out)
{
	const uint8_t *data = (const uint8_t *)key;
	const int nblocks = len / 4;
	uint32_t h1 = seed;
	uint32_t c1 = 0xcc9e2d51;
	uint32_t c2 = 0x1b873593;

	const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);
	for (int i = -nblocks; i; i++)
	{
		uint32_t k1 = blocks[i];
		k1 *= c1; k1 = rotl32(k1, 15); k1 *= c2;
		h1 ^= k1; h1 = rotl32(h1, 13); h1 = h1 * 5 + 0xe6546b64;
	}

	const uint8_t *tail = (const uint8_t *)(data + nblocks * 4);
	uint32_t k1 = 0;
	switch (len & 3)
	{
	case 3: k1 ^= tail[2] << 16;
	case 2: k1 ^= tail[1] << 8;
	case 1: k1 ^= tail[0];
		k1 *= c1; k1 = rotl32(k1, 15); k1 *= c2; h1 ^= k1;
	}

	h1 ^= len;
	h1 ^= h1 >> 16; h1 *= 0x85ebca6b;
	h1 ^= h1 >> 13; h1 *= 0xc2b2ae35;
	h1 ^= h1 >> 16;

	*(uint32_t *)out = h1;
}

static uint32_t revHash(const char *str, int n = -1)
{
	uint32_t hash = 0x4E67C6A7;
	for (int cc = *str; cc && n != 0; cc = *++str, --n)
		hash ^= (hash >> 2) + cc + 32 * hash;
	return hash;
}

static bool IsHddsnNumber(const char *str)
{
	if (!str || !str[0]) return false;
	for (const char *p = str; *p; p++)
	{
		if ((*p < '0' || *p > '9') && *p != '-' && *p != '_')
			return false;
	}
	return true;
}

static void RevEmuFinishAuth(authdata_t *authdata, const char *authStr, int maxLen)
{
	authdata->authKeyKind = IsHddsnNumber(authStr) ? AK_HDDSN : AK_VOLUMEID;
	authdata->idtype = 2;

	int len = (int)strlen(authStr);
	if (len > maxLen) len = maxLen;
	authdata->authKeyLen = len;

	if (len > 0)
	{
		memcpy(authdata->authKey, authStr, len);
		authdata->authKey[len] = '\0';
		authdata->steamId = revHash(authdata->authKey) << 1;
	}
	else
	{
		authdata->steamId = STEAM_ID_PENDING;
		authdata->authKeyKind = AK_OTHER;
		authdata->authKeyLen = 0;
	}
}

static char *InfoValueForKey(char *info, const char *key)
{
	if (!info || !key) return NULL;
	int keyLen = (int)strlen(key);
	char *p = info;
	while (*p)
	{
		if (*p == '\\') p++;
		char *keyStart = p;
		while (*p && *p != '\\') p++;
		int thisKeyLen = (int)(p - keyStart);
		if (*p == '\\') p++;
		char *valStart = p;
		while (*p && *p != '\\') p++;
		if (thisKeyLen == keyLen && strnicmp(keyStart, key, keyLen) == 0)
		{
			static char val[256];
			int valLen = (int)(p - valStart);
			if (valLen > 255) valLen = 255;
			memcpy(val, valStart, valLen);
			val[valLen] = '\0';
			return val;
		}
	}
	return NULL;
}

static client_auth_kind Auth_HLTV(authdata_t *ad)
{
	if (!ad->userinfo) return CA_UNKNOWN;
	char *hltv = InfoValueForKey(ad->userinfo, "*hltv");
	if (!hltv || !hltv[0]) return CA_UNKNOWN;

	ad->idtype = 2;
	ad->steamId = STEAM_ID_LAN;
	ad->authKeyKind = AK_OTHER;
	ad->authKeyLen = 0;
	return CA_HLTV;
}

static client_auth_kind Auth_SxEI(authdata_t *ad)
{
	if (!g_reunionConfig.EnableSXEIdGeneration) return CA_UNKNOWN;
	if (!ad->userinfo) return CA_UNKNOWN;

	char *hid = InfoValueForKey(ad->userinfo, "*HID");
	if (!hid || !hid[0]) return CA_UNKNOWN;

	uint32_t sxeId = 0;
	if (sscanf(hid, "%X", &sxeId) != 1) return CA_UNKNOWN;

	ad->idtype = 2;
	if (sxeId == 0)
	{
		ad->steamId = STEAM_ID_PENDING;
		ad->authKeyKind = AK_OTHER;
		ad->authKeyLen = 0;
	}
	else
	{
		ad->steamId = sxeId << 1;
		ad->authKeyKind = AK_SXEID;
		if (g_reunionConfig.AuthVersion >= 3)
		{
			ad->authKeyLen = sizeof(uint32_t);
			*(uint32_t *)ad->authKey = sxeId;
		}
		else
		{
			ad->authKeyLen = (int)strlen(hid);
			if (ad->authKeyLen > MAX_AUTHKEY_LEN) ad->authKeyLen = MAX_AUTHKEY_LEN;
			memcpy(ad->authKey, hid, ad->authKeyLen);
		}
	}
	return CA_SXEI;
}

static client_auth_kind Auth_AVSMP(authdata_t *ad)
{
	if (ad->ticketLen != 28) return CA_UNKNOWN;

	uint32_t *ticket = (uint32_t *)ad->authTicket;
	if (ticket[0] != 0x14) return CA_UNKNOWN;

	uint32_t accId = ticket[3];
	ad->idtype = 2;

	if (accId == STEAM_ID_LAN || accId == 1234)
	{
		ad->steamId = STEAM_ID_PENDING;
		ad->authKeyKind = AK_OTHER;
		ad->authKeyLen = 0;
	}
	else
	{
		ad->steamId = accId;
		ad->authKeyKind = AK_FILEID;
		ad->authKeyLen = sizeof(uint32_t);
		*(uint32_t *)ad->authKey = accId;
	}
	return CA_AVSMP;
}

static client_auth_kind Auth_Setti(authdata_t *ad)
{
	if (ad->ticketLen != 0x300) return CA_UNKNOWN;

	uint32_t *ikey = (uint32_t *)ad->authTicket;
	if (!(ikey[0] == 0xD4CA7F7B || ikey[1] == 0xC7DB6023 ||
		ikey[2] == 0x6D6A2E1F || ikey[5] == 0xB4C43105))
		return CA_UNKNOWN;

	ad->idtype = 2;
	ad->steamId = STEAM_ID_LAN;
	ad->authKeyKind = AK_OTHER;
	ad->authKeyLen = 0;
	return CA_SETTI;
}

static client_auth_kind Auth_RevEmu(authdata_t *ad)
{
	if (ad->ticketLen < 152) return CA_UNKNOWN;

	uint32_t *ticket = (uint32_t *)ad->authTicket;
	if (ticket[0] != 0x4A) return CA_UNKNOWN;
	if ((ticket[2] != REVEMU_SIGNATURE && ticket[2] != REVEMU_SIGNATURE_ALT) ||
		ticket[3] != REVEMU_SIGNATURE2)
		return CA_UNKNOWN;

	char *ticketBuf = (char *)(ad->authTicket + 24);
	char saved = ticketBuf[127];
	ticketBuf[127] = '\0';

	uint32_t hash = revHash(ticketBuf);
	uint32_t accId = ticket[4] & 0xFFFFFFFF;

	if (hash != ticket[1] || (hash << 1) != accId)
	{
		ticketBuf[127] = saved;
		return CA_UNKNOWN;
	}

	RevEmuFinishAuth(ad, ticketBuf, 127);
	ticketBuf[127] = saved;
	return CA_REVEMU;
}

static client_auth_kind Auth_OldRevEmu(authdata_t *ad)
{
	if (ad->ticketLen != 10) return CA_UNKNOWN;

	uint32_t *ticket = (uint32_t *)ad->authTicket;
	if ((uint16_t)(ticket[0]) != 0xFFFF) return CA_UNKNOWN;
	if ((uint16_t)(ticket[2]) != 0) return CA_UNKNOWN;

	uint32_t volumeId = ticket[1] & 0x7FFFFFFF;
	ad->idtype = 2;

	if (volumeId == 0)
	{
		ad->steamId = STEAM_ID_PENDING;
		ad->authKeyKind = AK_OTHER;
		ad->authKeyLen = 0;
	}
	else
	{
		ad->steamId = (volumeId ^ STEAMEMU_HASH_KEY) << 1;
		ad->authKeyKind = AK_VOLUMEID;
		ad->authKeyLen = sizeof(uint32_t);
		*(uint32_t *)ad->authKey = volumeId;
	}
	return CA_OLD_REVEMU;
}

static client_auth_kind Auth_SteamEmu(authdata_t *ad)
{
	if (ad->ticketLen != 0x300) return CA_UNKNOWN;

	uint32_t *ticket = (uint32_t *)ad->authTicket;
	if (ticket[20] != 0xFFFFFFFF) return CA_UNKNOWN;

	uint32_t accId = ticket[21];
	ad->idtype = 2;

	if (accId == 0 || accId == 777)
	{
		ad->steamId = STEAM_ID_PENDING;
		ad->authKeyKind = AK_OTHER;
		ad->authKeyLen = 0;
	}
	else
	{
		uint32_t hash = ticket[21] ^ STEAMEMU_HASH_KEY;
		ad->steamId = hash << 1;
		ad->authKeyKind = AK_VOLUMEID;
		ad->authKeyLen = sizeof(uint32_t);

		if (g_reunionConfig.AuthVersion >= 3)
			*(uint32_t *)ad->authKey = (ticket[21] ^ STEAMEMU_HASH_KEY) & 0x7FFFFFFF;
		else
			*(uint32_t *)ad->authKey = accId;
	}
	return CA_STEAM_EMU;
}

static client_auth_kind Auth_NoSteam47(authdata_t *ad)
{
	if (ad->protocol != 47) return CA_UNKNOWN;
	ad->idtype = 2;
	ad->steamId = STEAM_ID_LAN;
	ad->authKeyKind = AK_OTHER;
	ad->authKeyLen = 0;
	return CA_NO_STEAM_47;
}

static client_auth_kind Auth_NoSteam48(authdata_t *ad)
{
	if (ad->protocol != 48) return CA_UNKNOWN;
	ad->idtype = 2;
	ad->steamId = STEAM_ID_LAN;
	ad->authKeyKind = AK_OTHER;
	ad->authKeyLen = 0;
	return CA_NO_STEAM_48;
}

#ifdef HAS_RIJNDAEL
#include "rijndael.h"
#include "sha2.h"

static client_auth_kind Auth_RevEmu2013(authdata_t *ad)
{
	if (ad->ticketLen < 136) return CA_UNKNOWN;

	uint32_t *ticket = (uint32_t *)ad->authTicket;
	if (ticket[0] != 0x53) return CA_UNKNOWN;
	if ((ticket[2] != REVEMU_SIGNATURE && ticket[2] != REVEMU_SIGNATURE_ALT) ||
		ticket[3] != REVEMU_SIGNATURE2)
		return CA_UNKNOWN;

	char *encKey = (char *)(ad->authTicket + 72);
	char *encData = (char *)(ad->authTicket + 40);
	char *digest = (char *)(ad->authTicket + 104);

	CRijndael crypt1;
	crypt1.MakeKey("_YOU_SERIOUSLY_NEED_TO_GET_LAID_", CRijndael::sm_chain0, 32, 32);
	char decKey[32];
	if (!crypt1.DecryptBlock(encKey, decKey)) return CA_UNKNOWN;

	CRijndael crypt2;
	crypt2.MakeKey(decKey, CRijndael::sm_chain0, 32, 32);
	char decData[33];
	if (!crypt2.DecryptBlock(encData, decData)) return CA_UNKNOWN;
	decData[32] = '\0';

	sha2 sha;
	sha.Init(sha2::enuSHA256);
	sha.Update((const uint8_t *)decData, 32);
	sha.End();
	int shaLen;
	const char *hash = sha.RawHash(shaLen);
	if (memcmp(hash, digest, 32) != 0) return CA_UNKNOWN;

	uint32_t h = revHash(decData);
	uint32_t accId = ticket[4];
	if (h != ticket[1] || (h << 1) != accId) return CA_UNKNOWN;

	RevEmuFinishAuth(ad, decData, 31);
	return CA_REVEMU_2013;
}

static client_auth_kind Auth_SteamClient2009(authdata_t *ad)
{
	if (ad->ticketLen < 120) return CA_UNKNOWN;

	uint32_t *ticket = (uint32_t *)ad->authTicket;
	if (ticket[0] != 0x53) return CA_UNKNOWN;
	if ((ticket[2] != REVEMU_SIGNATURE && ticket[2] != REVEMU_SIGNATURE_ALT) ||
		ticket[3] != REVEMU_SIGNATURE2)
		return CA_UNKNOWN;

	char *encKey = (char *)(ad->authTicket + 56);
	char *encData = (char *)(ad->authTicket + 24);
	char *digest = (char *)(ad->authTicket + 88);

	CRijndael crypt1;
	crypt1.MakeKey("_YOU_SERIOUSLY_NEED_TO_GET_LAID_", CRijndael::sm_chain0, 32, 32);
	char decKey[32];
	if (!crypt1.DecryptBlock(encKey, decKey)) return CA_UNKNOWN;

	CRijndael crypt2;
	crypt2.MakeKey(decKey, CRijndael::sm_chain0, 32, 32);
	char decData[33];
	if (!crypt2.DecryptBlock(encData, decData)) return CA_UNKNOWN;
	decData[32] = '\0';

	sha2 sha;
	sha.Init(sha2::enuSHA256);
	sha.Update((const uint8_t *)decData, 32);
	sha.End();
	int shaLen;
	const char *hash = sha.RawHash(shaLen);
	if (memcmp(hash, digest, 32) != 0) return CA_UNKNOWN;

	uint32_t h = revHash(decData);
	uint32_t accId = ticket[4];
	if (h != ticket[1] || (h << 1) != accId) return CA_UNKNOWN;

	RevEmuFinishAuth(ad, decData, 31);
	return CA_STEAMCLIENT_2009;
}

static void SaltSteamId(authdata_t *ad)
{
	int saltLen = (int)strlen(g_reunionConfig.SteamIdHashSalt);
	if (saltLen == 0) return;

	uint8_t buf[4 + MAX_AUTHKEY_LEN + 64];
	int pos = 0;

	if (g_reunionConfig.AuthVersion < 3)
	{
		memcpy(buf + pos, &ad->steamId, 4);
		pos += 4;
	}
	if (g_reunionConfig.AuthVersion > 1)
	{
		int copyLen = ad->authKeyLen;
		if (copyLen > MAX_AUTHKEY_LEN) copyLen = MAX_AUTHKEY_LEN;
		if (copyLen > 0)
		{
			memcpy(buf + pos, ad->authKey, copyLen);
			pos += copyLen;
		}
	}
	memcpy(buf + pos, g_reunionConfig.SteamIdHashSalt, saltLen);
	pos += saltLen;

	sha2 sha;
	sha.Init(sha2::enuSHA256);
	sha.Update(buf, pos);
	sha.End();
	int shaLen;
	const char *hash = sha.RawHash(shaLen);

	ad->steamId = *(uint32_t *)(hash + 8);
}

#endif

typedef client_auth_kind (*AuthFunc)(authdata_t *);

client_auth_kind Reunion_Authorize_Client(authdata_t *authdata)
{
	AuthFunc chain[] = {
		Auth_HLTV,
		Auth_SxEI,
		Auth_Setti,
		Auth_AVSMP,
#ifdef HAS_RIJNDAEL
		Auth_RevEmu2013,
		Auth_SteamClient2009,
#endif
		Auth_RevEmu,
		Auth_OldRevEmu,
		Auth_SteamEmu,
		Auth_NoSteam47,
		Auth_NoSteam48,
	};

	int count = sizeof(chain) / sizeof(chain[0]);
	for (int i = 0; i < count; i++)
	{
		client_auth_kind kind = chain[i](authdata);
		if (kind != CA_UNKNOWN)
			return kind;
	}
	return CA_UNKNOWN;
}

uint32_t Reunion_SteamByIp(uint32_t ip)
{
	uint32_t accId;
	if (g_reunionConfig.AuthVersion >= 3)
		MurmurHash3_x86_32(&ip, sizeof(ip), IPGEN_KEY, &accId);
	else
		accId = ip ^ IPGEN_KEY;
	return accId;
}

static const char *AuthKindName(client_auth_kind kind)
{
	static const char *names[] = {
		"Unknown", "HLTV", "NoSteam47", "NoSteam48", "Setti",
		"Steam", "SteamPending", "SteamEmu", "OldRevEmu", "RevEmu",
		"SteamClient2009", "RevEmu2013", "AVSMP", "sXeI"
	};
	if (kind >= 0 && kind < CA_MAX) return names[kind];
	return "Unknown";
}

int Reunion_GetCidForAuthKind(client_auth_kind kind)
{
	switch (kind)
	{
	case CA_STEAM: return g_reunionConfig.cid_RealSteam;
	case CA_STEAM_PENDING: return g_reunionConfig.cid_PendingSteam;
	case CA_HLTV: return g_reunionConfig.cid_HLTV;
	case CA_NO_STEAM_47: return g_reunionConfig.cid_NoSteam47;
	case CA_NO_STEAM_48: return g_reunionConfig.cid_NoSteam48;
	case CA_REVEMU:
	case CA_REVEMU_2013: return g_reunionConfig.cid_RevEmu;
	case CA_STEAMCLIENT_2009: return g_reunionConfig.cid_RevEmu;
	case CA_SETTI: return g_reunionConfig.cid_SETTi;
	case CA_STEAM_EMU: return g_reunionConfig.cid_StmEmu;
	case CA_OLD_REVEMU: return g_reunionConfig.cid_RevEmu;
	case CA_AVSMP: return g_reunionConfig.cid_AVSMP;
	case CA_SXEI: return g_reunionConfig.cid_SxEI;
	default: return CI_STEAM_BY_IP;
	}
}

uint32_t Reunion_ProcessAuth(authdata_t *authdata, client_auth_kind authkind)
{
	int cid = Reunion_GetCidForAuthKind(authkind);

	RealMasterLog("Reunion: client authorized as %s, cid=%d, rawId=%u",
		AuthKindName(authkind), cid, authdata->steamId);

	if (cid == CI_DEPRECATED)
	{
		RealMasterLog("Reunion: client type %s is deprecated (cid=5), rejecting", AuthKindName(authkind));
		return 0;
	}

	if (cid == CI_STEAM_BY_IP || cid == CI_VALVE_BY_IP)
	{
		authdata->steamId = Reunion_SteamByIp(authdata->ipaddr);
	}
	else if (authdata->steamId == STEAM_ID_LAN || authdata->steamId == STEAM_ID_PENDING)
	{
		authdata->steamId = Reunion_SteamByIp(authdata->ipaddr);
	}

#ifdef HAS_RIJNDAEL
	if (g_reunionConfig.SteamIdHashSalt[0] && authdata->authKeyLen > 0)
		SaltSteamId(authdata);
#endif

	if (g_reunionConfig.EnableGenPrefix2)
		authdata->steamId = rotl32(authdata->steamId, 1);
	else
		authdata->steamId <<= 1;

	return authdata->steamId;
}
