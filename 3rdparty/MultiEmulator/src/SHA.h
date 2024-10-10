//SHA.h 
 
#ifndef __SHA_H__ 
#define __SHA_H__ 
 
#include "MessageDigest.h" 
 
//Typical DISCLAIMER: 
//The code in this project is Copyright (C) 2003 by George Anescu. You have the right to 
//use and distribute the code in any way you see fit as long as this paragraph is included 
//with the distribution. No warranties or claims are made as to the validity of the 
//information and code contained herein, so use it at your own risk. 
 
//Structure for representing an Unsigned Integer on 64 bits 
struct SUI64 
{ 
	//Data 
	unsigned int m_uiLeft; 
	unsigned int m_uiRight; 
 
	//Operators 
	SUI64& operator++() 
	{ 
		unsigned int uiTemp = m_uiRight; 
		m_uiRight++; 
		if(m_uiRight < uiTemp) 
			m_uiLeft++; 
		return *this; 
	} 
 
	SUI64& operator--() 
	{ 
		unsigned int uiTemp = m_uiRight; 
		m_uiRight--; 
		if(m_uiRight > uiTemp) 
			m_uiLeft--; 
		return *this; 
	} 
 
	SUI64& operator+=(SUI64 const& roUI64) 
	{ 
		m_uiRight += roUI64.m_uiRight; 
		if(m_uiRight < roUI64.m_uiRight) 
			m_uiLeft++; 
		m_uiLeft += roUI64.m_uiLeft; 
		return *this; 
	} 
 
	SUI64& operator|=(SUI64 const& roUI64) 
	{ 
		m_uiRight |= roUI64.m_uiRight; 
		m_uiLeft |= roUI64.m_uiLeft; 
		return *this; 
	} 
 
	SUI64& operator&=(SUI64 const& roUI64) 
	{ 
		m_uiRight &= roUI64.m_uiRight; 
		m_uiLeft &= roUI64.m_uiLeft; 
		return *this; 
	} 
 
	SUI64& operator^=(SUI64 const& roUI64) 
	{ 
		m_uiRight ^= roUI64.m_uiRight; 
		m_uiLeft ^= roUI64.m_uiLeft; 
		return *this; 
	} 
 
	SUI64& operator<<=(unsigned int uiBits) 
	{ 
		if(uiBits < 32) 
		{ 
			(m_uiLeft <<= uiBits) |= (m_uiRight >> (32-uiBits)); 
			m_uiRight <<= uiBits; 
			 
		} 
		else 
		{ 
			m_uiLeft = m_uiRight << (uiBits-32); 
			m_uiRight = 0; 
		} 
		return *this; 
	} 
 
	SUI64& operator>>=(unsigned int uiBits) 
	{ 
		if(uiBits < 32) 
		{ 
			(m_uiRight >>= uiBits) |= (m_uiLeft << (32-uiBits)); 
			m_uiLeft >>= uiBits; 
		} 
		else 
		{ 
			m_uiRight = m_uiLeft >> (uiBits-32); 
			m_uiLeft = 0; 
		} 
		return *this; 
	} 
 
	bool operator>(SUI64 const& roUI64) const 
	{ 
		if(m_uiLeft == roUI64.m_uiLeft) 
			return m_uiRight > roUI64.m_uiRight; 
		else 
			return m_uiLeft > roUI64.m_uiLeft; 
	} 
 
	bool operator<(SUI64 const& roUI64) const 
	{ 
		if(m_uiLeft == roUI64.m_uiLeft) 
			return m_uiRight < roUI64.m_uiRight; 
		else 
			return m_uiLeft < roUI64.m_uiLeft; 
	} 
}; 
 
inline SUI64 operator+(SUI64 const& roUI64_1, SUI64 const& roUI64_2) 
{ 
	SUI64 temp = roUI64_1; 
	temp += roUI64_2; 
	return temp; 
} 
 
inline SUI64 operator|(SUI64 const& roUI64_1, SUI64 const& roUI64_2) 
{ 
	SUI64 temp = roUI64_1; 
	temp |= roUI64_2; 
	return temp; 
} 
 
inline SUI64 operator&(SUI64 const& roUI64_1, SUI64 const& roUI64_2) 
{ 
	SUI64 temp = roUI64_1; 
	temp &= roUI64_2; 
	return temp; 
} 
 
inline SUI64 operator^(SUI64 const& roUI64_1, SUI64 const& roUI64_2) 
{ 
	SUI64 temp = roUI64_1; 
	temp ^= roUI64_2; 
	return temp; 
} 
 
inline SUI64 operator<<(SUI64 const& roUI64, unsigned int uiBits) 
{ 
	SUI64 temp = roUI64; 
	temp <<= uiBits; 
	return temp; 
} 
 
inline SUI64 operator>>(SUI64 const& roUI64, unsigned int uiBits) 
{ 
	SUI64 temp = roUI64; 
	temp >>= uiBits; 
	return temp; 
} 
 
