#pragma once

#define MAX_MASTER_SERVERS 128

struct master_entry_t
{
	char addr[256];
};

struct master_list_t
{
	master_entry_t entries[MAX_MASTER_SERVERS];
	int count;
};

bool vdf_parse_master_servers(const char *filepath, master_list_t *out);
