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

#include "build.h"
#include <inttypes.h>
#include <fcntl.h>
#if !XASH_WIN32
#include <dirent.h>
#else
#include <io.h>
#endif
#include "common.h"
#include "client.h"

/*
==========================================================

simple 64-bit one-hash-func bloom filter
should be enough to determine if device exist in identifier

==========================================================
*/
typedef uint64_t bloomfilter_t;

static bloomfilter_t id;

#define bf64_mask ((1U<<6)-1)

static bloomfilter_t BloomFilter_Process( const char *buffer, int size )
{
	dword crc32;
	bloomfilter_t value = 0;

	if( size <= 0 || size > 512 )
		return 0;

	CRC32_Init( &crc32 );
	CRC32_ProcessBuffer( &crc32, buffer, size );

	while( crc32 )
	{
		value |= ((uint64_t)1) << ( crc32 & bf64_mask );
		crc32 = crc32 >> 6;
	}

	return value;
}

static bloomfilter_t BloomFilter_ProcessStr( const char *buffer )
{
	return BloomFilter_Process( buffer, Q_strlen( buffer ) );
}

static uint BloomFilter_Weight( bloomfilter_t value )
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

static qboolean BloomFilter_ContainsString( bloomfilter_t filter, const char *str )
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

static qboolean ID_ProcessFile( bloomfilter_t *value, const char *path );

static void ID_BloomFilter_f( void )
{
	bloomfilter_t value = 0;
	int i;

	for( i = 1; i < Cmd_Argc(); i++ )
		value |= BloomFilter_ProcessStr( Cmd_Argv( i ) );

	Msg( "%d %016"PRIX64"\n", BloomFilter_Weight( value ), value );

	// test
	// for( i = 1; i < Cmd_Argc(); i++ )
	//	Msg( "%s: %d\n", Cmd_Argv( i ), BloomFilter_ContainsString( value, Cmd_Argv( i ) ) );
}

static qboolean ID_VerifyHEX( const char *hex )
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

static void ID_VerifyHEX_f( void )
{
	if( ID_VerifyHEX( Cmd_Argv( 1 ) ) )
		Msg( "Good\n" );
	else
		Msg( "Bad\n" );
}

#if XASH_LINUX
static qboolean ID_ProcessCPUInfo( bloomfilter_t *value )
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

static qboolean ID_ValidateNetDevice( const char *dev )
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

static int ID_ProcessNetDevices( bloomfilter_t *value )
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

static int ID_CheckNetDevices( bloomfilter_t value )
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

static void ID_TestCPUInfo_f( void )
{
	bloomfilter_t value = 0;

	if( ID_ProcessCPUInfo( &value ) )
		Msg( "Got %016"PRIX64"\n", value );
	else
		Msg( "Could not get serial\n" );
}

#endif

static qboolean ID_ProcessFile( bloomfilter_t *value, const char *path )
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
static int ID_ProcessFiles( bloomfilter_t *value, const char *prefix, const char *postfix )
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

