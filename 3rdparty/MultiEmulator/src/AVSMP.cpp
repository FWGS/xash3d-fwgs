#include "multi_emulator.h"

/* bUniverse param is "y" value in "STEAM_x:y:z" */
int GenerateAVSMP(void *pDest, int nSteamID, int bUniverse)
{
	auto pTicket = (int *)pDest;

	pTicket[0] = 0x14;                                    //  +0, header
	pTicket[3] = (nSteamID << 1) | (bUniverse ? 1 : 0);   // +12, SteamId, Low part
	pTicket[4] = 0x01100001;                              // +16, SteamId, High part

	return 28;
}
