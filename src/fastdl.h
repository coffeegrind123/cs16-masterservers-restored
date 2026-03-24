#pragma once
#include <windows.h>

#define FASTDL_DEFAULT_PORT 27015
#define FASTDL_MAX_FILE_SIZE (64 * 1024 * 1024)
#define FASTDL_MAX_CONNECTIONS 4

bool FastDL_Start(const char *halfLifeDir, const char *gameDir, const char *serverIP, int port);
void FastDL_Stop();
bool FastDL_GetPublicIP(char *out, int outSize, const char *masterAddr);
