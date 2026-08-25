#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "a2s_query.h"

const uint8_t A2S_INFO_REQUEST[] = {
	0xFF, 0xFF, 0xFF, 0xFF,
	0x54,
	'S','o','u','r','c','e',' ','E','n','g','i','n','e',' ','Q','u','e','r','y', 0x00
};

static const char *read_string(const uint8_t *data, int len, int *pos, char *out, int out_size)
{
	int i = 0;
	while (*pos < len && data[*pos] != 0 && i < out_size - 1)
	{
		out[i++] = (char)data[*pos];
		(*pos)++;
	}
	out[i] = '\0';
	if (*pos < len) (*pos)++;
	return out;
}

static void skip_string(const uint8_t *data, int len, int *pos)
{
	while (*pos < len && data[*pos] != 0) (*pos)++;
	if (*pos < len) (*pos)++;
}

/* Reads a trailing field a short reply is allowed to omit. Servers that stop
   early still parse, with the field left at its default, rather than the whole
   reply being discarded. */
static uint8_t read_u8_or(const uint8_t *data, int len, int *pos, uint8_t dflt)
{
	if (*pos >= len) return dflt;
	return data[(*pos)++];
}

bool a2s_is_challenge(const uint8_t *data, int len, uint32_t *out_challenge)
{
	if (len < 9) return false;
	if (data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF)
		return false;
	if (data[4] != 0x41) return false;

	if (out_challenge)
		memcpy(out_challenge, data + 5, 4);
	return true;
}

int a2s_build_info_request(uint8_t *out, int out_size, const uint32_t *challenge)
{
	int len = (int)sizeof(A2S_INFO_REQUEST);
	if (out_size < len) return 0;

	memcpy(out, A2S_INFO_REQUEST, len);

	if (challenge)
	{
		if (out_size < len + 4) return len;
		memcpy(out + len, challenge, 4);
		len += 4;
	}
	return len;
}

static bool parse_a2s_source_info(const uint8_t *data, int len, int pos, a2s_server_info_t *out)
{
	if (pos >= len) return false;
	out->protocol = data[pos++];

	read_string(data, len, &pos, out->name, sizeof(out->name));
	read_string(data, len, &pos, out->map, sizeof(out->map));
	read_string(data, len, &pos, out->gamedir, sizeof(out->gamedir));
	read_string(data, len, &pos, out->gamedesc, sizeof(out->gamedesc));

	if (pos + 7 > len) return false;

	out->appid = data[pos] | (data[pos + 1] << 8);
	pos += 2;
	out->players = data[pos++];
	out->max_players = data[pos++];
	out->bots = data[pos++];
	out->type = (char)data[pos++];
	out->os = (char)data[pos++];

	if (pos >= len) return false;
	out->password = data[pos++];

	/* VAC and the version string are trailing fields a short reply may omit. */
	out->secure = read_u8_or(data, len, &pos, 0);

	read_string(data, len, &pos, out->version, sizeof(out->version));

	out->valid = true;
	return true;
}

/* Legacy GoldSrc info reply ('m'): leading address string, no AppID, and the
   optional mod block sits between the visibility and VAC bytes. */
static bool parse_a2s_goldsrc_info(const uint8_t *data, int len, int pos, a2s_server_info_t *out)
{
	skip_string(data, len, &pos);

	read_string(data, len, &pos, out->name, sizeof(out->name));
	read_string(data, len, &pos, out->map, sizeof(out->map));
	read_string(data, len, &pos, out->gamedir, sizeof(out->gamedir));
	read_string(data, len, &pos, out->gamedesc, sizeof(out->gamedesc));

	if (pos + 2 > len) return false;

	out->players = data[pos++];
	out->max_players = data[pos++];

	/* Everything past max_players is a trailing field a short reply may omit.
	   The defaults match what a listing shows for a server that never sent them. */
	out->protocol = read_u8_or(data, len, &pos, 0);
	out->type = (char)read_u8_or(data, len, &pos, 'd');
	out->os = (char)read_u8_or(data, len, &pos, 'l');
	out->password = read_u8_or(data, len, &pos, 0);

	uint8_t is_mod = read_u8_or(data, len, &pos, 0);
	if (is_mod == 1)
	{
		skip_string(data, len, &pos);	/* mod website */
		skip_string(data, len, &pos);	/* mod download url */
		pos += 1;						/* NUL filler */
		pos += 4;						/* mod version */
		pos += 4;						/* mod size */
		pos += 1;						/* server-side only */
		pos += 1;						/* custom client dll */
		if (pos > len) pos = len;		/* truncated block: fall back to defaults */
	}

	out->secure = read_u8_or(data, len, &pos, 0);
	out->bots = read_u8_or(data, len, &pos, 0);

	out->valid = true;
	return true;
}

bool parse_a2s_response(const uint8_t *data, int len, a2s_server_info_t *out)
{
	if (len < 6) return false;
	if (data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF)
		return false;

	if (data[4] == 0x49)
		return parse_a2s_source_info(data, len, 5, out);

	if (data[4] == 0x6D)
		return parse_a2s_goldsrc_info(data, len, 5, out);

	return false;
}

