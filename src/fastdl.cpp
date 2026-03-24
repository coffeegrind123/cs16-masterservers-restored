#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fastdl.h"

extern void RealMasterLog(const char *fmt, ...);

static bool HttpGetPlainText(const char *host, int port, const char *path, char *out, int outSize)
{
	uint32_t ip = 0;
	struct hostent *he = gethostbyname(host);
	if (!he) return false;
	ip = *(uint32_t *)he->h_addr_list[0];

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) return false;

	struct timeval tv = { 3, 0 };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ip;
	addr.sin_port = htons((uint16_t)port);

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		closesocket(sock);
		return false;
	}

	char req[512];
	int reqLen = snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
	send(sock, req, reqLen, 0);

	char buf[1024];
	int total = 0;
	while (total < (int)sizeof(buf) - 1)
	{
		int r = recv(sock, buf + total, sizeof(buf) - 1 - total, 0);
		if (r <= 0) break;
		total += r;
	}
	closesocket(sock);
	buf[total] = '\0';

	if (total < 12 || strncmp(buf, "HTTP/1.", 7) != 0) return false;
	int status = atoi(buf + 9);
	if (status != 200) return false;

	char *body = strstr(buf, "\r\n\r\n");
	if (!body) return false;
	body += 4;

	while (*body == ' ' || *body == '\r' || *body == '\n') body++;
	int len = 0;
	while (body[len] && body[len] != '\r' && body[len] != '\n' && body[len] != ' ' && len < outSize - 1)
		len++;

	if (len < 7 || len > 45) return false;

	memcpy(out, body, len);
	out[len] = '\0';
	return true;
}

bool FastDL_GetPublicIP(char *out, int outSize, const char *masterAddr)
{
	char host[256] = {0};
	int port = 27010;

	if (masterAddr && masterAddr[0])
	{
		strncpy(host, masterAddr, sizeof(host) - 1);
		char *colon = strrchr(host, ':');
		if (colon)
		{
			*colon = '\0';
			int p = atoi(colon + 1);
			if (p > 0 && p < 65536) port = p;
		}
	}

	if (host[0])
	{
		RealMasterLog("FastDL: trying public IP from master %s:%d/ip", host, port);
		if (HttpGetPlainText(host, port, "/ip", out, outSize))
		{
			RealMasterLog("FastDL: got public IP from master: %s", out);
			return true;
		}

		if (port != 80)
		{
			RealMasterLog("FastDL: trying master %s:80/ip", host);
			if (HttpGetPlainText(host, 80, "/ip", out, outSize))
			{
				RealMasterLog("FastDL: got public IP from master:80: %s", out);
				return true;
			}
		}
	}

	RealMasterLog("FastDL: trying ipinfo.io/ip");
	if (HttpGetPlainText("ipinfo.io", 80, "/ip", out, outSize))
	{
		RealMasterLog("FastDL: got public IP from ipinfo.io: %s", out);
		return true;
	}

	return false;
}

static SOCKET g_listenSock = INVALID_SOCKET;
static HANDLE g_serverThread = NULL;
static volatile bool g_running = false;
static volatile LONG g_activeConnections = 0;
static char g_baseDir[512] = {0};

static const char *g_allowedExts[] = {
	".bmp", ".bsp", ".gif", ".jpg", ".png", ".lmp", ".lst", ".mdl",
	".mp3", ".res", ".spr", ".tga", ".txt", ".wad", ".wav", ".zip",
	".overviewtxt", NULL
};

static bool IsExtAllowed(const char *path)
{
	const char *dot = strrchr(path, '.');
	if (!dot) return false;

	if (stricmp(dot, ".cfg") == 0 || stricmp(dot, ".ini") == 0)
		return false;

	for (int i = 0; g_allowedExts[i]; i++)
	{
		if (stricmp(dot, g_allowedExts[i]) == 0)
			return true;
	}
	return false;
}

static bool IsPathSafe(const char *path)
{
	if (!path || !path[0]) return false;
	if (strstr(path, "..")) return false;
	if (path[0] == '\\' || path[0] == '/') path++;
	if (path[0] == '.') return false;

	for (const char *p = path; *p; p++)
	{
		if (*p < 0x20) return false;
	}
	return true;
}

