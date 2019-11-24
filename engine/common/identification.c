/*
identification.c - unique id generation
Copyright (C) 2017 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include <fcntl.h>
#if !XASH_WIN32
#include <dirent.h>
#endif
static char id_md5[33];
static char id_customid[MAX_STRING];

/*
==========================================================

simple 64-bit one-hash-func bloom filter
should be enough to determine if device exist in identifier

==========================================================
*/
typedef integer64 bloomfilter_t;

static bloomfilter_t id;

#define bf64_mask ((1U<<6)-1)

bloomfilter_t BloomFilter_Process( const char *buffer, int size )
{
	dword crc32;
	bloomfilter_t value = 0;

	if( size <= 0 || size > 512 )
		return 0;

	CRC32_Init( &crc32 );
	CRC32_ProcessBuffer( &crc32, buffer, size );

	while( crc32 )
	{
		value |= ((integer64)1) << ( crc32 & bf64_mask );
		crc32 = crc32 >> 6;
	}

	return value;
}

bloomfilter_t BloomFilter_ProcessStr( const char *buffer )
{
	return BloomFilter_Process( buffer, Q_strlen( buffer ) );
}

uint BloomFilter_Weight( bloomfilter_t value )
{
	int weight = 0;

	while( value )
	{
		if( value & 1 )
			weight++;
		value = value >> 1;
#if _MSC_VER == 1200
		value &= 0x7FFFFFFFFFFFFFFF;
#endif
	}

	return weight;
}

qboolean BloomFilter_ContainsString( bloomfilter_t filter, const char *str )
{
	bloomfilter_t value = BloomFilter_ProcessStr( str );

	return (filter & value) == value;
}

/*
=============================================

IDENTIFICATION

=============================================
*/
#define MAXBITS_GEN 30
#define MAXBITS_CHECK MAXBITS_GEN + 6

qboolean ID_ProcessFile( bloomfilter_t *value, const char *path );

void ID_BloomFilter_f( void )
{
	bloomfilter_t value = 0;
	int i;

	for( i = 1; i < Cmd_Argc(); i++ )
		value |= BloomFilter_ProcessStr( Cmd_Argv( i ) );

	Msg( "%d %016llX\n", BloomFilter_Weight( value ), value );

	// test
	// for( i = 1; i < Cmd_Argc(); i++ )
	//	Msg( "%s: %d\n", Cmd_Argv( i ), BloomFilter_ContainsString( value, Cmd_Argv( i ) ) );
}

qboolean ID_VerifyHEX( const char *hex )
{
	uint chars = 0;
	char prev = 0;
	qboolean monotonic = true; // detect 11:22...
	int weight = 0;

	while( *hex++ )
	{
		char ch = Q_tolower( *hex );

		if( ( ch >= 'a' && ch <= 'f') || ( ch >= '0' && ch <= '9' ) )
		{
			if( prev && ( ch - prev < -1 || ch - prev > 1 ) )
				monotonic = false;

			if( ch >= 'a' )
				chars |= 1 << (ch - 'a' + 10);
			else
				chars |= 1 << (ch - '0');

			prev = ch;
		}
	}

	if( monotonic )
		return false;

	while( chars )
	{
		if( chars & 1 )
			weight++;

		chars = chars >> 1;

		if( weight > 2 )
			return true;
	}

	return false;
}

void ID_VerifyHEX_f( void )
{
	if( ID_VerifyHEX( Cmd_Argv( 1 ) ) )
		Msg( "Good\n" );
	else
		Msg( "Bad\n" );
}

