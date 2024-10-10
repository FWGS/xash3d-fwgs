//SHA.cpp
#include "SHA.h"
#include <exception>
#include <strstream>
#include <string.h>

using namespace std;

const unsigned int CSHA::sm_K160[4] =
{
    0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
};

const unsigned int CSHA::sm_H160[SHA160LENGTH] =
{
    0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
};

const unsigned int CSHA::sm_K256[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

const unsigned int CSHA::sm_H256[SHA256LENGTH] =
{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

const SUI64 CSHA::sm_H384[SHA512LENGTH] =
{
    {0xcbbb9d5d, 0xc1059ed8},
    {0x629a292a, 0x367cd507},
    {0x9159015a, 0x3070dd17},
    {0x152fecd8, 0xf70e5939},
    {0x67332667, 0xffc00b31},
    {0x8eb44a87, 0x68581511},
    {0xdb0c2e0d, 0x64f98fa7},
    {0x47b5481d, 0xbefa4fa4}
};

const SUI64 CSHA::sm_K512[80] =
{
    {0x428a2f98, 0xd728ae22}, {0x71374491, 0x23ef65cd},
    {0xb5c0fbcf, 0xec4d3b2f}, {0xe9b5dba5, 0x8189dbbc},
    {0x3956c25b, 0xf348b538}, {0x59f111f1, 0xb605d019},
    {0x923f82a4, 0xaf194f9b}, {0xab1c5ed5, 0xda6d8118},
    {0xd807aa98, 0xa3030242}, {0x12835b01, 0x45706fbe},
    {0x243185be, 0x4ee4b28c}, {0x550c7dc3, 0xd5ffb4e2},
    {0x72be5d74, 0xf27b896f}, {0x80deb1fe, 0x3b1696b1},
    {0x9bdc06a7, 0x25c71235}, {0xc19bf174, 0xcf692694},
    {0xe49b69c1, 0x9ef14ad2}, {0xefbe4786, 0x384f25e3},
    {0x0fc19dc6, 0x8b8cd5b5}, {0x240ca1cc, 0x77ac9c65},
    {0x2de92c6f, 0x592b0275}, {0x4a7484aa, 0x6ea6e483},
    {0x5cb0a9dc, 0xbd41fbd4}, {0x76f988da, 0x831153b5},
    {0x983e5152, 0xee66dfab}, {0xa831c66d, 0x2db43210},
    {0xb00327c8, 0x98fb213f}, {0xbf597fc7, 0xbeef0ee4},
    {0xc6e00bf3, 0x3da88fc2}, {0xd5a79147, 0x930aa725},
    {0x06ca6351, 0xe003826f}, {0x14292967, 0x0a0e6e70},
    {0x27b70a85, 0x46d22ffc}, {0x2e1b2138, 0x5c26c926},
    {0x4d2c6dfc, 0x5ac42aed}, {0x53380d13, 0x9d95b3df},
    {0x650a7354, 0x8baf63de}, {0x766a0abb, 0x3c77b2a8},
    {0x81c2c92e, 0x47edaee6}, {0x92722c85, 0x1482353b},
    {0xa2bfe8a1, 0x4cf10364}, {0xa81a664b, 0xbc423001},
    {0xc24b8b70, 0xd0f89791}, {0xc76c51a3, 0x0654be30},
    {0xd192e819, 0xd6ef5218}, {0xd6990624, 0x5565a910},
    {0xf40e3585, 0x5771202a}, {0x106aa070, 0x32bbd1b8},
    {0x19a4c116, 0xb8d2d0c8}, {0x1e376c08, 0x5141ab53},
    {0x2748774c, 0xdf8eeb99}, {0x34b0bcb5, 0xe19b48a8},
    {0x391c0cb3, 0xc5c95a63}, {0x4ed8aa4a, 0xe3418acb},
    {0x5b9cca4f, 0x7763e373}, {0x682e6ff3, 0xd6b2b8a3},
    {0x748f82ee, 0x5defb2fc}, {0x78a5636f, 0x43172f60},
    {0x84c87814, 0xa1f0ab72}, {0x8cc70208, 0x1a6439ec},
    {0x90befffa, 0x23631e28}, {0xa4506ceb, 0xde82bde9},
    {0xbef9a3f7, 0xb2c67915}, {0xc67178f2, 0xe372532b},
    {0xca273ece, 0xea26619c}, {0xd186b8c7, 0x21c0c207},
    {0xeada7dd6, 0xcde0eb1e}, {0xf57d4f7f, 0xee6ed178},
    {0x06f067aa, 0x72176fba}, {0x0a637dc5, 0xa2c898a6},
    {0x113f9804, 0xbef90dae}, {0x1b710b35, 0x131c471b},
    {0x28db77f5, 0x23047d84}, {0x32caab7b, 0x40c72493},
    {0x3c9ebe0a, 0x15c9bebc}, {0x431d67c4, 0x9c100d4c},
    {0x4cc5d4be, 0xcb3e42b6}, {0x597f299c, 0xfc657e2a},
    {0x5fcb6fab, 0x3ad6faec}, {0x6c44198c, 0x4a475817}
};

const SUI64 CSHA::sm_H512[SHA512LENGTH] =
{
    {0x6a09e667, 0xf3bcc908},
    {0xbb67ae85, 0x84caa73b},
    {0x3c6ef372, 0xfe94f82b},
    {0xa54ff53a, 0x5f1d36f1},
    {0x510e527f, 0xade682d1},
    {0x9b05688c, 0x2b3e6c1f},
    {0x1f83d9ab, 0xfb41bd6b},
    {0x5be0cd19, 0x137e2179}
};

//CONSTRUCTOR
CSHA::CSHA(int iMethod)
{
    //Check the method
    switch(iMethod)
    {
        case SHA160:
            {
                for(int i=0; i<SHA160LENGTH; i++)
                    m_auiBuf[i] = sm_H160[i];
                m_auiBits[0] = 0;
                m_auiBits[1] = 0;
            }
            break;

        case SHA256:
            {
                for(int i=0; i<SHA256LENGTH; i++)
                    m_auiBuf[i] = sm_H256[i];
                m_auiBits[0] = 0;
                m_auiBits[1] = 0;
            }
            break;

        case SHA384:
            {
                for(int i=0; i<SHA512LENGTH; i++)
                    m_aoui64Buf[i] = sm_H384[i];
                m_aoui64Bits[0].m_uiLeft = 0;
                m_aoui64Bits[0].m_uiRight = 0;
                m_aoui64Bits[1].m_uiLeft = 0;
                m_aoui64Bits[1].m_uiRight = 0;
            }
            break;

        case SHA512:
            {
                for(int i=0; i<SHA512LENGTH; i++)
                    m_aoui64Buf[i] = sm_H512[i];
                m_aoui64Bits[0].m_uiLeft = 0;
                m_aoui64Bits[0].m_uiRight = 0;
                m_aoui64Bits[1].m_uiLeft = 0;
                m_aoui64Bits[1].m_uiRight = 0;
            }
            break;

        default:
        {
            ostrstream ostr;
            ostr << "FileDigest ERROR: in CSHA() Constructor, Illegal Method " << iMethod << "!" << ends;
            string ostrMsg = ostr.str();
            ostr.freeze(false);
            throw runtime_error(ostrMsg);
        }
    }
    m_iMethod = iMethod;
}

//Update context to reflect the concatenation of another buffer of bytes.
void CSHA::AddData(char const* pcData, int iDataLength)
{
    if(iDataLength < 0)
        throw runtime_error(string("FileDigest ERROR: in CSHA::AddData(), Data Length should be >= 0!"));
    unsigned int uiT;
    switch(m_iMethod)
    {
        case SHA160:
        case SHA256:
            {
                //Update bitcount
                uiT = m_auiBits[0];
                if((m_auiBits[0] = uiT + ((unsigned int)iDataLength << 3)) < uiT)
                    m_auiBits[1]++; //Carry from low to high
                m_auiBits[1] += iDataLength >> 29;
                uiT = (uiT >> 3) & (BLOCKSIZE-1); //Bytes already
                //Handle any leading odd-sized chunks
                if(uiT != 0)
                {
                    unsigned char* puc = (unsigned char*)m_aucIn + uiT;
                    uiT = BLOCKSIZE - uiT;
                    if(iDataLength < uiT)
                    {
                        memcpy(puc, pcData, iDataLength);
                        return;
                    }
                    memcpy(puc, pcData, uiT);
                    Transform();
                    pcData += uiT;
                    iDataLength -= uiT;
                }
                //Process data in 64-byte chunks
                while(iDataLength >= BLOCKSIZE)
                {
                    memcpy(m_aucIn, pcData, BLOCKSIZE);
                    Transform();
                    pcData += BLOCKSIZE;
                    iDataLength -= BLOCKSIZE;
                }
                //Handle any remaining bytes of data
                memcpy(m_aucIn, pcData, iDataLength);
            }
            break;

        case SHA384:
        case SHA512:
            {
                uiT = m_aoui64Bits[0].m_uiRight;
                unsigned int uiU = m_aoui64Bits[0].m_uiLeft;
                if((m_aoui64Bits[0].m_uiRight = uiT + ((unsigned int)iDataLength << 3)) < uiT)
                    m_aoui64Bits[0].m_uiLeft++; //Carry from low to high
                unsigned int uiV = m_aoui64Bits[1].m_uiRight;
                if((m_aoui64Bits[0].m_uiLeft += iDataLength >> 29) < uiU)
                    m_aoui64Bits[1].m_uiRight++;
                if(m_aoui64Bits[1].m_uiRight < uiV)
                    m_aoui64Bits[1].m_uiLeft++;
                uiT = (uiT >> 3) & (BLOCKSIZE2-1); //Bytes already
                //Handle any leading odd-sized chunks
                if(uiT != 0)
                {
                    unsigned char* puc = (unsigned char*)m_aucIn + uiT;
                    uiT = BLOCKSIZE2 - uiT;
                    if(iDataLength < uiT)
                    {
                        memcpy(puc, pcData, iDataLength);
                        return;
                    }
                    memcpy(puc, pcData, uiT);
                    Transform();
                    pcData += uiT;
                    iDataLength -= uiT;
                }
                //Process data in 64-byte chunks
                while(iDataLength >= BLOCKSIZE2)
                {
                    memcpy(m_aucIn, pcData, BLOCKSIZE2);
                    Transform();
                    pcData += BLOCKSIZE2;
                    iDataLength -= BLOCKSIZE2;
                }
                //Handle any remaining bytes of data
                memcpy(m_aucIn, pcData, iDataLength);
            }
            break;
    }
    //Set the flag
    m_bAddData = true;
}

//Final wrapup - pad to 64-byte boundary with the bit pattern
//1 0*(64-bit count of bits processed, MSB-first)
void CSHA::FinalDigest(char* pcDigest)
{
    //Is the User's responsability to ensure that pcDigest is properly allocated 20, 32,
    //48 or 64 bytes, depending on the method
    if(false == m_bAddData)
        throw runtime_error(string("FileDigest ERROR: in CSHA::FinalDigest(), No data Added before call!"));
    switch(m_iMethod)
    {
        case SHA160:
        case SHA256:
            {
                unsigned int uiCount;
                unsigned char *puc;
                //Compute number of bytes mod 64
                uiCount = (m_auiBits[0] >> 3) & (BLOCKSIZE-1);
                //Set the first char of padding to 0x80. This is safe since there is
                //always at least one byte free
                puc = m_aucIn + uiCount;
                *puc++ = 0x80;
                //Bytes of padding needed to make 64 bytes
                uiCount = BLOCKSIZE - uiCount - 1;
                //Pad out to 56 mod 64
                if(uiCount < 8)
                {
                    //Two lots of padding: Pad the first block to 64 bytes
                    memset(puc, 0, uiCount);
                    Transform();
                    //Now fill the next block with 56 bytes
                    memset(m_aucIn, 0, BLOCKSIZE-8);
                }
                else
                {
                    //Pad block to 56 bytes
                    memset(puc, 0, uiCount - 8);
                }
                //Append length in bits and transform
                Word2Bytes(m_auiBits[1], &m_aucIn[BLOCKSIZE-8]);
                Word2Bytes(m_auiBits[0], &m_aucIn[BLOCKSIZE-4]);
                Transform();
                switch(m_iMethod)
                {
                    case SHA160:
                        {
                            for(int i=0; i<SHA160LENGTH; i++,pcDigest+=4)
                                Word2Bytes(m_auiBuf[i], reinterpret_cast<unsigned char*>(pcDigest));
                        }
                        break;

                    case SHA256:
                        {
                            for(int i=0; i<SHA256LENGTH; i++,pcDigest+=4)
                                Word2Bytes(m_auiBuf[i], reinterpret_cast<unsigned char*>(pcDigest));
                        }
                        break;
                }
            }
            break;

        case SHA384:
        case SHA512:
            {
                unsigned char *puc;
                //Compute number of bytes mod 128
                unsigned int uiCount = (m_aoui64Bits[0].m_uiRight >> 3) & (BLOCKSIZE2-1);
                //Set the first char of padding to 0x80. This is safe since there is
                //always at least one byte free
                puc = m_aucIn + uiCount;
                *puc++ = 0x80;
                //Bytes of padding needed to make 128 bytes
                uiCount = BLOCKSIZE2 - uiCount - 1;
                //Pad out to 112 mod 128
                if(uiCount < 16)
                {
                    //Two lots of padding: Pad the first block to 128 bytes
                    memset(puc, 0, uiCount);
                    Transform();
                    //Now fill the next block with 112 bytes
                    memset(m_aucIn, 0, BLOCKSIZE2-16);
                }
                else
                {
                    //Pad block to 112 bytes
                    memset(puc, 0, uiCount - 16);
                }
                //Append length in bits and transform
                Word2Bytes(m_aoui64Bits[1], &m_aucIn[BLOCKSIZE2-16]);
                Word2Bytes(m_aoui64Bits[0], &m_aucIn[BLOCKSIZE2-8]);
                Transform();
                switch(m_iMethod)
                {
                    case SHA384:
                        {
                            for(int i=0; i<SHA384LENGTH; i++,pcDigest+=8)
                                Word2Bytes(m_aoui64Buf[i], reinterpret_cast<unsigned char*>(pcDigest));
                        }
                        break;

                    case SHA512:
                        {
                            for(int i=0; i<SHA512LENGTH; i++,pcDigest+=8)
                                Word2Bytes(m_aoui64Buf[i], reinterpret_cast<unsigned char*>(pcDigest));
                        }
                        break;
                }
            }
            break;
    }
    //Reinitialize
    Reset();
}

//Reset current operation in order to prepare a new one
void CSHA::Reset()
{
    //Reinitialize
    switch(m_iMethod)
    {
        case SHA160:
            {
                for(int i=0; i<SHA160LENGTH; i++)
                    m_auiBuf[i] = sm_H160[i];
                m_auiBits[0] = 0;
                m_auiBits[1] = 0;
            }
            break;

        case SHA256:
            {
                for(int i=0; i<SHA256LENGTH; i++)
                    m_auiBuf[i] = sm_H256[i];
                m_auiBits[0] = 0;
                m_auiBits[1] = 0;
            }
            break;

        case SHA384:
            {
                for(int i=0; i<SHA512LENGTH; i++)
                    m_aoui64Buf[i] = sm_H384[i];
                m_aoui64Bits[0].m_uiLeft = 0;
                m_aoui64Bits[0].m_uiRight = 0;
                m_aoui64Bits[1].m_uiLeft = 0;
                m_aoui64Bits[1].m_uiRight = 0;
            }
            break;

        case SHA512:
            {
                for(int i=0; i<SHA512LENGTH; i++)
                    m_aoui64Buf[i] = sm_H512[i];
                m_aoui64Bits[0].m_uiLeft = 0;
                m_aoui64Bits[0].m_uiRight = 0;
                m_aoui64Bits[1].m_uiLeft = 0;
                m_aoui64Bits[1].m_uiRight = 0;
            }
    }
    //Reset the flag
    m_bAddData = false;
}

//The core of the SHA algorithm, this alters an existing SHA hash to
//reflect the addition of 16 longwords of new data.
void CSHA::Transform()
{
    switch(m_iMethod)
    {
        case SHA160:
            {
                //Expansion of m_aucIn
                unsigned char* pucIn = m_aucIn;
                unsigned int auiW[80];
                int i;
                for(i=0; i<16; i++,pucIn+=4)
                    Bytes2Word(pucIn, auiW[i]);
                for(i=16; i<80; i++)
                    auiW[i] = CircularShift(1, auiW[i-3]^auiW[i-8]^auiW[i-14]^auiW[i-16]);
                unsigned int temp;
                unsigned int A, B, C, D, E;
                A = m_auiBuf[0];
                B = m_auiBuf[1];
                C = m_auiBuf[2];
                D = m_auiBuf[3];
                E = m_auiBuf[4];
                for(i=0; i<20; i++)
                {
                    temp = CircularShift(5, A) + ((B & C) | ((~B) & D)) + E + auiW[i] + sm_K160[0];
                    E = D;
                    D = C;
                    C = CircularShift(30, B);
                    B = A;
                    A = temp;
                }
                for(i=20; i<40; i++)
                {
                    temp = CircularShift(5, A) + (B ^ C ^ D) + E + auiW[i] + sm_K160[1];
                    E = D;
                    D = C;
                    C = CircularShift(30, B);
                    B = A;
                    A = temp;
                }
                for(i=40; i<60; i++)
                {
                    temp = CircularShift(5, A) + ((B & C) | (B & D) | (C & D)) + E + auiW[i] + sm_K160[2];
                    E = D;
                    D = C;
                    C = CircularShift(30, B);
                    B = A;
                    A = temp;
                }
                for(i=60; i<80; i++)
                {
                    temp = CircularShift(5, A) + (B ^ C ^ D) + E + auiW[i] + sm_K160[3];
                    E = D;
                    D = C;
                    C = CircularShift(30, B);
                    B = A;
                    A = temp;
                }
                m_auiBuf[0] += A;
                m_auiBuf[1] += B;
                m_auiBuf[2] += C;
                m_auiBuf[3] += D;
                m_auiBuf[4] += E;
            }
            break;

        case SHA256:
            {
                //Expansion of m_aucIn
                unsigned char* pucIn = m_aucIn;
                unsigned int auiW[64];
                int i;
                for(i=0; i<16; i++,pucIn+=4)
                    Bytes2Word(pucIn, auiW[i]);
                for(i=16; i<64; i++)
                    auiW[i] = sig1(auiW[i-2]) + auiW[i-7] + sig0(auiW[i-15]) + auiW[i-16];
                //OR
                //for(i=0; i<48; i++)
                //  auiW[i+16] = sig1(auiW[i+14]) + auiW[i+9] + sig0(auiW[i+1]) + auiW[i];
                unsigned int a, b, c, d, e, f, g, h, t;
                a = m_auiBuf[0];
                b = m_auiBuf[1];
                c = m_auiBuf[2];
                d = m_auiBuf[3];
                e = m_auiBuf[4];
                f = m_auiBuf[5];
                g = m_auiBuf[6];
                h = m_auiBuf[7];
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[0] + auiW[0]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[1] + auiW[1]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[2] + auiW[2]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[3] + auiW[3]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[4] + auiW[4]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[5] + auiW[5]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[6] + auiW[6]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[7] + auiW[7]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[8] + auiW[8]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[9] + auiW[9]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[10] + auiW[10]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[11] + auiW[11]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[12] + auiW[12]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[13] + auiW[13]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[14] + auiW[14]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[15] + auiW[15]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[16] + auiW[16]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[17] + auiW[17]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[18] + auiW[18]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[19] + auiW[19]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[20] + auiW[20]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[21] + auiW[21]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[22] + auiW[22]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[23] + auiW[23]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[24] + auiW[24]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[25] + auiW[25]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[26] + auiW[26]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[27] + auiW[27]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[28] + auiW[28]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[29] + auiW[29]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[30] + auiW[30]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[31] + auiW[31]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[32] + auiW[32]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[33] + auiW[33]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[34] + auiW[34]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[35] + auiW[35]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[36] + auiW[36]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[37] + auiW[37]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[38] + auiW[38]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[39] + auiW[39]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[40] + auiW[40]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[41] + auiW[41]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[42] + auiW[42]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[43] + auiW[43]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[44] + auiW[44]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[45] + auiW[45]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[46] + auiW[46]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[47] + auiW[47]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[48] + auiW[48]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[49] + auiW[49]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[50] + auiW[50]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[51] + auiW[51]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[52] + auiW[52]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[53] + auiW[53]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[54] + auiW[54]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[55] + auiW[55]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K256[56] + auiW[56]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K256[57] + auiW[57]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K256[58] + auiW[58]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K256[59] + auiW[59]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K256[60] + auiW[60]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K256[61] + auiW[61]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K256[62] + auiW[62]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K256[63] + auiW[63]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                //OR
                /*
                unsigned int a, b, c, d, e, f, g, h, t1, t2;
                a = m_auiBuf[0];
                b = m_auiBuf[1];
                c = m_auiBuf[2];
                d = m_auiBuf[3];
                e = m_auiBuf[4];
                f = m_auiBuf[5];
                g = m_auiBuf[6];
                h = m_auiBuf[7];
                //
                for(i=0; i<64; i++)
                {
                    t1 = h + SIG1(e) + CH(e, f, g) + sm_K256[i] + auiW[i];
                    t2 = SIG0(a) + MAJ(a, b, c);
                    h = g;
                    g = f;
                    f = e;
                    e = d+t1;
                    d = c;
                    c = b;
                    b = a;
                    a = t1 + t2;
                }
                */
                m_auiBuf[0] += a;
                m_auiBuf[1] += b;
                m_auiBuf[2] += c;
                m_auiBuf[3] += d;
                m_auiBuf[4] += e;
                m_auiBuf[5] += f;
                m_auiBuf[6] += g;
                m_auiBuf[7] += h;
            }
            break;

            case SHA384:
            case SHA512:
            {
                //Expansion of m_aucIn
                unsigned char* pucIn = m_aucIn;
                SUI64 aoui64W[80];
                int i;
                for(i=0; i<16; i++,pucIn+=8)
                    Bytes2Word(pucIn, aoui64W[i]);
                for(i=16; i<80; i++)
                    aoui64W[i] = sig1(aoui64W[i-2]) + aoui64W[i-7] + sig0(aoui64W[i-15]) + aoui64W[i-16];
                SUI64 a, b, c, d, e, f, g, h, t;
                a = m_aoui64Buf[0];
                b = m_aoui64Buf[1];
                c = m_aoui64Buf[2];
                d = m_aoui64Buf[3];
                e = m_aoui64Buf[4];
                f = m_aoui64Buf[5];
                g = m_aoui64Buf[6];
                h = m_aoui64Buf[7];
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[0] + aoui64W[0]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[1] + aoui64W[1]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[2] + aoui64W[2]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[3] + aoui64W[3]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[4] + aoui64W[4]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[5] + aoui64W[5]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[6] + aoui64W[6]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[7] + aoui64W[7]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[8] + aoui64W[8]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[9] + aoui64W[9]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[10] + aoui64W[10]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[11] + aoui64W[11]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[12] + aoui64W[12]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[13] + aoui64W[13]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[14] + aoui64W[14]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[15] + aoui64W[15]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[16] + aoui64W[16]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[17] + aoui64W[17]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[18] + aoui64W[18]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[19] + aoui64W[19]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[20] + aoui64W[20]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[21] + aoui64W[21]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[22] + aoui64W[22]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[23] + aoui64W[23]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[24] + aoui64W[24]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[25] + aoui64W[25]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[26] + aoui64W[26]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[27] + aoui64W[27]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[28] + aoui64W[28]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[29] + aoui64W[29]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[30] + aoui64W[30]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[31] + aoui64W[31]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[32] + aoui64W[32]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[33] + aoui64W[33]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[34] + aoui64W[34]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[35] + aoui64W[35]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[36] + aoui64W[36]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[37] + aoui64W[37]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[38] + aoui64W[38]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[39] + aoui64W[39]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[40] + aoui64W[40]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[41] + aoui64W[41]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[42] + aoui64W[42]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[43] + aoui64W[43]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[44] + aoui64W[44]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[45] + aoui64W[45]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[46] + aoui64W[46]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[47] + aoui64W[47]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[48] + aoui64W[48]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[49] + aoui64W[49]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[50] + aoui64W[50]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[51] + aoui64W[51]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[52] + aoui64W[52]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[53] + aoui64W[53]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[54] + aoui64W[54]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[55] + aoui64W[55]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[56] + aoui64W[56]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[57] + aoui64W[57]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[58] + aoui64W[58]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[59] + aoui64W[59]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[60] + aoui64W[60]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[61] + aoui64W[61]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[62] + aoui64W[62]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[63] + aoui64W[63]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[64] + aoui64W[64]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[65] + aoui64W[65]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[66] + aoui64W[66]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[67] + aoui64W[67]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[68] + aoui64W[68]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[69] + aoui64W[69]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[70] + aoui64W[70]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[71] + aoui64W[71]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                t = h + SIG1(e) + CH(e, f, g) + sm_K512[72] + aoui64W[72]; h = t + SIG0(a) + MAJ(a, b, c); d += t;
                t = g + SIG1(d) + CH(d, e, f) + sm_K512[73] + aoui64W[73]; g = t + SIG0(h) + MAJ(h, a, b); c += t;
                t = f + SIG1(c) + CH(c, d, e) + sm_K512[74] + aoui64W[74]; f = t + SIG0(g) + MAJ(g, h, a); b += t;
                t = e + SIG1(b) + CH(b, c, d) + sm_K512[75] + aoui64W[75]; e = t + SIG0(f) + MAJ(f, g, h); a += t;
                t = d + SIG1(a) + CH(a, b, c) + sm_K512[76] + aoui64W[76]; d = t + SIG0(e) + MAJ(e, f, g); h += t;
                t = c + SIG1(h) + CH(h, a, b) + sm_K512[77] + aoui64W[77]; c = t + SIG0(d) + MAJ(d, e, f); g += t;
                t = b + SIG1(g) + CH(g, h, a) + sm_K512[78] + aoui64W[78]; b = t + SIG0(c) + MAJ(c, d, e); f += t;
                t = a + SIG1(f) + CH(f, g, h) + sm_K512[79] + aoui64W[79]; a = t + SIG0(b) + MAJ(b, c, d); e += t;
                //
                m_aoui64Buf[0] += a;
                m_aoui64Buf[1] += b;
                m_aoui64Buf[2] += c;
                m_aoui64Buf[3] += d;
                m_aoui64Buf[4] += e;
                m_aoui64Buf[5] += f;
                m_aoui64Buf[6] += g;
                m_aoui64Buf[7] += h;
            }
            break;
    }
}
