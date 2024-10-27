/*
net_http.c - HTTP client implementation
Copyright (C) 2024 mittorn
Copyright (C) 2024 Alibek Omarov

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
#include "client.h" // ConnectionProgress
#include "netchan.h"
#include "xash3d_mathlib.h"
#include "ipv6text.h"
#include "net_ws_private.h"

/*
=================================================

HTTP downloader

=================================================
*/

#define MAX_HTTP_BUFFER_SIZE (BIT( 13 ))

typedef struct httpserver_s
{
	char host[256];
	int port;
	char path[MAX_SYSPATH];
	qboolean needfree;
	struct httpserver_s *next;

} httpserver_t;

enum connectionstate
{
	HTTP_QUEUE = 0,
	HTTP_OPENED,
	HTTP_SOCKET,
	HTTP_NS_RESOLVED,
	HTTP_CONNECTED,
	HTTP_REQUEST,
	HTTP_REQUEST_SENT,
	HTTP_RESPONSE_RECEIVED,
	HTTP_FREE
};

typedef struct httpfile_s
{
	struct httpfile_s *next;
	httpserver_t *server;
	char path[MAX_SYSPATH];
	file_t *file;
	int socket;
	int size;
	int downloaded;
	int lastchecksize;
	float checktime;
	float blocktime;
	int id;
	enum connectionstate state;
	qboolean process;
	resource_t *resource;

	string query_backup;

	// query or response
	char buf[MAX_HTTP_BUFFER_SIZE+1];
	int header_size, query_length, bytes_sent;
} httpfile_t;

static struct http_static_s
{
	// file and server lists
	httpfile_t *first_file, *last_file;
	httpserver_t *first_server, *last_server;
} http;