static void CleanPath(char *out, int outSize, const char *in)
{
	const char *src = in;
	while (*src == '/' || *src == '\\') src++;

	int j = 0;
	for (int i = 0; src[i] && j < outSize - 1; i++)
	{
		char c = src[i];
		if (c == '/') c = '\\';
		if (c == '\\' && j > 0 && out[j - 1] == '\\') continue;
		out[j++] = c;
	}
	out[j] = '\0';
}

static bool ParseHttpRequest(const char *buf, int len, char *method, int methodSize,
	char *path, int pathSize, char *userAgent, int uaSize, bool *isHttp11)
{
	method[0] = path[0] = userAgent[0] = '\0';
	*isHttp11 = false;

	const char *end = buf + len;
	const char *lineEnd = (const char *)memchr(buf, '\n', len);
	if (!lineEnd) return false;

	int lineLen = (int)(lineEnd - buf);
	if (lineLen > 0 && buf[lineLen - 1] == '\r') lineLen--;

	char requestLine[512];
	if (lineLen >= (int)sizeof(requestLine)) return false;
	memcpy(requestLine, buf, lineLen);
	requestLine[lineLen] = '\0';

	char *sp1 = strchr(requestLine, ' ');
	if (!sp1) return false;
	*sp1 = '\0';
	strncpy(method, requestLine, methodSize - 1);

	char *pathStart = sp1 + 1;
	char *sp2 = strchr(pathStart, ' ');
	if (!sp2) return false;
	*sp2 = '\0';
	strncpy(path, pathStart, pathSize - 1);

	char *proto = sp2 + 1;
	if (strstr(proto, "HTTP/1.1")) *isHttp11 = true;

	const char *cur = lineEnd + 1;
	while (cur < end)
	{
		const char *nl = (const char *)memchr(cur, '\n', end - cur);
		if (!nl) break;

		int hdrLen = (int)(nl - cur);
		if (hdrLen > 0 && cur[hdrLen - 1] == '\r') hdrLen--;
		if (hdrLen == 0) break;

		if (strnicmp(cur, "User-Agent:", 11) == 0)
		{
			const char *val = cur + 11;
			while (*val == ' ') val++;
			int valLen = hdrLen - (int)(val - cur);
			if (valLen > uaSize - 1) valLen = uaSize - 1;
			memcpy(userAgent, val, valLen);
			userAgent[valLen] = '\0';
		}
		cur = nl + 1;
	}
	return true;
}

static void SendResponse(SOCKET client, int status, const char *statusText,
	const char *contentType, const void *body, int bodyLen)
{
	char header[512];
	int headerLen = snprintf(header, sizeof(header),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		status, statusText, contentType, bodyLen);

	send(client, header, headerLen, 0);
	if (body && bodyLen > 0)
	{
		int sent = 0;
		while (sent < bodyLen)
		{
			int chunk = bodyLen - sent;
			if (chunk > 65536) chunk = 65536;
			int r = send(client, (const char *)body + sent, chunk, 0);
			if (r <= 0) break;
			sent += r;
		}
	}
}