inline bool operator>(SUI64 const& roUI64_1, SUI64 const& roUI64_2) 
{ 
	return roUI64_1.operator>(roUI64_2); 
} 
 
inline bool operator<(SUI64 const& roUI64_1, SUI64 const& roUI64_2) 
{ 
	return roUI64_1.operator<(roUI64_2); 
} 
 
//SHA Message Digest algorithm 
//SHA160 TEST VALUES: 
//1)"abc" 
//"A9993E364706816ABA3E25717850C26C9CD0D89D" 
// 
//2)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" 
//"84983E441C3BD26EBAAE4AA1F95129E5E54670F1" 
// 
//3)1,000,000 repetitions of "a".  
//"34AA973CD4C4DAA4F61EEB2BDBAD27316534016F" 
// 
//SHA256 TEST VALUES: 
//1)One-Block Message "abc" 
//"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" 
// 
//2)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" 
//"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" 
// 
//3)Multi-Block Message "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu" 
//"CF5B16A778AF8380036CE59E7B0492370B249B11E8F07A51AFAC45037AFEE9D1" 
// 
//4)Long Message "a" 1,000,000 times 
//"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" 
// 
//SHA384 TEST VALUES: 
//1)One-Block Message "abc" 
//"cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7" 
// 
//2)Multi-Block Message "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu" 
//"09330C33F71147E83D192FC782CD1B4753111B173B3B05D22FA08086E3B0F712FCC7C71A557E2DB966C3E9FA91746039" 
// 
//3)Long Message "a" 1,000,000 times 
//"9d0e1809716474cb086e834e310a4a1ced149e9c00f248527972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d8985" 
// 
//SHA512 TEST VALUES: 
//1)One-Block Message "abc" 
//"ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a8 
//36ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f" 
// 
//2)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu" 
//"8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4 
//331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909" 
// 
//3)"" 
//"CF83E1357EEFB8BDF1542850D66D8007D620E4050B5715DC83F4A921D36CE9CE47D0D13C5D85F2B0 
//FF8318D2877EEC2F63B931BD47417A81A538327AF927DA3E" 
// 
//4)Long Message "a" 1,000,000 times 
//"E718483D0CE769644E2E42C7BC15B4638E1F98B13B2044285632A803AFA973EBDE0FF244877EA60A 
//4CB0432CE577C31BEB009C5C2C49AA2E4EADB217AD8CC09B" 
 
class CSHA : public IMessageDigest 
{ 
public: 
	enum { SHA160=0, SHA256=1, SHA384=2, SHA512=3 }; 
	//CONSTRUCTOR 
	CSHA(int iMethod=SHA160); 
	//Update context to reflect the concatenation of another buffer of bytes. 
	void AddData(char const* pcData, int iDataLength); 
	//Final wrapup - pad to 64-byte boundary with the bit pattern  
	//1 0*(64-bit count of bits processed, MSB-first) 
	void FinalDigest(char* pcDigest); 
	//Reset current operation in order to prepare a new one 
	void Reset(); 
 
private: 
	//Transformation Function 
	void Transform(); 
	//The Method 
	int m_iMethod; 
	enum { BLOCKSIZE2 = BLOCKSIZE<<1 }; 
	//For 32 bits Integers 
	enum { SHA160LENGTH=5, SHA256LENGTH=8 }; 
	//Context Variables 
	unsigned int m_auiBuf[SHA256LENGTH]; //Maximum for SHA256 
	unsigned int m_auiBits[2]; 
	unsigned char m_aucIn[BLOCKSIZE2]; //128 bytes for SHA384, SHA512 
	//Internal auxiliary static functions 
	static unsigned int CircularShift(unsigned int uiBits, unsigned int uiWord); 
	static unsigned int CH(unsigned int x, unsigned int y, unsigned int z); 
	static unsigned int MAJ(unsigned int x, unsigned int y, unsigned int z); 
	static unsigned int SIG0(unsigned int x); 
	static unsigned int SIG1(unsigned int x); 
	static unsigned int sig0(unsigned int x); 
	static unsigned int sig1(unsigned int x); 
	static void Bytes2Word(unsigned char const* pcBytes, unsigned int& ruiWord); 
	static void Word2Bytes(unsigned int const& ruiWord, unsigned char* pcBytes); 
	static const unsigned int sm_K160[4]; 
	static const unsigned int sm_H160[SHA160LENGTH]; 
	static const unsigned int sm_K256[64]; 
	static const unsigned int sm_H256[SHA256LENGTH]; 
 
