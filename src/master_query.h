#pragma once
#include <stdint.h>

#define MAX_QUERY_SERVERS 16384
#define QUERY_TIMEOUT_MS 5000
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
