#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "vdf_parser.h"
#include "utils.h"

static const char *skip_whitespace(const char *p)
{
	while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
		p++;
	return p;
}

static const char *read_quoted_string(const char *p, char *out, int out_size)
{
	if (*p != '"') return NULL;
	p++;
	int i = 0;
	while (*p && *p != '"' && i < out_size - 1)
		out[i++] = *p++;
	out[i] = '\0';
	if (*p == '"') p++;
	return p;
}

static const char *skip_block(const char *p)
{
	int depth = 1;
	while (*p && depth > 0)
	{
		if (*p == '{') depth++;
		else if (*p == '}') depth--;
		p++;
	}
	return p;
}

static const char *parse_entries(const char *p, master_list_t *out, int depth)
{
	while (1)
	{
		p = skip_whitespace(p);
		if (!*p || *p == '}') break;

		char key[256];
		p = read_quoted_string(p, key, sizeof(key));
		if (!p) break;

		p = skip_whitespace(p);

		if (*p == '{')
		{
			p++;
			if (depth < 3)
			{
				p = parse_entries(p, out, depth + 1);
			}
			else
			{
				p = skip_block(p);
			}
			p = skip_whitespace(p);
			if (*p == '}') p++;
		}
		else if (*p == '"')
		{
			char value[256];
			p = read_quoted_string(p, value, sizeof(value));
			if (!p) break;

			if (!stricmp(key, "addr") && value[0] && out->count < MAX_MASTER_SERVERS)
			{
				strncpy(out->entries[out->count].addr, value, sizeof(out->entries[0].addr) - 1);
				out->entries[out->count].addr[sizeof(out->entries[0].addr) - 1] = '\0';
				out->count++;
			}
		}
	}

	return p;
}

bool vdf_parse_master_servers(const char *filepath, master_list_t *out)
{
	memset(out, 0, sizeof(*out));

	FILE *f = fopen(filepath, "r");
	if (!f) return false;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 1024 * 1024)
	{
		fclose(f);
		return false;
	}

	char *buf = new char[size + 1];
	size_t rd = fread(buf, 1, size, f);
	fclose(f);
	buf[rd] = '\0';

	const char *p = buf;
	p = skip_whitespace(p);

	char root_key[256];
	p = read_quoted_string(p, root_key, sizeof(root_key));
	if (p)
	{
		p = skip_whitespace(p);
		if (*p == '{')
		{
			p++;
			parse_entries(p, out, 0);
		}
	}

	delete[] buf;
	return out->count > 0;
}

bool vdf_write_master_server(const char *filepath, const char *addr)
{
	char dir[512];
	strncpy(dir, filepath, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char *slash = strrchr(dir, '\\');
	if (!slash) slash = strrchr(dir, '/');
	if (slash)
	{
		*slash = '\0';
		CreateDirectoryA(dir, NULL);
	}

	FILE *f = fopen(filepath, "w");
	if (!f) return false;
	fprintf(f,
		"\"MasterServers\"\n"
		"{\n"
		"\t\"hl1\"\n"
		"\t{\n"
		"\t\t\"0\"\n"
		"\t\t{\n"
		"\t\t\t\"addr\"\t\"%s\"\n"
		"\t\t}\n"
		"\t}\n"
		"}\n", addr);
	fclose(f);
	return true;
}