static int ID_CheckFiles( bloomfilter_t value, const char *prefix, const char *postfix )
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
static int ID_GetKeyData( HKEY hRootKey, char *subKey, char *value, LPBYTE data, DWORD cbData )
{
	HKEY hKey;

	if( RegOpenKeyExA( hRootKey, subKey, 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS )
		return 0;

	if( RegQueryValueExA( hKey, value, NULL, NULL, data, &cbData ) != ERROR_SUCCESS )
	{
		RegCloseKey( hKey );
		return 0;
	}

	RegCloseKey( hKey );
	return 1;
}

static int ID_SetKeyData( HKEY hRootKey, char *subKey, DWORD dwType, char *value, LPBYTE data, DWORD cbData )
{
	HKEY hKey;
	if( RegCreateKeyA( hRootKey, subKey, &hKey ) != ERROR_SUCCESS )
		return 0;

	if( RegSetValueExA( hKey, value, 0, dwType, data, cbData ) != ERROR_SUCCESS )
	{
		RegCloseKey( hKey );
		return 0;
	}

	RegCloseKey( hKey );
	return 1;
}

#define BUFSIZE 4096

static int ID_RunWMIC( char *buffer, const wchar_t *cmdline )
{
	HANDLE g_IN_Rd = NULL;
	HANDLE g_IN_Wr = NULL;
	HANDLE g_OUT_Rd = NULL;
	HANDLE g_OUT_Wr = NULL;
	DWORD dwRead;
	BOOL bSuccess = FALSE;
	wchar_t *cmdline_copy;
	
	const int cmdline_size = wcslen( cmdline ) * sizeof( *cmdline );
	PROCESS_INFORMATION pi = { 0 };
	SECURITY_ATTRIBUTES saAttr =
	{
		.nLength = sizeof( SECURITY_ATTRIBUTES ),
		.bInheritHandle = TRUE,
		.lpSecurityDescriptor = NULL,
	};

	CreatePipe( &g_IN_Rd, &g_IN_Wr, &saAttr, 0 );
	CreatePipe( &g_OUT_Rd, &g_OUT_Wr, &saAttr, 0 );
	SetHandleInformation( g_IN_Wr, HANDLE_FLAG_INHERIT, 0 );

	STARTUPINFOW si =
	{
		.cb = sizeof( STARTUPINFOW ),
		.dwFlags = STARTF_USESTDHANDLES,
		.hStdInput = g_IN_Rd,
		.hStdOutput = g_OUT_Wr,
		.hStdError = g_OUT_Wr,
		.wShowWindow = SW_HIDE,
		.dwFlags = STARTF_USESTDHANDLES,
	};

	cmdline_copy = malloc( cmdline_size );
	if( !cmdline_copy )
		goto err;

	memcpy( cmdline_copy, cmdline, cmdline_size );

	if( !CreateProcessW( NULL, cmdline_copy, NULL, NULL, true, CREATE_NO_WINDOW, NULL, NULL, &si, &pi ))
		goto err;

	WaitForSingleObject( pi.hProcess, 500 );

	bSuccess = ReadFile( g_OUT_Rd, buffer, BUFSIZE, &dwRead, NULL );
	buffer[BUFSIZE-1] = 0;

	TerminateProcess( pi.hProcess, 0 );

	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );

err:
	CloseHandle( g_IN_Wr );
	CloseHandle( g_OUT_Wr );
	CloseHandle( g_IN_Rd );
	CloseHandle( g_OUT_Rd );

	free( cmdline_copy );

	return bSuccess;
}

static int ID_ProcessWMIC( bloomfilter_t *value, const wchar_t *cmdline )
{
	char buffer[BUFSIZE], token[BUFSIZE], *pbuf;
	int count = 0;

	if( !ID_RunWMIC( buffer, cmdline ))
		return 0;

	pbuf = COM_ParseFile( buffer, token, sizeof( token )); // Header
	while(( pbuf = COM_ParseFile( pbuf, token, sizeof( token ))))
	{
		if( !ID_VerifyHEX( token ))
			continue;

		*value |= BloomFilter_ProcessStr( token );
		count++;
	}

	return count;
}

static int ID_CheckWMIC( bloomfilter_t value, const wchar_t *cmdline )
{
	char buffer[BUFSIZE], token[BUFSIZE], *pbuf;
	int count = 0;

	if( !ID_RunWMIC( buffer, cmdline ))
		return 0;

	pbuf = COM_ParseFile( buffer, token, sizeof( token )); // Header
	while(( pbuf = COM_ParseFile( pbuf, token, sizeof( token ))))
	{
		bloomfilter_t filter;

		if( !ID_VerifyHEX( token ))
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

#if XASH_PSVITA
int PSVita_GetPSID( char *buf, const size_t buflen );
#endif

static bloomfilter_t ID_GenerateRawId( void )
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
	count += ID_ProcessWMIC( &value, L"wmic path win32_physicalmedia get SerialNumber " );
	count += ID_ProcessWMIC( &value, L"wmic bios get serialnumber " );
#endif
#if XASH_IOS
	{
		value |= BloomFilter_ProcessStr(IOS_GetUDID());
		count ++;
	}
#endif
#if XASH_PSVITA
	{
		char data[16];
		PSVita_GetPSID( data, sizeof( data ));
		value |= BloomFilter_Process( data, sizeof( data ));
		count ++;
	}
#endif
	return value;
}

static uint ID_CheckRawId( bloomfilter_t filter )
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
	count += ID_CheckWMIC( filter, L"wmic path win32_physicalmedia get SerialNumber" );
	count += ID_CheckWMIC( filter, L"wmic bios get serialnumber" );
#endif

#if XASH_IOS
	{
		value = BloomFilter_ProcessStr(IOS_GetUDID());
		count += (filter & value) == value;
		value = 0;
	}
#endif
#if XASH_PSVITA
	{
		char data[16];
		PSVita_GetPSID( data, sizeof( data ));
		value = BloomFilter_Process( data, sizeof( data ));
		count += (filter & value) == value;
		value = 0;
	}
#endif
#if 0
	Msg( "%s: %d\n", __func__, count );
#endif
	return count;
}

#define SYSTEM_XOR_MASK 0x10331c2dce4c91db
#define GAME_XOR_MASK 0x7ffc48fbac1711f1

