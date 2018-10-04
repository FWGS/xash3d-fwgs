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
	char		*temp;
	int		i;

	if( !svs.log.active )
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

	if( temp && Q_strlen( temp ) > 0 && !Q_strstr( temp, ":" ) && !Q_strstr( temp, ".." ))
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
	Log_Printf( "Log file started (file \"%s\") (game \"%s\") (version \"%i/%s/%d\")\n",
	szTestFile, Info_ValueForKey( SV_Serverinfo(), "*gamedir" ), PROTOCOL_VERSION, XASH_VERSION, Q_buildnum() );
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
	struct tm		*today;

	if( !svs.log.active )
		return;

	time( &ltime );
	today = localtime( &ltime );

	Q_snprintf( string, sizeof( string ), "%02i/%02i/%04i - %02i:%02i:%02i: ",
		today->tm_mon+1, today->tm_mday, 1900 + today->tm_year, today->tm_hour, today->tm_min, today->tm_sec );

	p = string + Q_strlen( string );

	va_start( argptr, fmt );
	Q_vsnprintf( p, sizeof( string ) - Q_strlen( string ), fmt, argptr );
	va_end( argptr );

	if( svs.log.net_log )
		Netchan_OutOfBandPrint( NS_SERVER, svs.log.net_address, "log %s", string );

	if( svs.log.active && svs.maxclients > 1 )
	{
		// echo to server console
		if( mp_logecho.value ) 
			Con_Printf( "%s", string );

		// echo to log file
		if( svs.log.file && mp_logfile.value )
			FS_Printf( svs.log.file, "%s", string );
	}
}

static void Log_PrintServerCvar( const char *var_name, const char *var_value, const char *unused2, void *unused3 )
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
	Cvar_LookupVars( FCVAR_SERVER, NULL, NULL, Log_PrintServerCvar );
	Log_Printf( "Server cvars end\n" );
}

/*
====================
SV_ServerLog_f

====================
*/
qboolean SV_ServerLog_f( sv_client_t *cl )
{
	if( svs.maxclients <= 1 )
		return false;

	if( Cmd_Argc() != 2 )
	{
		SV_ClientPrintf( cl, "usage: log < on|off >\n" );

		if( svs.log.active )
			SV_ClientPrintf( cl, "currently logging\n" );
		else SV_ClientPrintf( cl, "not currently logging\n" );
		return true;
	}

	if( !Q_stricmp( Cmd_Argv( 1 ), "off" ))
	{
		if( svs.log.active )
			Log_Close();
	}
	else if( !Q_stricmp( Cmd_Argv( 1 ), "on" ))
	{
		svs.log.active = true;
		Log_Open();
	}
	else
	{
		SV_ClientPrintf( cl, "log: unknown parameter %s\n", Cmd_Argv( 1 ));
	}

	return true;
}