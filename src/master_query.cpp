#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "master_query.h"
#include "utils.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSE_SOCKET close
#endif

static bool build_query_packet(uint8_t *buf, int *len, int max_len,
	uint8_t region, const char *last_addr, const char *filter)
{
	int pos = 0;
	buf[pos++] = 0x31;
	buf[pos++] = region;

	int addr_len = (int)strlen(last_addr) + 1;
	if (pos + addr_len > max_len) return false;
	memcpy(buf + pos, last_addr, addr_len);
	pos += addr_len;

	int filt_len = (int)strlen(filter) + 1;
	if (pos + filt_len > max_len) return false;
	memcpy(buf + pos, filter, filt_len);
	pos += filt_len;

	*len = pos;
	return true;
}

static bool parse_response(const uint8_t *data, int len, master_query_result_t *result,
	uint32_t *last_ip, uint16_t *last_port)
{
	if (len < 6) return false;

	if (data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF)
		return false;

	if (data[4] != 0x66 || data[5] != 0x0A)
		return false;

	int pos = 6;
	bool got_terminator = false;

	while (pos + 6 <= len && result->count < MAX_QUERY_SERVERS)
	{
		uint32_t ip = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
		uint16_t port = (data[pos + 4] << 8) | data[pos + 5];
		pos += 6;

		if (ip == 0 && port == 0)
		{
			got_terminator = true;
			break;
		}

		uint32_t ip_net = htonl(ip);
		uint16_t port_net = htons(port);

		result->servers[result->count].ip = ip_net;
		result->servers[result->count].port = port_net;
		result->count++;

		*last_ip = ip;
		*last_port = port;
	}

	return true;
}

extern void RealMasterLog(const char *fmt, ...);

bool master_query_servers(const char *master_addr, master_query_result_t *result)
{
	memset(result, 0, sizeof(*result));

	char hostname[256];
	unsigned short port = 0;
	hostname[0] = 0;

	if (sscanf(master_addr, "%255[-.0-9A-Za-z_]:%hu", hostname, &port) < 1)
	{
		RealMasterLog("  master_query: sscanf failed for '%s'", master_addr);
		return false;
	}

	if (!port) port = PORT_MASTER;

	uint32_t ip = host2ip(hostname);
	RealMasterLog("  master_query: host2ip('%s') = 0x%08X", hostname, ip);
	if (ip == 0 || ip == (uint32_t)-1)
	{
		RealMasterLog("  master_query: DNS resolution failed for '%s'", hostname);
		return false;
	}

	struct sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = ip;
	dest.sin_port = htons(port);

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET)
		return false;

	char last_addr[32] = "0.0.0.0:0";
	uint32_t last_ip = 0;
	uint16_t last_port_val = 0;
	char filter[256];
	snprintf(filter, sizeof(filter), "\\gamedir\\cstrike");

	int retries = 0;
	bool done = false;

	while (!done && retries < QUERY_MAX_RETRIES)
	{
		uint8_t pkt[512];
		int pkt_len = 0;

		if (!build_query_packet(pkt, &pkt_len, sizeof(pkt), 0xFF, last_addr, filter))
			break;

		int sr = sendto(sock, (const char *)pkt, pkt_len, 0,
			(struct sockaddr *)&dest, sizeof(dest));
		if (sr == SOCKET_ERROR)
		{
			RealMasterLog("  master_query: sendto failed, WSAError=%d", WSAGetLastError());
			break;
		}
		RealMasterLog("  master_query: sent %d bytes to %u.%u.%u.%u:%u, waiting...",
			pkt_len, (ip)&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF, port);

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);

		struct timeval tv;
		tv.tv_sec = QUERY_TIMEOUT_MS / 1000;
		tv.tv_usec = (QUERY_TIMEOUT_MS % 1000) * 1000;

		int sel = select((int)sock + 1, &readfds, NULL, NULL, &tv);
		if (sel <= 0)
		{
			RealMasterLog("  master_query: select timeout/error (sel=%d)", sel);
			break;
		}

		uint8_t recv_buf[4096];
		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);
		int recv_len = recvfrom(sock, (char *)recv_buf, sizeof(recv_buf), 0,
			(struct sockaddr *)&from, &fromlen);

		if (recv_len <= 0)
			break;

		uint32_t prev_last_ip = last_ip;
		uint16_t prev_last_port = last_port_val;
		int prev_count = result->count;

		if (!parse_response(recv_buf, recv_len, result, &last_ip, &last_port_val))
			break;

		if (result->count == prev_count)
		{
			done = true;
			break;
		}

		if (last_ip == prev_last_ip && last_port_val == prev_last_port && result->count > prev_count)
		{
			done = true;
			break;
		}

		if (last_ip == 0 && last_port_val == 0)
		{
			done = true;
			break;
		}

		snprintf(last_addr, sizeof(last_addr), "%u.%u.%u.%u:%u",
			(last_ip >> 24) & 0xFF, (last_ip >> 16) & 0xFF,
			(last_ip >> 8) & 0xFF, last_ip & 0xFF, last_port_val);

		retries++;
	}

	CLOSE_SOCKET(sock);
	return result->count > 0;
}

