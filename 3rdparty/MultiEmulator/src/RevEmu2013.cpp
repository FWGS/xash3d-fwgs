#include "multi_emulator.h"
#include "StrUtils.h"
#include "RevSpoofer.h"
#include "CRijndael.h"
#include "SHA.h"
#include <time.h>

int GenerateRevEmu2013(void *pDest, int nSteamID)
{
	char szhwid[64];

	CreateRandomString(szhwid, 32);
	if (!RevSpoofer::Spoof(szhwid, nSteamID))
		return 0;

	auto pTicket = (int *)pDest;
	auto pbTicket = (unsigned char *)pDest;

	auto revHash = RevSpoofer::Hash(szhwid);

	pTicket[0] = 'S';                      // +0
	pTicket[1] = revHash;                  // +4
	pTicket[2] = 'r' << 16 | 'e' << 8 | 'v';;// +8
	pTicket[3] = 0;                        // +12
	pTicket[4] = revHash << 1;             // +16
	pTicket[5] = 0x01100001;               // +20
	pTicket[6] = (int)time(0) + 90123;  // +24
	pbTicket[27] = ~(pbTicket[27] + pbTicket[24]);
	pTicket[7] = ~(int)time(0);         // +28
	pTicket[8] = revHash * 2 >> 3;         // +32
	pTicket[9] = 0;                        // +36

	static const char c_szAESKeyRand[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

	char szAESHashRand[32];
	auto AESRand = CRijndael();
	AESRand.MakeKey(c_szAESKeyRand, CRijndael::sm_chain0, 32, 32);
	AESRand.EncryptBlock(szhwid, szAESHashRand);
	memcpy(&pbTicket[40], szAESHashRand, 32);

	static const char c_szAESKeyRev[] = "_YOU_SERIOUSLY_NEED_TO_GET_LAID_";
	char AESHashRev[32];
	auto AESRev = CRijndael();
	AESRev.MakeKey(c_szAESKeyRev, CRijndael::sm_chain0, 32, 32);
	AESRev.EncryptBlock(c_szAESKeyRand, AESHashRev);
	memcpy(&pbTicket[72], AESHashRev, 32);

	char szSHAHash[32];
	auto sha = CSHA(CSHA::SHA256);
	sha.AddData(szhwid, 32);
	sha.FinalDigest(szSHAHash);
	memcpy(&pbTicket[104], szSHAHash, 32);

	return 194;
}