static CVAR_DEFINE_AUTO( http_useragent, "", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "User-Agent string" );
static CVAR_DEFINE_AUTO( http_autoremove, "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "remove broken files" );
static CVAR_DEFINE_AUTO( http_timeout, "45", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "timeout for http downloader" );
static CVAR_DEFINE_AUTO( http_maxconnections, "2", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "maximum http connection number" );
static CVAR_DEFINE_AUTO( http_show_request_header, "0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "show headers of the HTTP request" );

/*
========================
HTTP_ClearCustomServers
========================
*/
void HTTP_ClearCustomServers( void )
{
	if( http.first_file )
		return; // may be referenced

	while( http.first_server && http.first_server->needfree )
	{
		httpserver_t *tmp = http.first_server;

		http.first_server = http.first_server->next;
		Mem_Free( tmp );
	}
}

/*
==============
HTTP_FreeFile

Skip to next server/file
==============
*/
static void HTTP_FreeFile( httpfile_t *file, qboolean error )
{
	char incname[256];

	// Allways close file and socket
	if( file->file )
		FS_Close( file->file );

	file->file = NULL;

	if( file->socket != -1 )
		closesocket( file->socket );

	file->socket = -1;

	Q_snprintf( incname, sizeof( incname ), DEFAULT_DOWNLOADED_DIRECTORY "%s.incomplete", file->path );
	if( error )
	{
		// Switch to next fastdl server if present
		if( file->server && ( file->state > HTTP_QUEUE ) && ( file->state != HTTP_FREE ))
		{
			file->server = file->server->next;
			file->state = HTTP_QUEUE; // Reset download state, HTTP_Run() will open file again
			return;
		}

		// Called because there was no servers to download, free file now
		if( http_autoremove.value == 1 ) // remove broken file
			FS_Delete( incname );
		else // autoremove disabled, keep file
			Con_Printf( "cannot download %s from any server. "
				"You may remove %s now\n", file->path, incname ); // Warn about trash file

#if !XASH_DEDICATED
		if( file->process )
		{
			if( file->resource )
			{
				char buf[1024];
				sizebuf_t msg;

				MSG_Init( &msg, "DlFile", buf, sizeof( buf ));
				MSG_BeginClientCmd( &msg, clc_stringcmd );
				MSG_WriteStringf( &msg, "dlfile %s", file->path );

				Netchan_CreateFragments( &cls.netchan, &msg );
				Netchan_FragSend( &cls.netchan );
			}
			else
			{
				CL_ProcessFile( false, file->path ); // Process file, increase counter
			}
		}
#endif // !XASH_DEDICATED
	}
	else
	{
		// Success, rename and process file
		char name[256];

		Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s", file->path );
		FS_Rename( incname, name );

#if !XASH_DEDICATED
		if( file->process )
			CL_ProcessFile( true, name );
		else
#endif
		{
			Con_Printf( "successfully downloaded %s, processing disabled!\n", name );
		}
	}

	file->state = HTTP_FREE;
}

/*
===================
HTTP_AutoClean

remove files with HTTP_FREE state from list
===================
*/
static void HTTP_AutoClean( void )
{
	httpfile_t *curfile, *prevfile = 0;

	// clean all files marked to free
	for( curfile = http.first_file; curfile; curfile = curfile->next )
	{
		if( curfile->state != HTTP_FREE )
		{
			prevfile = curfile;
			continue;
		}

		if( curfile == http.first_file )
		{
			http.first_file = http.first_file->next;
			Mem_Free( curfile );
			curfile = http.first_file;
			if( !curfile )
				break;
			continue;
		}

		if( prevfile )
			prevfile->next = curfile->next;
		Mem_Free( curfile );
		curfile = prevfile;
		if( !curfile )
			break;
	}
	http.last_file = prevfile;
}

/*
===================
HTTP_ProcessStream

process incoming data
===================
*/
static qboolean HTTP_ProcessStream( httpfile_t *curfile )
{
	char buf[sizeof( curfile->buf )];
	char *begin = 0;
	int res;

	if( curfile->header_size >= sizeof( buf ))
	{
		Con_Reportf( S_ERROR "Header too big, the size is %d\n", curfile->header_size );
		HTTP_FreeFile( curfile, true );
		return false;
	}

	while( ( res = recv( curfile->socket, buf, sizeof( buf ) - curfile->header_size, 0 )) > 0) // if we got there, we are receiving data
	{
		curfile->blocktime = 0;

		if( curfile->state < HTTP_RESPONSE_RECEIVED ) // Response still not received
		{
			memcpy( curfile->buf + curfile->header_size, buf, res );
			curfile->buf[curfile->header_size + res] = 0;
			begin = Q_strstr( curfile->buf, "\r\n\r\n" );

			if( begin ) // Got full header
			{
				int cutheadersize = begin - curfile->buf + 4; // after that begin of data
				char *content_length_line;

				if( !Q_strstr( curfile->buf, "200 OK" ))
				{
					*begin = 0; // cut string to print out response
					begin = Q_strchr( curfile->buf, '\r' );

					if( !begin ) begin = Q_strchr( curfile->buf, '\n' );
					if( begin )
						*begin = 0;

					Con_Printf( S_ERROR "%s: bad response: %s\n", curfile->path, curfile->buf );

					if( http_show_request_header.value )
						Con_Printf( "Request headers: %s", curfile->query_backup );
					HTTP_FreeFile( curfile, true );
					return false;
				}

				// print size
				content_length_line = Q_stristr( curfile->buf, "Content-Length: " );
				if( content_length_line )
				{
					int size;

					content_length_line += sizeof( "Content-Length: " ) - 1;
					size = Q_atoi( content_length_line );

					Con_Reportf( "HTTP: Got 200 OK! File size is %d\n", size );

					if( ( curfile->size != -1 ) && ( curfile->size != size )) // check size if specified, not used
						Con_Reportf( S_WARN "Server reports wrong file size for %s!\n", curfile->path );

					curfile->size = size;
					curfile->header_size = 0;
				}

				if( curfile->size == -1 )
				{
					// Usually fastdl's reports file size if link is correct
					Con_Printf( S_ERROR "file size is unknown, refusing download!\n" );
					HTTP_FreeFile( curfile, true );
					return false;
				}

				curfile->state = HTTP_RESPONSE_RECEIVED; // got response, let's start download
				begin += 4;

				// Write remaining message part
				if( res - cutheadersize - curfile->header_size > 0 )
				{
					int ret = FS_Write( curfile->file, begin, res - cutheadersize - curfile->header_size );

					if( ret != res - cutheadersize - curfile->header_size ) // could not write file
					{
						// close it and go to next
						Con_Printf( S_ERROR "write failed for %s!\n", curfile->path );
						HTTP_FreeFile( curfile, true );
						return false;
					}
					curfile->downloaded += ret;
				}
			}
			else
				curfile->header_size += res;
		}
		else if( res > 0 )
		{
			// data download
			int ret = FS_Write( curfile->file, buf, res );

			if ( ret != res )
			{
				// close it and go to next
				Con_Printf( S_ERROR "write failed for %s!\n", curfile->path );
				curfile->state = HTTP_FREE;
				HTTP_FreeFile( curfile, true );
				return false;
			}

			curfile->downloaded += ret;
			curfile->lastchecksize += ret;

			// as after it will run in same frame
			if( curfile->checktime > 5 )
			{
				float speed = (float)curfile->lastchecksize / ( 5.0f * 1024 );

				curfile->checktime = 0;
				Con_Reportf( "download speed %f KB/s\n", speed );
				curfile->lastchecksize = 0;
			}
		}
	}
	curfile->checktime += host.frametime;

	return true;
}

/*
==============
HTTP_Run

Download next file block of each active file
Call every frame
==============
*/
void HTTP_Run( void )
{
	httpfile_t *curfile;
	int iActiveCount = 0;
	int iProgressCount = 0;
	float flProgress = 0;
	qboolean fResolving = false;

	for( curfile = http.first_file; curfile; curfile = curfile->next )
	{
		struct sockaddr_storage addr;

		if( curfile->state == HTTP_FREE )
			continue;

		if( curfile->state == HTTP_QUEUE )
		{
			char name[MAX_SYSPATH];

			if( iActiveCount > http_maxconnections.value )
				continue;

			if( !curfile->server )
			{
				Con_Printf( S_ERROR "no servers to download %s!\n", curfile->path );
				HTTP_FreeFile( curfile, true );
				break;
			}

			Con_Reportf( "HTTP: Starting download %s from %s\n", curfile->path, curfile->server->host );
			Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s.incomplete", curfile->path );

			curfile->file = FS_Open( name, "wb", true );

			if( !curfile->file )
			{
				Con_Printf( S_ERROR "HTTP: cannot open %s!\n", name );
				HTTP_FreeFile( curfile, true );
				break;
			}

			curfile->state = HTTP_OPENED;
			curfile->blocktime = 0;
			curfile->downloaded = 0;
			curfile->lastchecksize = 0;
			curfile->checktime = 0;
		}

		iActiveCount++;

		if( curfile->state < HTTP_SOCKET ) // Socket is not created
		{
			dword mode;

			curfile->socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

			// Now set non-blocking mode
			// You may skip this if not supported by system,
			// but download will lock engine, maybe you will need to add manual returns
			mode = 1;
			ioctlsocket( curfile->socket, FIONBIO, (void*)&mode );
#if XASH_LINUX
			// SOCK_NONBLOCK is not portable, so use fcntl
			fcntl( curfile->socket, F_SETFL, fcntl( curfile->socket, F_GETFL, 0 ) | O_NONBLOCK );
#endif
			curfile->state = HTTP_SOCKET;
		}

		if( curfile->state < HTTP_NS_RESOLVED )
		{
			net_gai_state_t res;
			char hostport[MAX_VA_STRING];

			if( fResolving )
				continue;

			Q_snprintf( hostport, sizeof( hostport ), "%s:%d", curfile->server->host, curfile->server->port );

			res = NET_StringToSockaddr( hostport, &addr, true, AF_INET );

			if( res == NET_EAI_AGAIN )
			{
				fResolving = true;
				continue;
			}

			if( res == NET_EAI_NONAME )
			{
				Con_Printf( S_ERROR "failed to resolve server address for %s!\n", curfile->server->host );
				HTTP_FreeFile( curfile, true ); // Cannot connect
				break;
			}
			curfile->state = HTTP_NS_RESOLVED;
		}

		if( curfile->state < HTTP_CONNECTED ) // Connection not enstabilished
		{
			int res = connect( curfile->socket, (struct sockaddr*)&addr, NET_SockAddrLen( &addr ) );

			if( res )
			{
				if( WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK ) // Should give EWOOLDBLOCK if try recv too soon
					curfile->state = HTTP_CONNECTED;
				else
				{
					Con_Printf( S_ERROR "cannot connect to server: %s\n", NET_ErrorString( ));
					HTTP_FreeFile( curfile, true ); // Cannot connect
					break;
				}
				continue; // skip to next file
			}
			curfile->state = HTTP_CONNECTED;
		}

		if( curfile->state < HTTP_REQUEST ) // Request not formatted
		{
			string useragent;

			if( !COM_CheckStringEmpty( http_useragent.string ) || !Q_strcmp( http_useragent.string, "xash3d" ))
			{
				Q_snprintf( useragent, sizeof( useragent ), "%s/%s (%s-%s; build %d; %s)",
					XASH_ENGINE_NAME, XASH_VERSION, Q_buildos( ), Q_buildarch( ), Q_buildnum( ), Q_buildcommit( ));
			}
			else
			{
				Q_strncpy( useragent, http_useragent.string, sizeof( useragent ));
			}

			curfile->query_length = Q_snprintf( curfile->buf, sizeof( curfile->buf ),
				"GET %s%s HTTP/1.0\r\n"
				"Host: %s:%d\r\n"
				"User-Agent: %s\r\n"
				"Accept: */*\r\n\r\n", curfile->server->path,
				curfile->path, curfile->server->host, curfile->server->port, useragent );
			Q_strncpy( curfile->query_backup, curfile->buf, sizeof( curfile->query_backup ));
			curfile->header_size = 0;
			curfile->bytes_sent = 0;
			curfile->state = HTTP_REQUEST;
		}

		if( curfile->state < HTTP_REQUEST_SENT ) // Request not sent
		{
			qboolean wait = false;

			while( curfile->bytes_sent < curfile->query_length )
			{
				int res = send( curfile->socket, curfile->buf + curfile->bytes_sent, curfile->query_length - curfile->bytes_sent, 0 );

				if( res < 0 )
				{
					if( WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAENOTCONN )
					{
						Con_Printf( S_ERROR "failed to send request: %s\n", NET_ErrorString( ));
						HTTP_FreeFile( curfile, true );
						wait = true;
						break;
					}
					// blocking while waiting connection
					// increase counter when blocking
					curfile->blocktime += host.frametime;
					wait = true;

					if( curfile->blocktime > http_timeout.value )
					{
						Con_Printf( S_ERROR "timeout on request send:\n%s\n", curfile->buf );
						HTTP_FreeFile( curfile, true );
						break;
					}
					break;
				}
				else
				{
					curfile->bytes_sent += res;
					curfile->blocktime = 0;
				}
			}

			if( wait )
				continue;

			Con_Reportf( "HTTP: Request sent!\n");
			memset( curfile->buf, 0, sizeof( curfile->buf ));
			curfile->state = HTTP_REQUEST_SENT;
		}

		if( !HTTP_ProcessStream( curfile ))
			break;

		if( curfile->size > 0 )
		{
			flProgress += (float)curfile->downloaded / curfile->size;
			iProgressCount++;
		}

		if( curfile->size > 0 && curfile->downloaded >= curfile->size )
		{
			HTTP_FreeFile( curfile, false ); // success
			break;
		}
		else if(( WSAGetLastError( ) != WSAEWOULDBLOCK ) && ( WSAGetLastError( ) != WSAEINPROGRESS ))
			Con_Reportf( "problem downloading %s:\n%s\n", curfile->path, NET_ErrorString( ));
		else
			curfile->blocktime += host.frametime;

		if( curfile->blocktime > http_timeout.value )
		{
			Con_Printf( S_ERROR "timeout on receiving data!\n");
			HTTP_FreeFile( curfile, true );
			break;
		}
	}

	// update progress
	if( !Host_IsDedicated() && iProgressCount != 0 )
		Cvar_SetValue( "scr_download", flProgress/iProgressCount * 100 );

	HTTP_AutoClean();
}

/*
===================
HTTP_AddDownload

Add new download to end of queue
===================
*/
void HTTP_AddDownload( const char *path, int size, qboolean process, resource_t *res )
{
	httpfile_t *httpfile = Z_Calloc( sizeof( httpfile_t ));

	Con_Reportf( "File %s queued to download\n", path );

	httpfile->resource = res;
	httpfile->size = size;
	httpfile->downloaded = 0;
	httpfile->socket = -1;
	Q_strncpy ( httpfile->path, path, sizeof( httpfile->path ));

	if( http.last_file )
	{
		// Add next to last download
		httpfile->id = http.last_file->id + 1;
		http.last_file->next= httpfile;
		http.last_file = httpfile;
	}
	else
	{
		// It will be the only download
		httpfile->id = 0;
		http.last_file = http.first_file = httpfile;
	}

	httpfile->file = NULL;
	httpfile->next = NULL;
	httpfile->state = HTTP_QUEUE;
	httpfile->server = http.first_server;
	httpfile->process = process;
}

/*
===============
HTTP_Download_f

Console wrapper
===============
*/
static void HTTP_Download_f( void )
{
	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "download <gamedir_path>\n");
		return;
	}

	HTTP_AddDownload( Cmd_Argv( 1 ), -1, false, NULL );
}

