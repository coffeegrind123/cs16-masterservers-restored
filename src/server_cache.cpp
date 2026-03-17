#include <stdio.h>
#include <string.h>
#include "server_cache.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#else
#include <sys/stat.h>
#define mkdir_p(d) mkdir(d, 0755)
#endif

static void ensure_parent_dir(const char *filepath)
{
	char dir[512];
	strncpy(dir, filepath, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';

	char *slash = strrchr(dir, '/');
#ifdef _WIN32
	if (!slash) slash = strrchr(dir, '\\');
#endif
	if (slash)
	{
		*slash = '\0';
		mkdir_p(dir);
	}
}

bool cache_load(const char *filepath, server_cache_t *out)
{
	memset(out, 0, sizeof(*out));

	FILE *f = fopen(filepath, "rb");
	if (!f) return false;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	int count = (int)(size / sizeof(cached_server_t));
	if (count <= 0)
	{
		fclose(f);
		return false;
	}

	if (count > MAX_CACHED_SERVERS)
		count = MAX_CACHED_SERVERS;

	size_t rd = fread(out->servers, sizeof(cached_server_t), count, f);
	fclose(f);

	out->count = (int)rd;
	return out->count > 0;
}

bool cache_save(const char *filepath, const server_cache_t *cache)
{
	if (!cache || cache->count <= 0) return false;

	ensure_parent_dir(filepath);

	FILE *f = fopen(filepath, "wb");
	if (!f) return false;

	size_t written = fwrite(cache->servers, sizeof(cached_server_t), cache->count, f);
	fclose(f);

	return (int)written == cache->count;
}