static void HandleClient(SOCKET client)
{
	char buf[4096];
	int total = 0;

	while (total < (int)sizeof(buf) - 1)
	{
		int r = recv(client, buf + total, sizeof(buf) - 1 - total, 0);
		if (r <= 0) { closesocket(client); return; }
		total += r;
		buf[total] = '\0';
		if (strstr(buf, "\r\n\r\n")) break;
	}

	char method[16], path[256], userAgent[128];
	bool isHttp11 = false;
	if (!ParseHttpRequest(buf, total, method, sizeof(method), path, sizeof(path),
		userAgent, sizeof(userAgent), &isHttp11))
	{
		closesocket(client);
		return;
	}

	if (!isHttp11 || strncmp(userAgent, "Half-Life", 9) != 0)
	{
		closesocket(client);
		return;
	}

	if (stricmp(method, "GET") != 0)
	{
		closesocket(client);
		return;
	}

	char cleanedPath[256];
	CleanPath(cleanedPath, sizeof(cleanedPath), path);

	if (!IsPathSafe(cleanedPath) || !IsExtAllowed(cleanedPath))
	{
		SendResponse(client, 404, "Not Found", "text/plain", "Not Found", 9);
		closesocket(client);
		return;
	}

	char fullPath[512];
	snprintf(fullPath, sizeof(fullPath), "%s\\%s", g_baseDir, cleanedPath);

	HANDLE hFile = CreateFileA(fullPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		SendResponse(client, 404, "Not Found", "text/plain", "Not Found", 9);
		closesocket(client);
		return;
	}

	DWORD fileSize = GetFileSize(hFile, NULL);
	if (fileSize == INVALID_FILE_SIZE || fileSize > FASTDL_MAX_FILE_SIZE)
	{
		CloseHandle(hFile);
		SendResponse(client, 403, "Forbidden", "text/plain", "Forbidden", 9);
		closesocket(client);
		return;
	}

	RealMasterLog("FastDL: serving %s (%d bytes)", cleanedPath, (int)fileSize);

	char header[512];
	int headerLen = snprintf(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n", (int)fileSize);
	send(client, header, headerLen, 0);

	char chunk[65536];
	DWORD remaining = fileSize;
	while (remaining > 0 && g_running)
	{
		DWORD toRead = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
		DWORD bytesRead;
		if (!ReadFile(hFile, chunk, toRead, &bytesRead, NULL) || bytesRead == 0)
			break;
		int sent = 0;
		while (sent < (int)bytesRead)
		{
			int r = send(client, chunk + sent, (int)bytesRead - sent, 0);
			if (r <= 0) goto done;
			sent += r;
		}
		remaining -= bytesRead;
	}

done:
	CloseHandle(hFile);
	closesocket(client);
}

static DWORD WINAPI ClientThread(LPVOID param)
{
	SOCKET client = (SOCKET)(uintptr_t)param;
	HandleClient(client);
	InterlockedDecrement(&g_activeConnections);
	return 0;
}

static DWORD WINAPI ServerThread(LPVOID param)
{
	RealMasterLog("FastDL: server thread started");

	while (g_running)
	{
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(g_listenSock, &readfds);

		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		int sel = select(0, &readfds, NULL, NULL, &tv);
		if (sel <= 0) continue;

		struct sockaddr_in addr;
		int addrLen = sizeof(addr);
		SOCKET client = accept(g_listenSock, (struct sockaddr *)&addr, &addrLen);
		if (client == INVALID_SOCKET) continue;

		if (g_activeConnections >= FASTDL_MAX_CONNECTIONS)
		{
			closesocket(client);
			continue;
		}

		struct timeval timeout;
		timeout.tv_sec = 30;
		timeout.tv_usec = 0;
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
		setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));

		InterlockedIncrement(&g_activeConnections);
		HANDLE hThread = CreateThread(NULL, 0, ClientThread, (LPVOID)(uintptr_t)client, 0, NULL);
		if (hThread)
			CloseHandle(hThread);
		else
		{
			InterlockedDecrement(&g_activeConnections);
			closesocket(client);
		}
	}

	RealMasterLog("FastDL: server thread stopped");
	return 0;
}

bool FastDL_Start(const char *halfLifeDir, const char *gameDir, const char *serverIP, int port)
{
	if (g_running) return true;

	snprintf(g_baseDir, sizeof(g_baseDir), "%s\\%s", halfLifeDir, gameDir);

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		RealMasterLog("FastDL: failed to create TCP socket");
		return false;
	}

	int reuse = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

	int boundPort = 0;
	for (int tryPort = port; tryPort <= port + 10; tryPort++)
	{
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons((uint16_t)tryPort);

		if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
		{
			boundPort = tryPort;
			break;
		}
	}

	if (boundPort == 0)
	{
		RealMasterLog("FastDL: failed to bind TCP port %d-%d", port, port + 10);
		closesocket(sock);
		return false;
	}

	if (listen(sock, FASTDL_MAX_CONNECTIONS) != 0)
	{
		RealMasterLog("FastDL: listen() failed");
		closesocket(sock);
		return false;
	}

	g_listenSock = sock;
	g_running = true;

	g_serverThread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);
	if (!g_serverThread)
	{
		RealMasterLog("FastDL: failed to create server thread");
		g_running = false;
		closesocket(g_listenSock);
		g_listenSock = INVALID_SOCKET;
		return false;
	}

	RealMasterLog("FastDL: HTTP server started on TCP port %d, serving from %s", boundPort, g_baseDir);
	return true;
}

void FastDL_Stop()
{
	if (!g_running) return;
	g_running = false;

	if (g_listenSock != INVALID_SOCKET)
	{
		closesocket(g_listenSock);
		g_listenSock = INVALID_SOCKET;
	}

	if (g_serverThread)
	{
		WaitForSingleObject(g_serverThread, 3000);
		CloseHandle(g_serverThread);
		g_serverThread = NULL;
	}

	RealMasterLog("FastDL: server stopped");
}