/*
==============
HTTP_ParseURL
==============
*/
static httpserver_t *HTTP_ParseURL( const char *url )
{
	httpserver_t *server;
	int i;

	url = Q_strstr( url, "http://" );

	if( !url )
		return NULL;

	url += 7;
	server = Z_Calloc( sizeof( httpserver_t ));
	i = 0;

	while( *url && ( *url != ':' ) && ( *url != '/' ) && ( *url != '\r' ) && ( *url != '\n' ))
	{
		if( i > sizeof( server->host ))
			return NULL;

		server->host[i++] = *url++;
	}

	server->host[i] = 0;

	if( *url == ':' )
	{
		server->port = Q_atoi( ++url );

		while( *url && ( *url != '/' ) && ( *url != '\r' ) && ( *url != '\n' ))
			url++;
	}
	else
		server->port = 80;

	i = 0;

	while( *url && ( *url != '\r' ) && ( *url != '\n' ))
	{
		if( i > sizeof( server->path ) - 1 )
			return NULL;

		server->path[i++] = *url++;
	}

	if( i == 0 || server->path[i-1] != '/' )
		server->path[i++] = '/';
	server->path[i] = 0;
	server->next = NULL;
	server->needfree = false;

	return server;
}

/*
=======================
HTTP_AddCustomServer
=======================
*/
void HTTP_AddCustomServer( const char *url )
{
	httpserver_t *server = HTTP_ParseURL( url );

	if( !server )
	{
		Con_Printf( S_ERROR "\"%s\" is not valid url!\n", url );
		return;
	}

	server->needfree = true;
	server->next = http.first_server;
	http.first_server = server;
}

