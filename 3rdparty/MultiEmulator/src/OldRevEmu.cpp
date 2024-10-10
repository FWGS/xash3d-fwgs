#include "multi_emulator.h"

int GenerateOldRevEmu(void* pDest, int nSteamID)
{
	auto pTicket = (int*)pDest;
	auto pbTicket = (unsigned char*)pDest;

	pTicket[0] = 0xFFFF;                       // +0, header
	pTicket[1] = (nSteamID ^ 0xC9710266) << 1; // +4, SteamId
	*(short *)&pbTicket[8] = 0;                // +8, unknown, in original emulator must be 0

	return 10;
}
