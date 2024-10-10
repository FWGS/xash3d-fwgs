//DoubleBuffering.h Header File 
 
#ifndef __DOUBLEBUFFERING_H__ 
#define __DOUBLEBUFFERING_H__ 
 
//Typical DISCLAIMER: 
//The code in this project is Copyright (C) 2003 by George Anescu. You have the right to 
//use and distribute the code in any way you see fit as long as this paragraph is included 
//with the distribution. No warranties or claims are made as to the validity of the 
//information and code contained herein, so use it at your own risk. 
 
#include <fstream> 
 
using namespace std; 
 
class CDoubleBuffering 
{ 
public: 
	//Constructor 
	CDoubleBuffering(ifstream& in, char* pcBuff, int iSize, int iDataLen); 
	//Get Next Data Buffer 
	int GetData(char* pszDataBuf, int iDataLen=-1); 
 
private: 
	ifstream& m_rin; 
	int m_iSize; 
	int m_iSize2; //m_iSize/2 
	int m_iDataLen; 
	//Current Position 
	int m_iCurPos; 
	//End Position 
	int m_iEnd; 
	//Which Buffer 
	int m_iBuf; 
	char* m_pcBuff; 
	//EOF attained 
	bool m_bEOF; 
}; 
 
#endif //__DOUBLEBUFFERING_H__ 
