#pragma once
#include <stdint.h>

#define MAX_CACHED_SERVERS 16384

#pragma pack(push, 1)
struct cached_server_t
{
	uint32_t ip;
	uint16_t port;
};
#pragma pack(pop)

struct server_cache_t
{
	cached_server_t servers[MAX_CACHED_SERVERS];
	int count;
};

bool cache_load(const char *filepath, server_cache_t *out);
bool cache_save(const char *filepath, const server_cache_t *cache);
