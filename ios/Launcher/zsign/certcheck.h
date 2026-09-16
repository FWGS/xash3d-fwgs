#pragma once
#include "common.h"

// Check certificate validity and OCSP revocation status.
// Works on: .ipa, Mach-O binary, .mobileprovision, .p12/.pfx, .cer/.pem
// Returns: 0 = valid, 1 = revoked, 2 = expired, -2 = not signed, -1 = error
int CheckCertificate(const string& strFilePath, const string& strPassword);

// Check the main binary inside an extracted app folder (used after signing)
// Returns: 0 = valid, 1 = revoked, 2 = expired, -2 = not signed, -1 = error
int CheckSignedBinary(const string& strAppFolder);
