#pragma once
#include "json.h"

class ZSignAsset
{
public:
	ZSignAsset();

public:
	bool Init(const string& strCertFile, 
				const string& strPKeyFile,
				const string& strProvFile,
				const string& strEntitleFile,
				const string& strPassword,
				bool bAdhoc,
				bool bSHA256Only,
				bool bSingleBinary);
	bool InitSimple(const void* strSignerPKeyData,
			   int strSignerPKeyDataSize,
			   const void* strProvisionData,
			   int strProvisionDataSize,
			   const string &strPassword);

	bool GenerateCMS(const string& strCDHashData, 
						const string& strCDHashesPlist, 
						const string& strCodeDirectorySlotSHA1, 
						const string& strAltnateCodeDirectorySlot256, 
						string& strCMSOutput);
	bool GetCMSContent2(const void* strCMSDataInput, int size, string &strContentOutput);

private:
	bool GenerateCMS(void* pscert, 
						void* pspkey, 
						const string& strCDHashData, 
						const string& strCDHashesPlist, 
						const string& strCodeDirectorySlotSHA1, 
						const string& strAltnateCodeDirectorySlot256, 
						string& strCMSOutput);

	bool GetCertSubjectCN(void* cert, string& strSubjectCN);
	bool GetCertSubjectCN(const string& strCertData, string& strSubjectCN);
	bool GetCertOU(void* pcert, string& strOU);

public:
	static bool		CMSError();
	// Returns the embedded WWDR intermediate (G1-G8) whose subject name hash
	// matches uIssuerHash, or NULL. Shared by signing and certificate check.
	static const char*	WWDRIntermediatePEM(unsigned long uIssuerHash);
	static void*	GenerateASN1Type(const string& value);
	static bool		GetCertInfo(void* pcert, jvalue& jvCertInfo);
	static bool		GetCMSInfo(uint8_t* pCMSData, uint32_t uCMSLength, jvalue& jvOutput);
	static bool		GetCMSContent(const string& strCMSDataInput, string& strContentOutput);
	static void		ParseCertSubject(const string& strSubject, jvalue& jvSubject);
	static string	ASN1_TIMEtoString(const void* time);

public:
	bool	m_bAdhoc;
	bool	m_bSHA256Only;
	bool	m_bSingleBinary;
	string	m_strTeamId;
	string	m_strSubjectCN;
	string	m_strProvData;
	string	m_strEntitleData;
	string	m_strApplicationId;
	time_t  expirationDate;

private:
	void*	m_evpPKey;
	void*	m_x509Cert;
	void*	m_caCerts; // STACK_OF(X509)* CA chain recovered from the input p12, if any

public:
	static const char* s_szAppleDevCACert;
	static const char* s_szAppleRootCACert;
	static const char* s_szAppleRootCACertG3;
	static const char* s_szAppleDevCACertG3;
	static const char* s_szAppleDevCACertG2;
	static const char* s_szAppleDevCACertG4;
	static const char* s_szAppleDevCACertG5;
	static const char* s_szAppleDevCACertG6;
	static const char* s_szAppleDevCACertG7;
	static const char* s_szAppleDevCACertG8;

public:
	class OpenSSLInit
	{
	public:
		OpenSSLInit();
	};
	static OpenSSLInit s_OpenSSLInit;
};