static void ID_Check( void )
{
	uint weight = BloomFilter_Weight( id );
	uint mincount = weight >> 2;

	if( mincount < 1 )
		mincount = 1;

	if( weight > MAXBITS_CHECK )
	{
		id = 0;
#if 0
		Msg( "%s: fail %d\n", __func__, weight );
#endif
		return;
	}

	if( ID_CheckRawId( id ) < mincount )
		id = 0;
#if 0
	Msg( "%s: success %d\n", __func__, weight );
#endif
}

void ID_GetMD5ForAddress( char *key, netadr_t adr, size_t size )
{
	MD5Context_t ctx;
	byte buf[32], md5[16];
	size_t bufsize = 0;
	bloomfilter_t value = id;

	switch( NET_NetadrType( &adr ))
	{
	// local server case
	case NA_IP:
		memcpy( buf, adr.ip, sizeof( adr.ip ));
		bufsize = sizeof( adr.ip );
		break;
	case NA_IP6:
		NET_NetadrToIP6Bytes( buf, &adr );
		bufsize = 16;
		break;
	default:
		break;
	}

	if( bufsize != 0 )
		value = BloomFilter_Process( buf, bufsize );

	MD5Init( &ctx );
	MD5Update( &ctx, (byte *)&value, sizeof( value ));
	MD5Final( md5, &ctx );
	Q_strnlwr( MD5_Print( md5 ), key, size );
}

void ID_Init( void )
{
	Cmd_AddRestrictedCommand( "bloomfilter", ID_BloomFilter_f, "print bloomfilter raw value of arguments set");
	Cmd_AddRestrictedCommand( "verifyhex", ID_VerifyHEX_f, "check if id source seems to be fake" );
#if XASH_LINUX
	Cmd_AddRestrictedCommand( "testcpuinfo", ID_TestCPUInfo_f, "try read cpu serial" );
#endif

#if XASH_ANDROID && !XASH_DEDICATED
	sscanf( Android_LoadID(), "%016"PRIX64, &id );
	if( id )
	{
		id ^= SYSTEM_XOR_MASK;
		ID_Check();
	}

#elif XASH_WIN32
	{
		CHAR szBuf[MAX_PATH];
		ID_GetKeyData( HKEY_CURRENT_USER, "Software\\"XASH_ENGINE_NAME"\\", "xash_id", szBuf, MAX_PATH );

		sscanf(szBuf, "%016"PRIX64, &id);
		id ^= SYSTEM_XOR_MASK;
		ID_Check();
	}
#else
	{
		const char *home = getenv( "HOME" );
		if( !COM_StringEmptyOrNULL( home ) )
		{
			FILE *cfg = fopen( va( "%s/.config/.xash_id", home ), "r" );
			if( !cfg )
				cfg = fopen( va( "%s/.local/.xash_id", home ), "r" );
			if( !cfg )
				cfg = fopen( va( "%s/.xash_id", home ), "r" );
			if( cfg )
			{
				if( fscanf( cfg, "%016"PRIX64, &id ) > 0 )
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
			sscanf( buf, "%016"PRIX64, &id );
			id ^= GAME_XOR_MASK;
			ID_Check();
		}
	}
	if( !id )
		id = ID_GenerateRawId();

#if XASH_ANDROID && !XASH_DEDICATED
	Android_SaveID( va("%016"PRIX64, id^SYSTEM_XOR_MASK ) );
#elif XASH_WIN32
	{
		CHAR Buf[MAX_PATH];
		sprintf( Buf, "%016"PRIX64, id^SYSTEM_XOR_MASK );
		ID_SetKeyData( HKEY_CURRENT_USER, "Software\\"XASH_ENGINE_NAME"\\", REG_SZ, "xash_id", Buf, Q_strlen(Buf) );
	}
#else
	{
		const char *home = getenv( "HOME" );
		if( !COM_StringEmptyOrNULL( home ) )
		{
			FILE *cfg = fopen( va( "%s/.config/.xash_id", home ), "w" );
			if( !cfg )
				cfg = fopen( va( "%s/.local/.xash_id", home ), "w" );
			if( !cfg )
				cfg = fopen( va( "%s/.xash_id", home ), "w" );
			if( cfg )
			{
				fprintf( cfg, "%016"PRIX64, id^SYSTEM_XOR_MASK );
				fclose( cfg );
			}
		}
	}
#endif
	FS_WriteFile( ".xash_id", va("%016"PRIX64, id^GAME_XOR_MASK), 16 );
#if 0
	Msg("MD5 id: %s\nRAW id:%016"PRIX64"\n", id_md5, id );
#endif
}
