//MessageDigest.h 
 
#ifndef __MESSAGEDIGEST_H__ 
#define __MESSAGEDIGEST_H__ 
 
#include <string> 
 
using namespace std; 
 
//Typical DISCLAIMER: 
//The code in this project is Copyright (C) 2003 by George Anescu. You have the right to 
//use and distribute the code in any way you see fit as long as this paragraph is included 
//with the distribution. No warranties or claims are made as to the validity of the 
//information and code contained herein, so use it at your own risk. 
 
//General Message Digest Interface 
class IMessageDigest 
{ 
public: 
	//CONSTRUCTOR 
	IMessageDigest() : m_bAddData(false) {} 
	//DESTRUCTOR 
	virtual ~IMessageDigest() {} 
	//Update context to reflect the concatenation of another buffer of bytes 
	virtual void AddData(char const* pcData, int iDataLength) = 0; 
	//Final wrapup - pad to BLOCKSIZE-byte boundary with the bit pattern  
	//10000...(64-bit count of bits processed, MSB-first) 
	virtual void FinalDigest(char* pcDigest) = 0; 
	//Reset current operation in order to prepare for a new one 
	virtual void Reset() = 0; 
	//Digesting a Full File 
	void DigestFile(string const& rostrFileIn, char* pcDigest); 
 
protected: 
	enum { BLOCKSIZE=64 }; 
	//Control Flag 
	bool m_bAddData; 
	//The core of the MessageDigest algorithm, this alters an existing MessageDigest hash to 
	//reflect the addition of 64 bytes of new data 
	virtual void Transform() = 0; 
 
private: 
	enum { DATA_LEN=384, BUFF_LEN=1024 }; 
}; 
 
#endif // __MESSAGEDIGEST_H__ 
 