/*
sv_log.c - server logging in multiplayer
Copyright (C) 2017 Uncle Mike

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
#include "server.h"

void Log_Open( void )
{
	time_t		ltime;
	struct tm		*today;
	char		szFileBase[ MAX_OSPATH ];
	char		szTestFile[ MAX_OSPATH ];
	file_t		*fp = NULL;
	const char		*temp;
	int		i;

	if( !svs.log.active )
		return;

	if( sv_log_onefile.value && svs.log.file )
		return;

	if( !mp_logfile.value )
	{
		Con_Printf( "Server logging data to console.\n" );
		return;
	}

	Log_Close();

	// Find a new log file slot
	time( &ltime );
	today = localtime( &ltime );
	temp = Cvar_VariableString( "logsdir" );

	if( COM_CheckString( temp ) && !Q_strchr( temp, ':' ) && !Q_strstr( temp, ".." ))
		Q_snprintf( szFileBase, sizeof( szFileBase ), "%s/L%02i%02i", temp, today->tm_mon + 1, today->tm_mday );
	else Q_snprintf( szFileBase, sizeof( szFileBase ), "logs/L%02i%02i", today->tm_mon + 1, today->tm_mday );

	for ( i = 0; i < 1000; i++ )
	{
		Q_snprintf( szTestFile, sizeof( szTestFile ), "%s%03i.log", szFileBase, i );

		if( FS_FileExists( szTestFile, false ))
			continue;

		fp = FS_Open( szTestFile, "w", true );
		if( fp )
		{
			Con_Printf( "Server logging data to file %s\n", szTestFile );
		}
		else
		{
			i = 1000;
		}
		break;
	}

	if( i == 1000 )
	{
		Con_Printf( "Unable to open logfiles under %s\nLogging disabled\n", szFileBase );
		svs.log.active = false;
		return;
	}

	if( fp ) svs.log.file = fp;
	Log_Printf( "Log file started (file \"%s\") (game \"%s\") (version \"%i/" XASH_VERSION "/%d\")\n",
	szTestFile, Info_ValueForKey( svs.serverinfo, "*gamedir" ), PROTOCOL_VERSION, Q_buildnum() );
}

void Log_Close( void )
{
	if( svs.log.file )
	{
		Log_Printf( "Log file closed\n" );
		FS_Close( svs.log.file );
	}
	svs.log.file = NULL;
}

/*
==================
Log_Printf

Prints a frag log message to the server's frag log file, console, and possible a UDP port.
==================
*/
void Log_Printf( const char *fmt, ... )
{
	va_list		argptr;
	static char	string[1024];
	char		*p;
	time_t		ltime;
	struct tm	*today;
	int		len;

	if( !svs.log.active )
		return;

	time( &ltime );
	today = localtime( &ltime );

	len = Q_snprintf( string, sizeof( string ), "%02i/%02i/%04i - %02i:%02i:%02i: ",
		today->tm_mon+1, today->tm_mday, 1900 + today->tm_year, today->tm_hour, today->tm_min, today->tm_sec );

	p = string + len;

	va_start( argptr, fmt );
	Q_vsnprintf( p, sizeof( string ) - len, fmt, argptr );
	va_end( argptr );

	if( svs.log.net_log )
		Netchan_OutOfBandPrint( NS_SERVER, svs.log.net_address, "log %s", string );

	if( svs.log.active && ( svs.maxclients > 1 || sv_log_singleplayer.value != 0.0f ))
	{
		// echo to server console
		if( mp_logecho.value )
			Con_Printf( "%s", string );

		// echo to log file
		if( svs.log.file && mp_logfile.value )
			FS_Printf( svs.log.file, "%s", string );
	}
}

static void Log_PrintServerCvar( const char *var_name, const char *var_value, const void *unused2, void *unused3 )
{
	Log_Printf( "Server cvar \"%s\" = \"%s\"\n", var_name, var_value );
}

/*
==================
Log_PrintServerVars

==================
*/
void Log_PrintServerVars( void )
{
	if( !svs.log.active )
		return;

	Log_Printf( "Server cvars start\n" );
	Cvar_LookupVars( FCVAR_SERVER, NULL, NULL, (setpair_t)Log_PrintServerCvar );
	Log_Printf( "Server cvars end\n" );
}

/*
====================
SV_SetLogAddress_f

====================
*/
void SV_SetLogAddress_f( void )
{
	const char *s;
	int port;
	string addr;

	if( svs.log.net_log && Cmd_Argc() == 2 && !Q_strcmp( Cmd_Argv( 1 ), "off" )) 
	{
		svs.log.net_log = false;
		memset( &svs.log.net_address, 0, sizeof( netadr_t ));

		Con_Printf( "logaddress: disabled.\n" );

		return;
	}

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "logaddress < ip port | off >\n" );

		if( svs.log.active && svs.log.net_log )
			Con_Printf( "current: %s\n", NET_AdrToString( svs.log.net_address ));

		return;
	}

	port = Q_atoi( Cmd_Argv( 2 ));
	if( !port )
	{
		Con_Printf( "logaddress: must specify a valid port\n" );
		return;
	}

	s = Cmd_Argv( 1 );
	if( !COM_CheckString( s ))
	{
		Con_Printf( "logaddress: unparseable address\n" );
		return;
	}

	Q_snprintf( addr, sizeof( addr ), "%s:%i", s, port );
	if( !NET_StringToAdr( addr, &svs.log.net_address ))
	{
		Con_Printf( "logaddress: unable to resolve %s\n", addr );
		return;
	}

	svs.log.net_log = true;
	Con_Printf( "logaddress: %s\n", NET_AdrToString( svs.log.net_address ));
}

/*
====================
SV_ServerLog_f

====================
*/
void SV_ServerLog_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "log < on|off >\n" );

		if( svs.log.active )
			Con_Printf( "currently logging\n" );
		else Con_Printf( "not currently logging\n" );
		return;
	}

	if( !Q_stricmp( Cmd_Argv( 1 ), "off" ))
	{
		if( svs.log.active )
		{
			Log_Close();
			svs.log.active = false;
		}
	}
	else if( !Q_stricmp( Cmd_Argv( 1 ), "on" ))
	{
		svs.log.active = true;
		Log_Open();
	}
	else
	{
		Con_Printf( "log: unknown parameter %s\n", Cmd_Argv( 1 ));
	}

	return;
}
