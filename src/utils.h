#pragma once
#include <stdint.h>
#include <string.h>

#ifndef _WIN32
#define stricmp strcasecmp
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#define PORT_MASTER 27010
#define PORT_SERVER 27015
#define HLDS_APPID 10
#define HLDS_APPVERSION "1.1.2.7/Stdio"
#define HEARTBEAT_TIME 300.0
#define TAG "RMAS"

extern char g_PluginDir[512];

void reverse(char *str, int length);
char *__itoa(int num, char *str);
void trimbuf(char *buf);
uint32_t host2ip(const char *hostname);
void ServerPrintf(const char *fmt, ...);
void find_plugin_dir();