#if XASH_LINUX
qboolean ID_ProcessCPUInfo( bloomfilter_t *value )
{
	int cpuinfofd = open( "/proc/cpuinfo", O_RDONLY );
	char buffer[1024], *pbuf, *pbuf2;
	int ret;

	if( cpuinfofd < 0 )
		return false;

	if( (ret = read( cpuinfofd, buffer, 1023 ) ) < 0 )
		return false;

	close( cpuinfofd );

	buffer[ret] = 0;

	if( !ret )
		return false;

	pbuf = Q_strstr( buffer, "Serial" );
	if( !pbuf )
		return false;
	pbuf += 6;

	if( ( pbuf2 = Q_strchr( pbuf, '\n' ) ) )
		*pbuf2 = 0;
	else
		pbuf2 = pbuf + Q_strlen( pbuf );

	if( !ID_VerifyHEX( pbuf ) )
		return false;

	*value |= BloomFilter_Process( pbuf, pbuf2 - pbuf );
	return true;
}

qboolean ID_ValidateNetDevice( const char *dev )
{
	const char *prefix = "/sys/class/net";
	byte *pfile;
	int assignType;

	// These devices are fake, their mac address is generated each boot, while assign_type is 0
	if( !Q_strnicmp( dev, "ccmni", sizeof( "ccmni" ) ) ||
		!Q_strnicmp( dev, "ifb", sizeof( "ifb" ) ) )
		return false;

	pfile = FS_LoadDirectFile( va( "%s/%s/addr_assign_type", prefix, dev ), NULL );

	// if NULL, it may be old kernel
	if( pfile )
	{
		assignType = Q_atoi( (char*)pfile );

		Mem_Free( pfile );

		// check is MAC address is constant
		if( assignType != 0 )
			return false;
	}

	return true;
}

int ID_ProcessNetDevices( bloomfilter_t *value )
{
	const char *prefix = "/sys/class/net";
	DIR *dir;
	struct dirent *entry;
	int count = 0;

	if( !( dir = opendir( prefix ) ) )
		return 0;

	while( ( entry = readdir( dir ) ) && BloomFilter_Weight( *value ) < MAXBITS_GEN )
	{
		if( !Q_strcmp( entry->d_name, "." ) || !Q_strcmp( entry->d_name, ".." ) )
			continue;

		if( !ID_ValidateNetDevice( entry->d_name ) )
			continue;

		count += ID_ProcessFile( value, va( "%s/%s/address", prefix, entry->d_name ) );
	}
	closedir( dir );
	return count;
}

int ID_CheckNetDevices( bloomfilter_t value )
{
	const char *prefix = "/sys/class/net";

	DIR *dir;
	struct dirent *entry;
	int count = 0;
	bloomfilter_t filter = 0;

	if( !( dir = opendir( prefix ) ) )
		return 0;

	while( ( entry = readdir( dir ) ) )
	{
		if( !Q_strcmp( entry->d_name, "." ) || !Q_strcmp( entry->d_name, ".." ) )
			continue;

		if( !ID_ValidateNetDevice( entry->d_name ) )
			continue;

		if( ID_ProcessFile( &filter, va( "%s/%s/address", prefix, entry->d_name ) ) )
			count += ( value & filter ) == filter, filter = 0;
	}

	closedir( dir );
	return count;
}

void ID_TestCPUInfo_f( void )
{
	bloomfilter_t value = 0;

	if( ID_ProcessCPUInfo( &value ) )
		Msg( "Got %016llX\n", value );
	else
		Msg( "Could not get serial\n" );
}

#endif

qboolean ID_ProcessFile( bloomfilter_t *value, const char *path )
{
	int fd = open( path, O_RDONLY );
	char buffer[256];
	int ret;

	if( fd < 0 )
		return false;

	if( (ret = read( fd, buffer, 255 ) ) < 0 )
		return false;

	close( fd );

	if( !ret )
		return false;

	buffer[ret] = 0;

	if( !ID_VerifyHEX( buffer ) )
		return false;

	*value |= BloomFilter_Process( buffer, ret );
	return true;
}