/*
=======================
HTTP_AddCustomServer_f
=======================
*/
static void HTTP_AddCustomServer_f( void )
{
	if( Cmd_Argc() == 2 )
	{
		HTTP_AddCustomServer( Cmd_Argv( 1 ));
	}
	else
	{
		Con_Printf( S_USAGE "http_addcustomserver <url>\n" );
	}
}

/*
============
HTTP_Clear_f

Clear all queue
============
*/
static void HTTP_Clear_f( void )
{
	http.last_file = NULL;

	while( http.first_file )
	{
		httpfile_t *file = http.first_file;

		http.first_file = http.first_file->next;

		if( file->file )
			FS_Close( file->file );

		if( file->socket != -1 )
			closesocket( file->socket );

		Mem_Free( file );
	}
}

/*
==============
HTTP_Cancel_f

Stop current download, skip to next file
==============
*/
static void HTTP_Cancel_f( void )
{
	if( !http.first_file )
		return;

	http.first_file->state = HTTP_FREE;
	HTTP_FreeFile( http.first_file, true );
}

/*
=============
HTTP_Skip_f

Stop current download, skip to next server
=============
*/
static void HTTP_Skip_f( void )
{
	if( http.first_file )
		HTTP_FreeFile( http.first_file, true );
}

/*
=============
HTTP_List_f

Print all pending downloads to console
=============
*/
static void HTTP_List_f( void )
{
	httpfile_t *file = http.first_file;

	while( file )
	{
		if ( file->server )
			Con_Printf ( "\t%d %d http://%s:%d/%s%s %d\n", file->id, file->state,
				file->server->host, file->server->port, file->server->path,
				file->path, file->downloaded );
		else
			Con_Printf ( "\t%d %d (no server) %s\n", file->id, file->state, file->path );

		file = file->next;
	}
}

