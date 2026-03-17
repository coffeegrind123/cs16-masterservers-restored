#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "utils.h"

char g_PluginDir[512];

void reverse(char *str, int length)
{
	int start = 0;
	int end = length - 1;
	while (start < end)
	{
		char temp = str[start];
		str[start] = str[end];
		str[end] = temp;
		start++;
		end--;
	}
}

char *__itoa(int num, char *str)
{
	int i = 0;
	bool isNeg = false;

	if (num == 0)
	{
		str[i++] = '0';
		str[i] = '\0';
		return str;
	}

	if (num < 0)
	{
		if (num == (-2147483647 - 1))
		{
			strcpy(str, "-2147483648");
			return str;
		}
		isNeg = true;
		num = -num;
	}

	while (num != 0)
	{
		int rem = num % 10;
		str[i++] = (char)(rem + '0');
		num /= 10;
	}

	if (isNeg)
		str[i++] = '-';

	str[i] = '\0';
	reverse(str, i);
	return str;
}

void trimbuf(char *buf)
{
	char *start = buf;
	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
		start++;

	if (start != buf)
		memmove(buf, start, strlen(start) + 1);

	int len = (int)strlen(buf);
	while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' || buf[len - 1] == '\r' || buf[len - 1] == '\n'))
		buf[--len] = '\0';
}

uint32_t host2ip(const char *hostname)
{
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(hostname, NULL, &hints, &res) != 0)
		return 0;

	uint32_t ip = 0;
	if (res)
	{
		struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
		ip = addr->sin_addr.s_addr;
		freeaddrinfo(res);
	}
	return ip;
}

void find_plugin_dir()
{
	char path[MAX_PATH];
	HMODULE hm = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&find_plugin_dir, &hm);
	GetModuleFileNameA(hm, path, sizeof(path));
	char *slash = strrchr(path, '\\');
	if (!slash) slash = strrchr(path, '/');
	if (slash) *slash = '\0';
	strncpy(g_PluginDir, path, sizeof(g_PluginDir) - 1);
	g_PluginDir[sizeof(g_PluginDir) - 1] = '\0';
}