#if !XASH_WIN32
int ID_ProcessFiles( bloomfilter_t *value, const char *prefix, const char *postfix )
{
	DIR *dir;
	struct dirent *entry;
	int count = 0;

	if( !( dir = opendir( prefix ) ) )
	    return 0;

	while( ( entry = readdir( dir ) ) && BloomFilter_Weight( *value ) < MAXBITS_GEN )
	{
		if( !Q_strcmp( entry->d_name, "." ) || !Q_strcmp( entry->d_name, ".." ) )
			continue;

		count += ID_ProcessFile( value, va( "%s/%s/%s", prefix, entry->d_name, postfix ) );
	}
	closedir( dir );
	return count;
}

int ID_CheckFiles( bloomfilter_t value, const char *prefix, const char *postfix )
{
	DIR *dir;
	struct dirent *entry;
	int count = 0;
	bloomfilter_t filter = 0;

	if( !( dir = opendir( prefix ) ) )
	    return 0;

	while( ( entry = readdir( dir ) ) )
	{
		if( !Q_strcmp( entry->d_name, "." ) || !Q_strcmp( entry->d_name, ".." ) )
			continue;

		if( ID_ProcessFile( &filter, va( "%s/%s/%s", prefix, entry->d_name, postfix ) ) )
			count += ( value & filter ) == filter, filter = 0;
	}

	closedir( dir );
	return count;
}
#else
int ID_GetKeyData( HKEY hRootKey, char *subKey, char *value, LPBYTE data, DWORD cbData )
{
	HKEY hKey;

	if( RegOpenKeyEx( hRootKey, subKey, 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS )
		return 0;
	
	if( RegQueryValueEx( hKey, value, NULL, NULL, data, &cbData ) != ERROR_SUCCESS )
	{
		RegCloseKey( hKey );
		return 0;
	}

	RegCloseKey( hKey );
	return 1;
}
int ID_SetKeyData( HKEY hRootKey, char *subKey, DWORD dwType, char *value, LPBYTE data, DWORD cbData)
{
	HKEY hKey;
	if( RegCreateKey( hRootKey, subKey, &hKey ) != ERROR_SUCCESS )
		return 0;
	
	if( RegSetValueEx( hKey, value, 0, dwType, data, cbData ) != ERROR_SUCCESS )
	{
		RegCloseKey( hKey );
		return 0;
	}
	
	RegCloseKey( hKey );
	return 1;
}

#define BUFSIZE 4096

int ID_RunWMIC(char *buffer, const char *cmdline)
{
	HANDLE g_IN_Rd = NULL;
	HANDLE g_IN_Wr = NULL;
	HANDLE g_OUT_Rd = NULL;
	HANDLE g_OUT_Wr = NULL;
	DWORD dwRead;
	BOOL bSuccess = FALSE;
	SECURITY_ATTRIBUTES saAttr;
	
	STARTUPINFO si = {0};
	
	PROCESS_INFORMATION pi = {0};
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;
	
	CreatePipe( &g_IN_Rd, &g_IN_Wr, &saAttr, 0 );
	CreatePipe( &g_OUT_Rd, &g_OUT_Wr, &saAttr, 0 );
	SetHandleInformation( g_IN_Wr, HANDLE_FLAG_INHERIT, 0 );
	
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = g_IN_Rd;
	si.hStdOutput = g_OUT_Wr;
	si.hStdError = g_OUT_Wr;
	si.wShowWindow = SW_HIDE;
	si.dwFlags |= STARTF_USESTDHANDLES;

	CreateProcess( NULL, (char*)cmdline, NULL, NULL, true, CREATE_NO_WINDOW , NULL, NULL, &si, &pi );
	
	CloseHandle( g_OUT_Wr );
	CloseHandle( g_IN_Wr );
	
	WaitForSingleObject( pi.hProcess, 500 );
	
	bSuccess = ReadFile( g_OUT_Rd, buffer, BUFSIZE, &dwRead, NULL );
	buffer[BUFSIZE-1] = 0;
	CloseHandle( g_IN_Rd );
	CloseHandle( g_OUT_Rd );

	return bSuccess;
}

int ID_ProcessWMIC( bloomfilter_t *value, const char *cmdline )
{
	char buffer[BUFSIZE], token[BUFSIZE], *pbuf;
	int count = 0;

	if( !ID_RunWMIC( buffer, cmdline ) )
		return 0;
	pbuf = COM_ParseFile( buffer, token ); // Header
	while( pbuf = COM_ParseFile( pbuf, token ) )
	{
		if( !ID_VerifyHEX( token ) )
			continue;

		*value |= BloomFilter_ProcessStr( token );
		count ++;
	}

	return count;
}

int ID_CheckWMIC( bloomfilter_t value, const char *cmdline )
{
	char buffer[BUFSIZE], token[BUFSIZE], *pbuf;
	int count = 0;

	if( !ID_RunWMIC( buffer, cmdline ) )
		return 0;
	pbuf = COM_ParseFile( buffer, token ); // Header
	while( pbuf = COM_ParseFile( pbuf, token ) )
	{
		bloomfilter_t filter;

		if( !ID_VerifyHEX( token ) )
			continue;

		filter = BloomFilter_ProcessStr( token );
		count += ( filter & value ) == filter;
	}

	return count;
}
#endif


#if XASH_IOS
char *IOS_GetUDID( void );
#endif

bloomfilter_t ID_GenerateRawId( void )
{
	bloomfilter_t value = 0;
	int count = 0;

#if XASH_LINUX
#if XASH_ANDROID && !XASH_DEDICATED
	{
		const char *androidid = Android_GetAndroidID();
		if( androidid && ID_VerifyHEX( androidid ) )
		{
			value |= BloomFilter_ProcessStr( androidid );
			count ++;
		}
	}
#endif
	count += ID_ProcessCPUInfo( &value );
	count += ID_ProcessFiles( &value, "/sys/block", "device/cid" );
	count += ID_ProcessNetDevices( &value );
#endif
#if XASH_WIN32
	count += ID_ProcessWMIC( &value, "wmic path win32_physicalmedia get SerialNumber " );
	count += ID_ProcessWMIC( &value, "wmic bios get serialnumber " );
#endif
#if XASH_IOS
	{
		value |= BloomFilter_ProcessStr(IOS_GetUDID());
		count ++;
	}
#endif
	return value;
}

uint ID_CheckRawId( bloomfilter_t filter )
{
	bloomfilter_t value = 0;
	int count = 0;

#if XASH_LINUX
#if XASH_ANDROID && !XASH_DEDICATED
	{
		const char *androidid = Android_GetAndroidID();
		if( androidid && ID_VerifyHEX( androidid ) )
		{
			value = BloomFilter_ProcessStr( androidid );
			count += (filter & value) == value;
			value = 0;
		}
	}
#endif
	count += ID_CheckNetDevices( filter );
	count += ID_CheckFiles( filter, "/sys/block", "device/cid" );
	if( ID_ProcessCPUInfo( &value ) )
		count += (filter & value) == value;
#endif
	
#if XASH_WIN32
	count += ID_CheckWMIC( filter, "wmic path win32_physicalmedia get SerialNumber" );
	count += ID_CheckWMIC( filter, "wmic bios get serialnumber" );
#endif

#if XASH_IOS
	{
		value = BloomFilter_ProcessStr(IOS_GetUDID());
		count += (filter & value) == value;
		value = 0;
	}
#endif
#if 0
	Msg( "ID_CheckRawId: %d\n", count );
#endif
	return count;
}

#define SYSTEM_XOR_MASK 0x10331c2dce4c91db
#define GAME_XOR_MASK 0x7ffc48fbac1711f1

void ID_Check( void )
{
	uint weight = BloomFilter_Weight( id );
	uint mincount = weight >> 2;

	if( mincount < 1 )
		mincount = 1;

	if( weight > MAXBITS_CHECK )
	{
		id = 0;
#if 0
		Msg( "ID_Check(): fail %d\n", weight );
#endif
		return;
	}

	if( ID_CheckRawId( id ) < mincount )
		id = 0;
#if 0
	Msg( "ID_Check(): success %d\n", weight );
#endif
}

const char *ID_GetMD5( void )
{
	if( id_customid[0] )
		return id_customid;
	return id_md5;
}

/*
===============
ID_SetCustomClientID

===============
*/
void GAME_EXPORT ID_SetCustomClientID( const char *id )
{
	if( !id )
		return;

	Q_strncpy( id_customid, id, sizeof( id_customid  ) );
}

void ID_Init( void )
{
	MD5Context_t hash = {0};
	byte md5[16];
	int i;

	Cmd_AddCommand( "bloomfilter", ID_BloomFilter_f, "print bloomfilter raw value of arguments set");
	Cmd_AddCommand( "verifyhex", ID_VerifyHEX_f, "check if id source seems to be fake" );
#if XASH_LINUX
	Cmd_AddCommand( "testcpuinfo", ID_TestCPUInfo_f, "try read cpu serial" );
#endif

#if XASH_ANDROID && !XASH_DEDICATED
	sscanf( Android_LoadID(), "%016llX", &id );
	if( id )
	{
		id ^= SYSTEM_XOR_MASK;
		ID_Check();
	}
	
#elif XASH_WIN32
	{
		CHAR szBuf[MAX_PATH];
		ID_GetKeyData( HKEY_CURRENT_USER, "Software\\Xash3D\\", "xash_id", szBuf, MAX_PATH );
		
		sscanf(szBuf, "%016llX", &id);
		id ^= SYSTEM_XOR_MASK;
		ID_Check();
	}
#else
	{
		const char *home = getenv( "HOME" );
		if( home )
		{
			FILE *cfg = fopen( va( "%s/.config/.xash_id", home ), "r" );
			if( !cfg )
				cfg = fopen( va( "%s/.local/.xash_id", home ), "r" );
			if( !cfg )
				cfg = fopen( va( "%s/.xash_id", home ), "r" );
			if( cfg )
			{
				if( fscanf( cfg, "%016llX", &id ) > 0 )
				{
					id ^= SYSTEM_XOR_MASK;
					ID_Check();
				}
				fclose( cfg );
			}
		}
	}
#endif
	if( !id )
	{
		const char *buf = (const char*) FS_LoadFile( ".xash_id", NULL, false );
		if( buf )
		{
			sscanf( buf, "%016llX", &id );
			id ^= GAME_XOR_MASK;
			ID_Check();
		}
	}
	if( !id )
		id = ID_GenerateRawId();

	MD5Init( &hash );
	MD5Update( &hash, (byte *)&id, sizeof( id ) );
	MD5Final( (byte*)md5, &hash );

	for( i = 0; i < 16; i++ )
		Q_sprintf( &id_md5[i*2], "%02hhx", md5[i] );

#if XASH_ANDROID && !XASH_DEDICATED
	Android_SaveID( va("%016llX", id^SYSTEM_XOR_MASK ) );
#elif XASH_WIN32
	{
		CHAR Buf[MAX_PATH];
		sprintf( Buf, "%016llX", id^SYSTEM_XOR_MASK );
		ID_SetKeyData( HKEY_CURRENT_USER, "Software\\Xash3D\\", REG_SZ, "xash_id", Buf, Q_strlen(Buf) );
	}
#else
	{
		const char *home = getenv( "HOME" );
		if( home )
		{
			FILE *cfg = fopen( va( "%s/.config/.xash_id", home ), "w" );
			if( !cfg )
				cfg = fopen( va( "%s/.local/.xash_id", home ), "w" );
			if( !cfg )
				cfg = fopen( va( "%s/.xash_id", home ), "w" );
			if( cfg )
			{
				fprintf( cfg, "%016llX", id^SYSTEM_XOR_MASK );
				fclose( cfg );
			}
		}
	}
#endif
	FS_WriteFile( ".xash_id", va("%016llX", id^GAME_XOR_MASK), 16 );
#if 0
	Msg("MD5 id: %s\nRAW id:%016llX\n", id_md5, id );
#endif
}
