#pragma once
#include "common.h"
#include "json.h"
#include "openssl.h"
#include <vector>
#include <list>
#include <set>

class ZBundle
{
public:
	ZBundle();

public:
	bool SignFolder(ZSignAsset* pSignAsset,
					const string& strFolder,
					const string& strBundleId,
					const string& strBundleVersion,
					const string& strDisplayName,
					const vector<string>& arrDylibFiles,
					const vector<string>& arrRemoveDylibNames,
					bool bForce,
					bool bWeakInject,
					bool bEnableCache,
					bool bRemoveProvision = false);

	bool SignFolder(list<ZSignAsset>* pSignAssets,
					const string& strFolder,
					const string& strBundleId,
					const string& strBundleVersion,
					const string& strDisplayName,
					const vector<string>& arrDylibFiles,
					const vector<string>& arrRemoveDylibNames,
					bool bForce,
					bool bWeakInject,
					bool bEnableCache,
					bool bRemoveProvision = false);

private:
	bool SignNode(jvalue& jvNode);
	void GetNodeChangedFiles(jvalue& jvNode);
	void GetChangedFiles(jvalue& jvNode, vector<string>& arrChangedFiles);
	bool ModifyPluginsBundleId(const string& strOldBundleId, const string& strNewBundleId);
	bool ModifyBundleInfo(const string& strBundleId, const string& strBundleVersion, const string& strDisplayName);
	bool ChangeAppIcon();

private:
	bool FindAppFolder(const string& strFolder, string& strAppFolder);
	bool GetObjectsToSign(const string& strFolder, jvalue& jvInfo);
	bool GetSignFolderInfo(const string& strFolder, jvalue& jvNode, bool bGetName = false);

private:
	bool GenerateCodeResources(const string& strFolder, jvalue& jvCodeRes);

private:
	bool			m_bForceSign;
	bool			m_bWeakInject;
	bool			m_bRemoveProvision;
	ZSignAsset*		m_pSignAsset;
	list<ZSignAsset>*	m_pSignAssets;
	vector<string>	m_arrInjectDylibs;
	vector<string>	m_arrInjectDylibNames;
	set<string>		m_setRemoveDylibs;

private:
	void ApplyAppModifications();

public:
	bool		m_bEnableDocuments;
	string		m_strMinVersion;
	string		m_strIconFile;
	bool		m_bRemoveExtensions;
	bool		m_bRemoveWatchApp;
	bool		m_bRemoveUISupportedDevices;
	bool		m_bInjectExtensions;
	string			m_strAppFolder;
};
