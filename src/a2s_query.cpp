#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "a2s_query.h"

static const uint8_t A2S_INFO_REQUEST[] = {
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

static bool parse_a2s_response(const uint8_t *data, int len, a2s_server_info_t *out)
{
	if (len < 6) return false;
	if (data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF)
		return false;

	if (data[4] == 0x41)
	{
		return false;
	}

	if (data[4] != 0x49) return false;

	int pos = 6;

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

	if (pos + 2 > len) return false;
	out->password = data[pos++];
	out->secure = data[pos++];

	read_string(data, len, &pos, out->version, sizeof(out->version));

	out->valid = true;
	return true;
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

	DWORD start = GetTickCount();

	if (sendto(sock, (const char *)A2S_INFO_REQUEST, sizeof(A2S_INFO_REQUEST), 0,
		(struct sockaddr *)&dest, sizeof(dest)) == SOCKET_ERROR)
	{
		closesocket(sock);
		return false;
	}

	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);

	struct timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;

	if (select((int)sock + 1, &readfds, NULL, NULL, &tv) <= 0)
	{
		closesocket(sock);
		return false;
	}

	uint8_t buf[2048];
	struct sockaddr_in from;
	int fromlen = sizeof(from);
	int recv_len = recvfrom(sock, (char *)buf, sizeof(buf), 0,
		(struct sockaddr *)&from, &fromlen);

	DWORD elapsed = GetTickCount() - start;
	closesocket(sock);

	if (recv_len <= 0) return false;

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

		for (int i = 0; i < batch; i++)
		{
			int idx = base + i;
			memset(&results[idx], 0, sizeof(results[idx]));

			socks[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (socks[i] == INVALID_SOCKET) continue;

			struct sockaddr_in dest;
			memset(&dest, 0, sizeof(dest));
			dest.sin_family = AF_INET;
			dest.sin_addr.s_addr = ips[idx];
			dest.sin_port = ports[idx];

			starts[i] = GetTickCount();
			sendto(socks[i], (const char *)A2S_INFO_REQUEST, sizeof(A2S_INFO_REQUEST), 0,
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