/*
================
HTTP_ResetProcessState

When connected to new server, all old files should not increase counter
================
*/
void HTTP_ResetProcessState( void )
{
	httpfile_t *file = http.first_file;

	while( file )
	{
		file->process = false;
		file = file->next;
	}
}

/*
=============
HTTP_Init
=============
*/
void HTTP_Init( void )
{
	char *serverfile, *line, token[1024];

	http.last_server = NULL;

	http.first_file = http.last_file = NULL;

	Cmd_AddRestrictedCommand( "http_download", HTTP_Download_f, "add file to download queue" );
	Cmd_AddRestrictedCommand( "http_skip", HTTP_Skip_f, "skip current download server" );
	Cmd_AddRestrictedCommand( "http_cancel", HTTP_Cancel_f, "cancel current download" );
	Cmd_AddRestrictedCommand( "http_clear", HTTP_Clear_f, "cancel all downloads" );
	Cmd_AddRestrictedCommand( "http_list", HTTP_List_f, "list all queued downloads" );
	Cmd_AddCommand( "http_addcustomserver", HTTP_AddCustomServer_f, "add custom fastdl server");

	Cvar_RegisterVariable( &http_useragent );
	Cvar_RegisterVariable( &http_autoremove );
	Cvar_RegisterVariable( &http_timeout );
	Cvar_RegisterVariable( &http_maxconnections );
	Cvar_RegisterVariable( &http_show_request_header );

	// Read servers from fastdl.txt
	line = serverfile = (char *)FS_LoadFile( "fastdl.txt", 0, false );

	if( serverfile )
	{
		while(( line = COM_ParseFile( line, token, sizeof( token ))))
		{
			httpserver_t *server = HTTP_ParseURL( token );

			if( !server )
				continue;

			if( !http.last_server )
				http.last_server = http.first_server = server;
			else
			{
				http.last_server->next = server;
				http.last_server = server;
			}
		}

		Mem_Free( serverfile );
	}
}

/*
====================
HTTP_Shutdown
====================
*/
void HTTP_Shutdown( void )
{
	HTTP_Clear_f();

	while( http.first_server )
	{
		httpserver_t *tmp = http.first_server;

		http.first_server = http.first_server->next;
		Mem_Free( tmp );
	}

	http.last_server = NULL;
}
