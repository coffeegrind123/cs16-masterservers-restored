#pragma once
#include <stdint.h>

#pragma pack(push, 1)
struct a2s_server_info_t
{
	char name[256];
	char map[256];
	char gamedir[256];
	char gamedesc[256];
	uint16_t appid;
	uint8_t players;
	uint8_t max_players;
	uint8_t bots;
	char type;
	char os;
	uint8_t password;
	uint8_t secure;
	char version[64];
	uint32_t ip;
	uint16_t port;
	int ping_ms;
	bool valid;
};
#pragma pack(pop)

bool a2s_query_server(uint32_t ip_net, uint16_t port_net, a2s_server_info_t *out, int timeout_ms = 2000);

int a2s_query_batch(uint32_t *ips, uint16_t *ports, int count,
	a2s_server_info_t *results, int timeout_ms = 3000);
