#include "multi_emulator.h"
int GenerateSteamEmu(void *pDest, int nSteamID)
{
	auto pTicket = (int *)pDest;

	pTicket[20] = -1;        // +80, dproto/reunion wants this value to be -1, but if this value
	                         //      does not match -1, then instead of SteamID in [21] cell
	                         //      client IP address that xored with 0x25730981 number should
	                         //      be used. But dproto/reunion will just skip ticket validation
	                         //      in that case.

	pTicket[21] = nSteamID;  // +84, SteamId, low part. Actually, this is just system volume serial
	                         //      number, which comes from GetVolumeInformationA() function. If
	                         //      function failed (returned 0), then instead of volume serial number
	                         //      777 number will be written to the ticket.

	return 768;
}