bool a2s_query_server(uint32_t ip_net, uint16_t port_net, a2s_server_info_t *out, int timeout_ms)
{
	memset(out, 0, sizeof(*out));

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) return false;

	struct sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = ip_net;
	dest.sin_port = port_net;

	uint8_t req[A2S_INFO_REQUEST_MAX];
	int req_len = a2s_build_info_request(req, sizeof(req), NULL);

	uint8_t buf[2048];
	DWORD deadline = GetTickCount() + timeout_ms;
	DWORD start = 0;
	DWORD elapsed = 0;
	int recv_len = 0;
	bool challenged = false;

	/* One extra round-trip is allowed for the A2S_SERVERQUERY_GETCHALLENGE
	   reply that ReHLDS and post-2020 Valve builds send first. */
	for (int attempt = 0; attempt < 2; attempt++)
	{
		start = GetTickCount();

		if (sendto(sock, (const char *)req, req_len, 0,
			(struct sockaddr *)&dest, sizeof(dest)) == SOCKET_ERROR)
		{
			closesocket(sock);
			return false;
		}

		DWORD now = GetTickCount();
		if (now >= deadline)
		{
			closesocket(sock);
			return false;
		}

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);

		DWORD remain = deadline - now;
		struct timeval tv;
		tv.tv_sec = remain / 1000;
		tv.tv_usec = (remain % 1000) * 1000;

		if (select((int)sock + 1, &readfds, NULL, NULL, &tv) <= 0)
		{
			closesocket(sock);
			return false;
		}

		struct sockaddr_in from;
		int fromlen = sizeof(from);
		recv_len = recvfrom(sock, (char *)buf, sizeof(buf), 0,
			(struct sockaddr *)&from, &fromlen);

		elapsed = GetTickCount() - start;

		if (recv_len <= 0)
		{
			closesocket(sock);
			return false;
		}

		uint32_t challenge;
		if (challenged || !a2s_is_challenge(buf, recv_len, &challenge))
			break;

		challenged = true;
		req_len = a2s_build_info_request(req, sizeof(req), &challenge);
	}

	closesocket(sock);

	if (!parse_a2s_response(buf, recv_len, out)) return false;

	out->ip = ip_net;
	out->port = port_net;
	out->ping_ms = (int)elapsed;
	return true;
}

int a2s_query_batch(uint32_t *ips, uint16_t *ports, int count,
	a2s_server_info_t *results, int timeout_ms)
{
	if (count <= 0) return 0;

	int max_batch = 64;
	int total_valid = 0;

	for (int base = 0; base < count; base += max_batch)
	{
		int batch = count - base;
		if (batch > max_batch) batch = max_batch;

		SOCKET socks[64];
		DWORD starts[64];
		bool challenged[64];

		for (int i = 0; i < batch; i++)
		{
			int idx = base + i;
			memset(&results[idx], 0, sizeof(results[idx]));
			challenged[i] = false;

			socks[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (socks[i] == INVALID_SOCKET) continue;

			struct sockaddr_in dest;
			memset(&dest, 0, sizeof(dest));
			dest.sin_family = AF_INET;
			dest.sin_addr.s_addr = ips[idx];
			dest.sin_port = ports[idx];

			uint8_t req[A2S_INFO_REQUEST_MAX];
			int req_len = a2s_build_info_request(req, sizeof(req), NULL);

			starts[i] = GetTickCount();
			sendto(socks[i], (const char *)req, req_len, 0,
				(struct sockaddr *)&dest, sizeof(dest));
		}

		DWORD deadline = GetTickCount() + timeout_ms;

		for (int done = 0; done < batch; )
		{
			DWORD now = GetTickCount();
			if (now >= deadline) break;

			fd_set readfds;
			FD_ZERO(&readfds);
			SOCKET max_sock = 0;
			int active = 0;

			for (int i = 0; i < batch; i++)
			{
				if (socks[i] == INVALID_SOCKET) continue;
				FD_SET(socks[i], &readfds);
				if (socks[i] > max_sock) max_sock = socks[i];
				active++;
			}

			if (active == 0) break;

			struct timeval tv;
			DWORD remain = deadline - now;
			tv.tv_sec = remain / 1000;
			tv.tv_usec = (remain % 1000) * 1000;

			int sel = select((int)max_sock + 1, &readfds, NULL, NULL, &tv);
			if (sel <= 0) break;

			for (int i = 0; i < batch; i++)
			{
				if (socks[i] == INVALID_SOCKET) continue;
				if (!FD_ISSET(socks[i], &readfds)) continue;

				int idx = base + i;
				uint8_t buf[2048];
				struct sockaddr_in from;
				int fromlen = sizeof(from);
				int recv_len = recvfrom(socks[i], (char *)buf, sizeof(buf), 0,
					(struct sockaddr *)&from, &fromlen);

				DWORD elapsed = GetTickCount() - starts[i];

				uint32_t challenge;
				if (recv_len > 0 && !challenged[i] &&
					a2s_is_challenge(buf, recv_len, &challenge))
				{
					uint8_t req[A2S_INFO_REQUEST_MAX];
					int req_len = a2s_build_info_request(req, sizeof(req), &challenge);

					struct sockaddr_in dest;
					memset(&dest, 0, sizeof(dest));
					dest.sin_family = AF_INET;
					dest.sin_addr.s_addr = ips[idx];
					dest.sin_port = ports[idx];

					challenged[i] = true;
					starts[i] = GetTickCount();
					sendto(socks[i], (const char *)req, req_len, 0,
						(struct sockaddr *)&dest, sizeof(dest));
					continue;
				}

				if (recv_len > 0 && parse_a2s_response(buf, recv_len, &results[idx]))
				{
					results[idx].ip = ips[idx];
					results[idx].port = ports[idx];
					results[idx].ping_ms = (int)elapsed;
					total_valid++;
				}

				closesocket(socks[i]);
				socks[i] = INVALID_SOCKET;
				done++;
			}
		}

		for (int i = 0; i < batch; i++)
		{
			if (socks[i] != INVALID_SOCKET)
				closesocket(socks[i]);
		}
	}

	return total_valid;
}