	//For 64 bits Integers 
	enum { SHA384LENGTH=6, SHA512LENGTH=8 }; 
	//Context Variables 
	SUI64 m_aoui64Buf[SHA512LENGTH]; //Maximum for SHA512 
	SUI64 m_aoui64Bits[2]; 
	//Internal auxiliary static functions 
	static SUI64 CircularShift(unsigned int uiBits, SUI64 const& roui64Word); 
	static SUI64 CH(SUI64 const& x, SUI64 const& y, SUI64 const& z); 
	static SUI64 MAJ(SUI64 const& x, SUI64 const& y, SUI64 const& z); 
	static SUI64 SIG0(SUI64 const& x); 
	static SUI64 SIG1(SUI64 const& x); 
	static SUI64 sig0(SUI64 const& x); 
	static SUI64 sig1(SUI64 const& x); 
	static void Bytes2Word(unsigned char const* pcBytes, SUI64& ruiWord); 
	static void Word2Bytes(SUI64 const& ruiWord, unsigned char* pcBytes); 
	static const SUI64 sm_H384[SHA512LENGTH]; //Dim is as 512 
	static const SUI64 sm_K512[80]; 
	static const SUI64 sm_H512[SHA512LENGTH]; 
}; 
 
inline unsigned int CSHA::CircularShift(unsigned int uiBits, unsigned int uiWord) 
{ 
	return (uiWord << uiBits) | (uiWord >> (32-uiBits)); 
} 
 
inline unsigned int CSHA::CH(unsigned int x, unsigned int y, unsigned int z) 
{ 
	return ((x&(y^z))^z); 
} 
 
inline unsigned int CSHA::MAJ(unsigned int x, unsigned int y, unsigned int z) 
{ 
	return (((x|y)&z)|(x&y)); 
} 
 
inline unsigned int CSHA::SIG0(unsigned int x) 
{ 
	return ((x >> 2)|(x << 30)) ^ ((x >> 13)|(x << 19)) ^ ((x >> 22)|(x << 10)); 
} 
 
inline unsigned int CSHA::SIG1(unsigned int x) 
{ 
	return ((x >> 6)|(x << 26)) ^ ((x >> 11)|(x << 21)) ^ ((x >> 25)|(x << 7)); 
} 
 
inline unsigned int CSHA::sig0(unsigned int x) 
{ 
	return ((x >> 7)|(x << 25)) ^ ((x >> 18)|(x << 14)) ^ (x >> 3); 
} 
 
inline unsigned int CSHA::sig1(unsigned int x) 
{ 
	return ((x >> 17)|(x << 15)) ^ ((x >> 19)|(x << 13)) ^ (x >> 10); 
} 
 
inline void CSHA::Bytes2Word(unsigned char const* pcBytes, unsigned int& ruiWord) 
{ 
	ruiWord = (unsigned int)*(pcBytes+3) | (unsigned int)(*(pcBytes+2)<<8) | 
		(unsigned int)(*(pcBytes+1)<<16) | (unsigned int)(*pcBytes<<24); 
} 
 
inline void CSHA::Word2Bytes(unsigned int const& ruiWord, unsigned char* pcBytes) 
{ 
	pcBytes += 3; 
	*pcBytes = ruiWord & 0xff; 
	*--pcBytes = (ruiWord>>8) & 0xff; 
	*--pcBytes = (ruiWord>>16) & 0xff; 
	*--pcBytes = (ruiWord>>24) & 0xff; 
} 
 
inline SUI64 CSHA::CircularShift(unsigned int uiBits, SUI64 const& roui64Word) 
{ 
	return (roui64Word << uiBits) | (roui64Word >> (64-uiBits)); 
} 
 
inline SUI64 CSHA::CH(SUI64 const& x, SUI64 const& y, SUI64 const& z) 
{ 
	return ((x&(y^z))^z); 
} 
 
inline SUI64 CSHA::MAJ(SUI64 const& x, SUI64 const& y, SUI64 const& z) 
{ 
	return (((x|y)&z)|(x&y)); 
} 
 
inline SUI64 CSHA::SIG0(SUI64 const& x) 
{ 
	return ((x >> 28)|(x << 36)) ^ ((x >> 34)|(x << 30)) ^ ((x >> 39)|(x << 25)); 
} 
 
inline SUI64 CSHA::SIG1(SUI64 const& x) 
{ 
	return ((x >> 14)|(x << 50)) ^ ((x >> 18)|(x << 46)) ^ ((x >> 41)|(x << 23)); 
} 
 
inline SUI64 CSHA::sig0(SUI64 const& x) 
{ 
	return ((x >> 1)|(x << 63)) ^ ((x >> 8)|(x << 56)) ^ (x >> 7); 
} 
 
inline SUI64 CSHA::sig1(SUI64 const& x) 
{ 
	return ((x >> 19)|(x << 45)) ^ ((x >> 61)|(x << 3)) ^ (x >> 6); 
} 
 
inline void CSHA::Bytes2Word(unsigned char const* pcBytes, SUI64& ruiWord) 
{ 
	Bytes2Word(pcBytes+4, ruiWord.m_uiRight); 
	Bytes2Word(pcBytes, ruiWord.m_uiLeft); 
} 
 
inline void CSHA::Word2Bytes(SUI64 const& ruiWord, unsigned char* pcBytes) 
{ 
	Word2Bytes(ruiWord.m_uiRight, pcBytes+4); 
	Word2Bytes(ruiWord.m_uiLeft, pcBytes); 
} 
 
#endif // __SHA_H__ 