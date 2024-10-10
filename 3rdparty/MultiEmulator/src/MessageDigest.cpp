//MessageDigest.cpp   
   
#include "MessageDigest.h"   
#include "DoubleBuffering.h"   
   
#include <exception>   
#include <fstream>   
#include <strstream>   
   
using namespace std;   
   
//Digesting a Full File   
void IMessageDigest::DigestFile(string const& rostrFileIn, char* pcDigest)   
{   
    //Is the User's responsability to ensure that pcDigest is appropriately allocated   
    //Open Input File   
    ifstream in(rostrFileIn.c_str(), ios::binary);   
    if(!in)   
    {   
        ostrstream ostr;   
        ostr << "FileDigest ERROR: in IMessageDigest::DigestFile(): Cannot open File " << rostrFileIn << "!" << ends;   
        string ostrMsg = ostr.str();   
        ostr.freeze(false);   
        throw runtime_error(ostrMsg);   
    }   
    //Resetting first   
    Reset();   
    //Reading from file   
    char szLargeBuff[BUFF_LEN+1] = {0};   
    char szBuff[DATA_LEN+1] = {0};   
    CDoubleBuffering oDoubleBuffering(in, szLargeBuff, BUFF_LEN, DATA_LEN);   
    int iRead;   
    while((iRead=oDoubleBuffering.GetData(szBuff)) > 0)   
        AddData(szBuff, iRead);   
    in.close();   
    //Final Step   
    FinalDigest(pcDigest);   
}   