bool master_validate_server(const char *master_addr)
{
	char hostname[256];
	strncpy(hostname, master_addr, sizeof(hostname) - 1);
	hostname[sizeof(hostname) - 1] = '\0';

	uint16_t port = PORT_MASTER;
	char *colon = strrchr(hostname, ':');
	if (colon)
	{
		*colon = '\0';
		int p = atoi(colon + 1);
		if (p > 0 && p < 65536) port = (uint16_t)p;
	}

	uint32_t ip = host2ip(hostname);
	if (ip == 0 || ip == (uint32_t)-1) return false;

	struct sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = ip;
	dest.sin_port = htons(port);

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) return false;

	uint8_t pkt[512];
	int pkt_len = 0;
	if (!build_query_packet(pkt, &pkt_len, sizeof(pkt), 0xFF, "0.0.0.0:0", "\\gamedir\\cstrike"))
	{
		CLOSE_SOCKET(sock);
		return false;
	}

	sendto(sock, (const char *)pkt, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));

	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);

	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;

	int sel = select((int)sock + 1, &readfds, NULL, NULL, &tv);
	CLOSE_SOCKET(sock);
	return sel > 0;
}

bool master_send_heartbeat(const char *master_addr, const heartbeat_info_t *info, SOCKET use_socket)
{
	char hostname[256];
	strncpy(hostname, master_addr, sizeof(hostname) - 1);
	hostname[sizeof(hostname) - 1] = '\0';

	uint16_t port = PORT_MASTER;
	char *colon = strrchr(hostname, ':');
	if (colon)
	{
		*colon = '\0';
		int p = atoi(colon + 1);
		if (p > 0 && p < 65536) port = (uint16_t)p;
	}

	uint32_t master_ip = host2ip(hostname);
	if (master_ip == 0 || master_ip == (uint32_t)-1) return false;

	struct sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = master_ip;
	dest.sin_port = htons(port);

	bool own_socket = (use_socket == INVALID_SOCKET);
	SOCKET sock = own_socket ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : use_socket;
	if (sock == INVALID_SOCKET) return false;

	static const uint8_t challenge_req[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x71 };
	sendto(sock, (const char *)challenge_req, sizeof(challenge_req), 0,
		(struct sockaddr *)&dest, sizeof(dest));

	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	struct timeval tv = { 2, 0 };

	if (select((int)sock + 1, &readfds, NULL, NULL, &tv) <= 0)
	{
		if (own_socket) CLOSE_SOCKET(sock);
		return false;
	}

	uint8_t resp[64];
	int resp_len = recvfrom(sock, (char *)resp, sizeof(resp), 0, NULL, NULL);
	if (resp_len < 10 || resp[4] != 0x73)
	{
		if (own_socket) CLOSE_SOCKET(sock);
		return false;
	}

	uint32_t challenge;
	memcpy(&challenge, resp + 6, 4);

	const char *gamedir = (info && info->gamedir[0]) ? info->gamedir : "cstrike";
	const char *map = (info && info->map[0]) ? info->map : "unknown";
	const char *version = (info && info->version[0]) ? info->version : "1.1.2.7/Stdio";
	int protocol = (info && info->protocol > 0) ? info->protocol : 48;
	char stype = (info && info->is_dedicated) ? 'd' : 'l';

	char heartbeat[1024];
	snprintf(heartbeat, sizeof(heartbeat),
		"0\n\\protocol\\%d\\challenge\\%u\\players\\%d\\max\\%d\\bots\\%d"
		"\\gamedir\\%s\\map\\%s\\type\\%c\\password\\%d\\os\\w"
		"\\secure\\%d\\lan\\%d\\version\\%s\\region\\255\\product\\%s\n",
		protocol, challenge,
		info ? info->players : 0,
		info ? info->max_players : 16,
		info ? info->bots : 0,
		gamedir, map, stype,
		info ? info->password : 0,
		info ? info->secure : 0,
		info ? info->lan : 0,
		version, gamedir);

	sendto(sock, heartbeat, (int)strlen(heartbeat), 0,
		(struct sockaddr *)&dest, sizeof(dest));

	RealMasterLog("Heartbeat sent to %s:%d (challenge=%u, map=%s, players=%d/%d, type=%c)",
		hostname, port, challenge, map,
		info ? info->players : 0, info ? info->max_players : 16, stype);

	if (own_socket) CLOSE_SOCKET(sock);
	return true;
}
