#include "multi_emulator.h"
#include "StrUtils.h"
#include "RevSpoofer.h"

int GenerateRevEmu(void *pDest, int nSteamID)
{
	char szhwid[64];

	CreateRandomString(szhwid, 16);
	if (!RevSpoofer::Spoof(szhwid, nSteamID))
		return 0;

	auto pTicket = (int *)pDest;
	auto revHash = RevSpoofer::Hash(szhwid);

	pTicket[0] = 'J';           //  +0, header
	pTicket[1] = revHash;       //  +4, hash of string at +24 offset
	pTicket[2] = 'r' << 16 | 'e' << 8 | 'v';//  +8, magic number
	pTicket[3] = 0;             // +12, unknown number, must always be 0

	pTicket[4] = revHash << 1;  // +16, SteamId, Low part
	pTicket[5] = 0x01100001;    // +20, SteamId, High part

	strcpy((char *)&pTicket[6], szhwid); // +24, string for hash

	return 152;
}
