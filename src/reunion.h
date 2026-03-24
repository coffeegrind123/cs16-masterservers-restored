#pragma once
#include <windows.h>
#include <stdint.h>

bool Reunion_InstallHook(uint8_t *hwBase, size_t hwSize, HMODULE hRealSteamApi, void *pCvarFindVar,
	void *pServerState, uint8_t *pClientArray, int *pMaxPlayers, int clientStride);
void Reunion_PostConnect();
