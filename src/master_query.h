#pragma once
#include <stdint.h>

#define MAX_QUERY_SERVERS 16384
#define QUERY_TIMEOUT_MS 3000
#define QUERY_MAX_RETRIES 30

struct query_server_t
{
	uint32_t ip;
	uint16_t port;
};

struct master_query_result_t
{
	query_server_t servers[MAX_QUERY_SERVERS];
	int count;
};

bool master_query_servers(const char *master_addr, master_query_result_t *result);
bool master_validate_server(const char *master_addr);
struct heartbeat_info_t
{
	char hostname[256];
	char map[256];
	char gamedir[64];
	char version[64];
	int protocol;
	int players;
	int max_players;
	int bots;
	int password;
	int secure;
	int lan;
	int is_dedicated;
};

bool master_send_heartbeat(const char *master_addr, const heartbeat_info_t *info);
