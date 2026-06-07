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
#include "net_ws_private.h"
#include "net_http_tls.h"
#include "miniz.h"

/*
=================================================

HTTP downloader

=================================================
*/

#define MAX_HTTP_BUFFER_SIZE (BIT( 16 ))
#define MAX_HTTP_DECOMPRESSED_SIZE ( 64 * 1024 * 1024 )
#define MAX_HTTP_MEMORY_SIZE       ( 4 * 1024 * 1024 )

typedef struct httpserver_s
{
	char host[256];
	int port;
	char path[MAX_SYSPATH];
	qboolean secure;
	struct httpserver_s *next;
} httpserver_t;

typedef struct httpfile_s httpfile_t;
typedef int (*http_process_fn_t)( httpfile_t *file );

typedef struct httpfile_s
{
	struct httpfile_s *next;
	httpserver_t *server;
	char path[MAX_SYSPATH];
	file_t *file;
	int socket;
	int size;
	int reported_size;
	int downloaded;
	int lastchecksize;
	float checktime;
	float blocktime;
	const char *blockreason;
	qboolean process;
	qboolean got_response;
	qboolean success;
	qboolean compressed;
	qboolean chunked;
	int chunksize;
	resource_t *resource;
	http_process_fn_t pfn_process;
	struct sockaddr_storage addr;
	int redirects_followed;
	qboolean url_in_server;
	tlsctx_t *tls;

	// in-memory response mode (set by HTTP_GetToMemory)
	qboolean to_memory;
	qboolean own_server;
	byte    *mem_data;
	size_t   mem_size;
	size_t   mem_cap;
	http_memory_cb_t mem_cb;
	void    *mem_user;
	char     url[1024];

	char query_backup[1024];

	// query or response
	char buf[MAX_HTTP_BUFFER_SIZE+1];
	int header_size, query_length, bytes_sent;
} httpfile_t;

static struct http_static_s
{
	// file and server lists
	httpfile_t *first_file;
	httpserver_t *first_server;

	int active_count, progress_count;
	float progress;
	qboolean resolving;
} http;

poolhandle_t http_mempool;

static CVAR_DEFINE_AUTO( http_useragent, "", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "User-Agent string" );
static CVAR_DEFINE_AUTO( http_autoremove, "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "remove broken files" );
static CVAR_DEFINE_AUTO( http_timeout, "45", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "timeout for http downloader" );
static CVAR_DEFINE_AUTO( http_maxconnections, "2", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "maximum http connection number" );
static CVAR_DEFINE_AUTO( http_show_headers, "0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "show HTTP headers (request and response)" );
static CVAR_DEFINE_AUTO( http_max_redirects, "5", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "maximum HTTP redirects to follow per request" );

static int HTTP_FileFree( httpfile_t *file );
static int HTTP_FileConnect( httpfile_t *file );
static int HTTP_FileCreateSocket( httpfile_t *file );
static int HTTP_FileProcessStream( httpfile_t *file );
static int HTTP_FileQueue( httpfile_t *file );
static int HTTP_FileResolveNS( httpfile_t *file );
static int HTTP_FileSendRequest( httpfile_t *file );
static int HTTP_FileTlsHandshake( httpfile_t *file );
static int HTTP_FileDecompress( httpfile_t *file );
static httpserver_t *HTTP_ParseURL( const char *url_, qboolean full_path );
static qboolean HTTP_FileRedirect( httpfile_t *file, const char *location );

static const char *HTTP_DownloadPath( char *buf, size_t buflen, const char *path, qboolean incomplete )
{
	Q_snprintf( buf, buflen, "../%s" DEFAULT_DOWNLOADED_DIRECTORY_SUFFIX "/%s%s",
		GI->gamefolder, path, incomplete ? ".incomplete" : "" );
	return buf;
}

/*
==============
HTTP_FreeFile

Skip to next server/file
==============
*/
static void HTTP_FreeFile( httpfile_t *file, qboolean error )
{
	char incname[MAX_SYSPATH + 64]; // plus ../{gamedir}_downloads/ plus .incomplete
	qboolean was_open = false;

	file->blocktime = 0;

	// Allways close file and socket
	if( file->file )
	{
		FS_Close( file->file );
		was_open = true;
	}

	file->file = NULL;

	if( file->tls )
	{
		HTTP_TlsFree( file->tls );
		file->tls = NULL;
	}

	if( file->socket != -1 )
	{
		closesocket( file->socket );
		http.active_count--;
	}

	file->socket = -1;

	if( file->to_memory )
	{
		if( file->mem_cb )
			file->mem_cb( file->url, !error, error ? NULL : file->mem_data, error ? 0 : file->mem_size, file->mem_user );

		if( file->mem_data )
		{
			Mem_Free( file->mem_data );
			file->mem_data = NULL;
		}

		if( file->own_server && file->server )
		{
			Mem_Free( file->server );
			file->server = NULL;
		}

		file->pfn_process = HTTP_FileFree;
		file->success = !error;
		return;
	}

	HTTP_DownloadPath( incname, sizeof( incname ), file->path, true );

	if( error )
	{
		// switch to next fastdl server if present
		if( file->server && was_open )
		{
			httpserver_t *next = file->server->next;

			if( file->own_server )
			{
				Mem_Free( file->server );
				file->own_server = false;
			}
			file->server = next;

			file->pfn_process = HTTP_FileQueue; // Reset download state, HTTP_Run() will open file again
			return;
		}

		// Called because there was no servers to download, free file now
		if( http_autoremove.value == 1 ) // remove broken file
		{
			Con_Printf( S_ERROR "no servers to download %s\n", file->path );
			FS_AllowDirectPaths( true );
			FS_Delete( incname );
			FS_AllowDirectPaths( false );
		}
		else // autoremove disabled, keep file
		{
			// warn about trash file
			Con_Printf( S_ERROR "no servers to download %s. You may remove %s now\n", file->path, incname );
		}
	}
	else
	{
		if( file->compressed )
		{
			FS_AllowDirectPaths( true );
			FS_Delete( incname );
			FS_AllowDirectPaths( false );
		}
		else
		{
			// Success, rename and process file
			char name[MAX_SYSPATH];
			HTTP_DownloadPath( name, sizeof( name ), file->path, false );
			FS_AllowDirectPaths( true );
			FS_Rename( incname, name );
			FS_AllowDirectPaths( false );
		}
	}

	file->pfn_process = HTTP_FileFree;
	file->success = !error;
}

static int HTTP_FileFree( httpfile_t *file )
{
	return 0; // do nothing, wait for memory clean up
}

static int HTTP_FileQueue( httpfile_t *file )
{
	char name[MAX_SYSPATH];

	if( http.active_count > http_maxconnections.value )
		return 0;

	if( !file->server )
	{
		HTTP_FreeFile( file, true );
		return 0;
	}

	if( file->to_memory )
	{
		Con_Reportf( "HTTP: Starting in-memory GET %s\n", file->url );
	}
	else
	{
		Con_Reportf( "HTTP: Starting download %s from %s:%d\n", file->path, file->server->host, file->server->port );
		HTTP_DownloadPath( name, sizeof( name ), file->path, true );

		FS_AllowDirectPaths( true );
		file->file = FS_Open( name, "wb+", true );
		FS_AllowDirectPaths( false );

		if( !file->file )
		{
			Con_Printf( S_ERROR "HTTP: cannot open %s!\n", name );
			HTTP_FreeFile( file, true );
			return 0;
		}
	}

	file->pfn_process = HTTP_FileResolveNS;
	file->blocktime = file->downloaded = file->lastchecksize = file->checktime = 0;
	return 1;
}

static int HTTP_FileResolveNS( httpfile_t *file )
{
	if( http.resolving )
		return 0;

	memset( &file->addr, 0, sizeof( file->addr ));

	net_gai_state_t res = NET_StringToSockaddr( file->server->host, &file->addr, true, AF_UNSPEC );

	switch( file->addr.ss_family )
	{
	case AF_INET:
		((struct sockaddr_in *)&file->addr)->sin_port = MSG_BigShort( file->server->port );
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&file->addr)->sin6_port = MSG_BigShort( file->server->port );
		break;
	}

	if( res == NET_EAI_AGAIN )
	{
		http.resolving = true;
		return 0;
	}

	if( res == NET_EAI_NONAME )
	{
		Con_Printf( S_ERROR "failed to resolve server address for %s!\n", file->server->host );
		HTTP_FreeFile( file, true );
		return 0;
	}

	file->pfn_process = HTTP_FileCreateSocket;
	return 1;
}

static int HTTP_FileCreateSocket( httpfile_t *file )
{
	file->socket = socket( file->addr.ss_family, SOCK_STREAM, IPPROTO_TCP );

	if( file->socket < 0 )
	{
		Con_Printf( S_ERROR "%s: socket() returned %s\n", __func__, NET_ErrorString());
		HTTP_FreeFile( file, true );
		return 0;
	}

	if( !NET_MakeSocketNonBlocking( file->socket ))
	{
		Con_Printf( S_ERROR "%s: failed to make socket non-blocking, error %s\n", __func__, NET_ErrorString());
		HTTP_FreeFile( file, true );
		return 0;
	}

	http.active_count++;
	file->pfn_process = HTTP_FileConnect;
	return 1;
}

static int HTTP_FileConnect( httpfile_t *file )
{
	string useragent;
	int res = connect( file->socket, (struct sockaddr *)&file->addr, NET_SockAddrLen( &file->addr ));

	if( res < 0 )
	{
		int err = WSAGetLastError();

		switch( err )
		{
		case WSAEISCONN:
			// we're connected, proceed
			break;
		case WSAEWOULDBLOCK:
		case WSAEINPROGRESS:
		case WSAEALREADY:
			// add to the timeout
			file->blocktime += host.frametime;
			file->blockreason = "request send";
			return 0;
		default:
			// error, exit
			Con_Printf( S_ERROR "cannot connect to server: %s\n", NET_ErrorString( ));
			HTTP_FreeFile( file, true );
			return 0;
		}
	}

	file->blocktime = 0;

	if( COM_StringEmpty( http_useragent.string ) || !Q_strcmp( http_useragent.string, "xash3d" ))
	{
		Q_snprintf( useragent, sizeof( useragent ), "%s/%s (%s-%s; build %d; %s)",
			XASH_ENGINE_NAME, XASH_VERSION, Q_buildos( ), Q_buildarch( ), Q_buildnum( ), g_buildcommit );
	}
	else Q_strncpy( useragent, http_useragent.string, sizeof( useragent ));

	const char *path_suffix = file->url_in_server ? "" : file->path;

	if( file->to_memory )
	{
		file->query_length = Q_snprintf( file->buf, sizeof( file->buf ),
			"GET %s%s HTTP/1.1\r\n"
			"Host: %s:%d\r\n"
			"User-Agent: %s\r\n"
			"Accept: */*\r\n\r\n",
			file->server->path, path_suffix,
			file->server->host, file->server->port,
			useragent );
	}
	else
	{
		file->query_length = Q_snprintf( file->buf, sizeof( file->buf ),
			"GET %s%s HTTP/1.1\r\n"
			"Host: %s:%d\r\n"
			"User-Agent: %s\r\n"
			"Accept-Encoding: gzip, deflate\r\n"
			"Accept: */*\r\n\r\n",
			file->server->path, path_suffix,
			file->server->host, file->server->port,
			useragent );
	}
	Q_strncpy( file->query_backup, file->buf, sizeof( file->query_backup ));
	file->bytes_sent = 0;
	file->header_size = 0;

	if( file->server->secure )
	{
		file->tls = HTTP_TlsNew( file->socket, file->server->host );
		if( !file->tls )
		{
			Con_Printf( S_ERROR "TLS context allocation failed for %s\n", file->server->host );
			HTTP_FreeFile( file, true );
			return 0;
		}
		file->pfn_process = HTTP_FileTlsHandshake;
	}
	else file->pfn_process = HTTP_FileSendRequest;

	return 1;
}

static int HTTP_FileTlsHandshake( httpfile_t *file )
{
	int ret = HTTP_TlsHandshake( file->tls );

	if( ret == HTTP_TLS_OK )
	{
		file->blocktime = 0;
		file->pfn_process = HTTP_FileSendRequest;
		return 1;
	}

	if( ret == HTTP_TLS_WANT )
	{
		file->blocktime += host.frametime;
		file->blockreason = "TLS handshake";
		return 0;
	}

	HTTP_FreeFile( file, true );
	return 0;
}

static int HTTP_FileSendRequest( httpfile_t *file )
{
	int res;

	if( file->tls )
		res = HTTP_TlsSend( file->tls, file->buf + file->bytes_sent, file->query_length - file->bytes_sent );
	else
		res = send( file->socket, file->buf + file->bytes_sent, file->query_length - file->bytes_sent, 0 );

	if( res >= 0 )
	{
		file->bytes_sent += res;
		file->blocktime = 0;

		if( file->bytes_sent >= file->query_length )
		{
			if( http_show_headers.value )
				Con_Reportf( "HTTP: Request sent! (size %d data %s)\n", file->bytes_sent, file->buf );
			else
				Con_Reportf( "HTTP: Request sent!\n" );
			memset( file->buf, 0, sizeof( file->buf ));
			file->pfn_process = HTTP_FileProcessStream;
			return 1;
		}
	}
	else if( file->tls )
	{
		if( res != HTTP_TLS_WANT )
		{
			HTTP_FreeFile( file, true );
			return 0;
		}

		file->blocktime += host.frametime;
		file->blockreason = "request send";
	}
	else
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAENOTCONN )
		{
			Con_Printf( S_ERROR "failed to send request: %s\n", NET_ErrorString( ));
			HTTP_FreeFile( file, true );
			return 0;
		}

		file->blocktime += host.frametime;
		file->blockreason = "request send";
	}

	return 0;
}

static int HTTP_FileDecompress( httpfile_t *file )
{
#pragma pack( push, 1 )
	struct
	{
		byte magic[2];
		byte method;
		byte flags;
		uint32_t mtime; // ignored
		byte xfl;
		byte os; // can be ignored
	} hdr;
#pragma pack( pop )

	enum
	{
		GZFLG_FTEXT = BIT( 0 ), // can be ignored
		GZFLG_FHCRC = BIT( 1 ),
		GZFLG_FEXTRA = BIT( 2 ),
		GZFLG_FNAME = BIT( 3 ),
		GZFLG_FCOMMENT = BIT( 4 )
	};

	char name[MAX_SYSPATH];

	g_fsapi.Seek( file->file, 0, SEEK_END );
	fs_offset_t len = g_fsapi.Tell( file->file );

	g_fsapi.Seek( file->file, 0, SEEK_SET );
	if( g_fsapi.Read( file->file, &hdr, sizeof( hdr )) != sizeof( hdr ))
	{
		HTTP_FreeFile( file, true );
		return 0;
	}

	if( hdr.magic[0] != 0x1f && hdr.magic[1] != 0x8b && hdr.method != 0x08 )
	{
		HTTP_FreeFile( file, true );
		return 0;
	}

	if( FBitSet( hdr.flags, GZFLG_FEXTRA ))
	{
		byte res[2];

		g_fsapi.Read( file->file, res, sizeof( res ));
		uint16_t xlen = res[0] | res[1] << 16;
		g_fsapi.Seek( file->file, xlen, SEEK_CUR );
	}

	if( FBitSet( hdr.flags, GZFLG_FNAME ))
	{
		byte ch;
		do
		{
			g_fsapi.Read( file->file, &ch, sizeof( ch ));
		} while( ch != 0 );
	}

	if( FBitSet( hdr.flags, GZFLG_FCOMMENT ))
	{
		byte ch;
		do
		{
			g_fsapi.Read( file->file, &ch, sizeof( ch ));
		} while( ch != 0 );
	}

	if( FBitSet( hdr.flags, GZFLG_FHCRC ))
		g_fsapi.Seek( file->file, 2, SEEK_CUR );

	fs_offset_t deflate_pos = g_fsapi.Tell( file->file );
	size_t compressed_len = len - deflate_pos;
	size_t decompressed_len;

	{
		byte data[4];

		g_fsapi.Seek( file->file, -4, SEEK_END );
		g_fsapi.Read( file->file, data, sizeof( data ));

		// FIXME: this isn't correct, as this size might be modulo 2^32
		// but we probably won't decompress files this big
		decompressed_len = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
	}

	if( decompressed_len == 0 || decompressed_len > MAX_HTTP_DECOMPRESSED_SIZE )
	{
		Con_Printf( S_ERROR "%s: refusing to decompress %s, claimed size out of range (%zu)\n", __func__, file->path, decompressed_len );
		HTTP_FreeFile( file, true );
		return 0;
	}

	byte *data_in = Mem_Malloc( http_mempool, compressed_len + 1 );
	byte *data_out = Mem_Malloc( http_mempool, decompressed_len + 1 );

	HTTP_DownloadPath( name, sizeof( name ), file->path, false );

	z_stream decompress_stream =
	{
		.total_in = compressed_len,
		.avail_in = compressed_len,
		.next_in = data_in,
		.total_out = decompressed_len,
		.avail_out = decompressed_len,
		.next_out = data_out,
	};

	g_fsapi.Seek( file->file, deflate_pos, SEEK_SET );
	g_fsapi.Read( file->file, data_in, compressed_len );

	if( inflateInit2( &decompress_stream, -MAX_WBITS ) != Z_OK )
	{
		Con_Printf( S_ERROR "%s: inflateInit2 failed\n", __func__ );
		Mem_Free( data_in );
		Mem_Free( data_out );
		HTTP_FreeFile( file, true );
		return 0;
	}

	int zlib_result = inflate( &decompress_stream, Z_NO_FLUSH );
	inflateEnd( &decompress_stream );

	if( zlib_result == Z_OK || zlib_result == Z_STREAM_END )
	{
		FS_AllowDirectPaths( true );
		g_fsapi.WriteFile( name, data_out, decompress_stream.total_out );
		FS_AllowDirectPaths( false );
		HTTP_FreeFile( file, false );
	}
	else HTTP_FreeFile( file, true );

	Mem_Free( data_in );
	Mem_Free( data_out );

	return 1;
}

/*
========================
HTTP_ClearCustomServers
========================
*/
void HTTP_ClearCustomServers( void )
{
	if( http.first_file )
		return; // may be referenced

	while( http.first_server )
	{
		httpserver_t *tmp = http.first_server;

		http.first_server = http.first_server->next;
		Mem_Free( tmp );
	}
}


/*
===================
HTTP_AutoClean

remove files with HTTP_FREE state from list
===================
*/
static void HTTP_AutoClean( void )
{
	char buf[1024];
	httpfile_t **prev = &http.first_file;
	sizebuf_t msg;

	MSG_Init( &msg, "DlFile", buf, sizeof( buf ));

	// clean all files marked to free
	while( 1 )
	{
		httpfile_t *cur = *prev;

		if( !cur )
			break;

		if( cur->pfn_process != HTTP_FileFree )
		{
			prev = &cur->next;
			continue;
		}

		// unlink before running callbacks (in-memory cb may queue another GET, which prepends to first_file)
		*prev = cur->next;

#if !XASH_DEDICATED
		if( cur->process )
		{
			if( cur->resource && !cur->success )
			{
				MSG_BeginClientCmd( &msg, clc_stringcmd );
				MSG_WriteStringf( &msg, "dlfile %s", cur->path );
			}
			else CL_ProcessFile( cur->success, cur->path );
		}
		else
#endif
		{
			if( cur->success )
				Con_Printf( "successfully downloaded %s!\n", cur->path );
		}

		Mem_Free( cur );
	}

#if !XASH_DEDICATED
	if( MSG_GetNumBytesWritten( &msg ) > 0 )
	{
		// it's expected to be on fragments channel
		Netchan_CreateFragments( &cls.netchan, &msg );
		Netchan_FragSend( &cls.netchan );
	}
#endif
}

static int HTTP_FileSaveReceivedData( httpfile_t *file, int pos, int length )
{
	while( length > 0 )
	{
		int oldpos = pos;
		int len_to_write;

		if( file->chunked && file->chunksize <= 0 )
		{
			char *begin = &file->buf[pos];

			if( begin[0] == '\r' && begin[1] == '\n' )
				begin += 2;

			file->chunksize = Q_atoi_hex( 1, begin );

			if( !file->chunksize && begin[0] == '0' ) // actually an end, not Q_atoi being stupid
			{
				if( file->compressed )
				{
					file->blocktime = 0;
					file->pfn_process = HTTP_FileDecompress;
					return 1;
				}
				else if( file->to_memory )
				{
					HTTP_FreeFile( file, false );
					return 1;
				}
				else
				{
					fs_offset_t filelen = FS_FileLength( file->file );

					if( filelen != file->reported_size )
					{
						Con_Printf( S_ERROR "downloaded file %s size doesn't match reported size. Got %ld bytes, expected %d bytes\n", file->path, (long)filelen, file->reported_size );
						HTTP_FreeFile( file, true );
					}
					else
					{
						HTTP_FreeFile( file, false ); // success
					}

					return 1;
				}
			}

			begin = Q_strstr( begin, "\r\n" );
			if( !begin )
			{
				Con_Printf( S_ERROR "can't parse chunked transfer encoding header for %s\n", file->path );
				if( http_show_headers.value )
					Con_Reportf( "Request headers: %s", file->query_backup );
				HTTP_FreeFile( file, true );
				return 0;
			}

			pos = ( begin + 2 ) - file->buf;
			length -= pos - oldpos;

			if( length < 0 )
			{
				Con_Printf( S_ERROR "can't parse chunked transfer encoding header 2 for %s\n", file->path );
				if( http_show_headers.value )
					Con_Reportf( "Request headers: %s", file->query_backup );
				HTTP_FreeFile( file, true );
				return 0;
			}
		}

		if( file->chunked )
			len_to_write = Q_min( length, file->chunksize );
		else len_to_write = length;

		int ret;

		if( file->to_memory )
		{
			if( file->mem_size + len_to_write > MAX_HTTP_MEMORY_SIZE )
			{
				Con_Printf( S_ERROR "%s: response too large (>%d bytes)\n", file->url, MAX_HTTP_MEMORY_SIZE );
				HTTP_FreeFile( file, true );
				return 0;
			}

			if( file->mem_size + len_to_write > file->mem_cap )
			{
				size_t newcap = file->mem_cap ? file->mem_cap * 2 : 4096;

				while( newcap < file->mem_size + len_to_write )
					newcap *= 2;

				file->mem_data = Mem_Realloc( http_mempool, file->mem_data, newcap );
				file->mem_cap = newcap;
			}

			memcpy( file->mem_data + file->mem_size, &file->buf[pos], len_to_write );
			file->mem_size += len_to_write;
			ret = len_to_write;
		}
		else
		{
			ret = FS_Write( file->file, &file->buf[pos], len_to_write );
			if( ret != len_to_write )
			{
				// close it and go to next
				Con_Printf( S_ERROR "write failed for %s!\n", file->path );
				HTTP_FreeFile( file, true );
				return 0;
			}
		}

		length -= len_to_write;
		file->chunksize -= len_to_write;

		pos += ret;
		file->downloaded += ret;
		file->lastchecksize += ret;
	}

	return 1;
}

/*
===================
HTTP_ProcessStream

process incoming data
===================
*/
static int HTTP_FileProcessStream( httpfile_t *curfile )
{
	char buf[sizeof( curfile->buf )];
	char *begin = 0;
	int res;

	// if we got there, we are receiving data
	while( 1 )
	{
		int rlen = sizeof( buf ) - curfile->header_size - 1;

		if( curfile->tls )
			res = HTTP_TlsRecv( curfile->tls, buf, rlen );
		else
			res = recv( curfile->socket, buf, rlen, 0 );

		if( res <= 0 )
			break;

		curfile->blocktime = 0;

		if( !curfile->got_response ) // Response still not received
		{
			if( curfile->header_size + res + 1 > sizeof( buf ))
			{
				Con_Reportf( S_ERROR "Header too big, the size is %d\n", curfile->header_size );
				HTTP_FreeFile( curfile, true );
				return 0;
			}

			memcpy( curfile->buf + curfile->header_size, buf, res );
			curfile->buf[curfile->header_size + res] = 0;
			begin = Q_strstr( curfile->buf, "\r\n\r\n" );

			if( begin ) // Got full header
			{
				char *content_length;
				char *transfer_encoding;

				*begin = 0; // cut string to print out response

				int num = -1;

				// don't assume the response is valid HTTP
				if( !Q_strncmp( curfile->buf, "HTTP/1.", 7 ))
				{
					char tmp[4];

					Q_strncpy( tmp, curfile->buf + 9, sizeof( tmp ));
					if( Q_isdigit( tmp ))
						num = Q_atoi( tmp );
				}

				if( num != 200 )
				{
					if( num == 301 || num == 302 || num == 303 || num == 307 || num == 308 )
					{
						char *loc = Q_stristr( curfile->buf, "Location:" );

						if( loc )
						{
							loc += sizeof( "Location:" ) - 1;
							while( *loc == ' ' || *loc == '\t' )
								loc++;

							char *eol = Q_strchr( loc, '\r' );
							if( !eol ) eol = Q_strchr( loc, '\n' );
							if( eol ) *eol = 0;

							if( HTTP_FileRedirect( curfile, loc ))
								return 1;
						}
					}

					char *p = Q_strchr( curfile->buf, '\r' );
					if( !p ) p = Q_strchr( curfile->buf, '\n' );
					if( p ) *p = 0;

					switch( num )
					{
					case 404:
						Con_Printf( S_ERROR "%s: file not found\n", curfile->path );
						break;
					default:
						Con_Printf( S_ERROR "%s: bad response: %s\n", curfile->path, curfile->buf );
						if( http_show_headers.value )
							Con_Printf( "Request headers: %s", curfile->query_backup );
						break;
					}

					HTTP_FreeFile( curfile, true );
					return 0;
				}

				char *content_encoding = Q_stristr( curfile->buf, "Content-Encoding" );
				if( content_encoding ) // fetch compressed status
				{
					content_encoding += sizeof( "Content-Encoding: " ) - 1;

					if( curfile->to_memory )
					{
						// in-memory mode never advertises gzip and has no decompressor
						Con_Printf( S_ERROR "%s: server sent Content-Encoding for an in-memory request\n", curfile->url );
						HTTP_FreeFile( curfile, true );
						return 0;
					}
					else if( !Q_strnicmp( content_encoding, "gzip", 4 ) && ( content_encoding[4] == '\0' || content_encoding[4] == '\n' || content_encoding[4] == '\r' ))
						curfile->compressed = true;
					else
					{
						Con_Printf( S_ERROR "%s: bad Content-Encoding: %s\n", curfile->path, content_encoding );
						if( http_show_headers.value )
							Con_Printf( "Request headers: %s", curfile->query_backup );
						HTTP_FreeFile( curfile, true );
						return 0;
					}
				}

				if(( transfer_encoding = Q_stristr( curfile->buf, "Transfer-Encoding: chunked" )))
				{
					curfile->size = -1;
					curfile->chunked = true;

					Con_Reportf( "HTTP: Got 200 OK! Chunked transfer encoding%s\n", curfile->compressed ? ", compressed" : "" );
				}
				else if(( content_length = Q_stristr( curfile->buf, "Content-Length: " ) ))
				{
					content_length += sizeof( "Content-Length: " ) - 1;
					int size = Q_atoi( content_length );

					Con_Reportf( "HTTP: Got 200 OK! File size is %d%s\n", size, curfile->compressed ? ", compressed" : "" );

					if( !curfile->compressed )
					{
						if( ( curfile->size != -1 ) && ( curfile->size != size )) // check size if specified, not used
							Con_Reportf( S_WARN "Server reports wrong file size for %s!\n", curfile->path );
					}

					curfile->size = size;
					curfile->header_size = 0;
				}

				if( curfile->size == -1 && !curfile->chunked )
				{
					// Usually fastdl's reports file size if link is correct
					Con_Printf( S_ERROR "file size is unknown, refusing download!\n" );
					HTTP_FreeFile( curfile, true );
					return 0;
				}

				if( http_show_headers.value )
					Con_Reportf( "Response headers: %s\n", curfile->buf );

				curfile->got_response = true; // got response, let's start download
				begin += 4;

				if( res - ( begin - curfile->buf ) > 0 )
				{
					if( !HTTP_FileSaveReceivedData( curfile, begin - curfile->buf, res - ( begin - curfile->buf )))
						return 0;
				}
			}
			else
				curfile->header_size += res;
		}
		else if( res > 0 )
		{
			memcpy( curfile->buf, buf, res );

			// data download
			if( !HTTP_FileSaveReceivedData( curfile, 0, res ))
				return 0;

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

	if( curfile->size > 0 )
	{
		http.progress += (float)curfile->downloaded / curfile->size;
		http.progress_count++;

		if( curfile->downloaded >= curfile->size )
		{
			// chunked files are finalized in FileSaveReceivedData
			if( curfile->compressed && !curfile->chunked )
			{
				curfile->pfn_process = HTTP_FileDecompress;
				curfile->success = true;
			}
			else
			{
				HTTP_FreeFile( curfile, false ); // success
			}
			return 0;
		}
	}

	if( res == 0 )
	{
		Con_Printf( S_ERROR "connection closed prematurely for %s\n", curfile->to_memory ? curfile->url : curfile->path );
		HTTP_FreeFile( curfile, true );
		return 0;
	}

	if( res < 0 )
	{
		if( curfile->tls )
		{
			if( res != HTTP_TLS_WANT )
			{
				HTTP_FreeFile( curfile, true );
				return 0;
			}
		}
		else
		{
			int err = WSAGetLastError();

			if( err != WSAEWOULDBLOCK && err != WSAEINPROGRESS )
			{
				Con_Reportf( "problem downloading %s: %s\n", curfile->path, NET_ErrorString( ));
				HTTP_FreeFile( curfile, true );
				return 0;
			}
		}

		curfile->blocktime += host.frametime;

		if( !curfile->got_response )
			curfile->blockreason = "receiving header";
		else curfile->blockreason = "receiving data";
		return 0;
	}

	curfile->checktime += host.frametime;
	return 0; // don't block
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
	http.resolving = false;
	http.progress_count = 0;
	http.progress = 0;

	for( httpfile_t *curfile = http.first_file; curfile; curfile = curfile->next )
	{
		int move_next = 1;

		while( move_next > 0 )
			move_next = curfile->pfn_process( curfile );

		if( curfile->blocktime > http_timeout.value )
		{
			Con_Printf( S_ERROR "timeout on %s (file: %s)\n", curfile->blockreason,
				curfile->to_memory ? curfile->url : curfile->path );
			HTTP_FreeFile( curfile, true );
		}
	}

	// update progress
	if( !Host_IsDedicated() && http.progress_count != 0 )
		Cvar_SetValue( "scr_download", http.progress/http.progress_count * 100 );

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
	if( COM_CheckNastyPath( path ))
	{
		Con_Printf( S_ERROR "%s: refused to download %s, nasty path\n", __func__, path );
		return;
	}

	if( Q_strpbrk( path, "\r\n" ))
	{
		Con_Printf( S_ERROR "%s: refused to download, path contains CRLF\n", __func__ );
		return;
	}

	if( !http.first_server )
	{
		Con_Printf( S_ERROR "no servers to download %s\n", path );
		return;
	}

	httpfile_t *httpfile = Mem_Calloc( http_mempool, sizeof( *httpfile ));

	Con_Reportf( "File %s queued to download\n", path );

	httpfile->resource = res;
	httpfile->size = size;
	httpfile->reported_size = size;
	httpfile->socket = -1;
	Q_strncpy( httpfile->path, path, sizeof( httpfile->path ));

	httpfile->pfn_process = HTTP_FileQueue;
	httpfile->server = http.first_server;
	httpfile->process = process;

	httpfile->next = http.first_file;
	http.first_file = httpfile;
}

/*
===================
HTTP_GetToMemory

One-shot async GET. The full response body is collected into a heap buffer
and handed to the callback when the request completes (success or failure).
===================
*/
qboolean HTTP_GetToMemory( const char *url, http_memory_cb_t cb, void *userdata )
{
	if( Q_strpbrk( url, "\r\n" ))
	{
		Con_Printf( S_ERROR "%s: refused, URL contains CRLF\n", __func__ );
		return false;
	}

	httpserver_t *server = HTTP_ParseURL( url, true );

	if( !server )
	{
		Con_Printf( S_ERROR "%s: \"%s\" is not a valid URL\n", __func__, url );
		return false;
	}

	httpfile_t *httpfile = Mem_Calloc( http_mempool, sizeof( *httpfile ));

	httpfile->size = -1;
	httpfile->reported_size = -1;
	httpfile->socket = -1;
	httpfile->server = server;
	httpfile->own_server = true;
	httpfile->to_memory = true;
	httpfile->mem_cb = cb;
	httpfile->mem_user = userdata;
	httpfile->pfn_process = HTTP_FileQueue;
	Q_strncpy( httpfile->url, url, sizeof( httpfile->url ));
	// file->path is empty; the full path lives in server->path

	httpfile->next = http.first_file;
	http.first_file = httpfile;

	return true;
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
static httpserver_t *HTTP_ParseURL( const char *url_, qboolean full_path )
{
	qboolean secure = false;
	const char *url = NULL;

	if( !Q_strnicmp( url_, "https://", 8 ))
	{
		url = url_ + 8;
		secure = true;
	}
	else if( !Q_strnicmp( url_, "http://", 7 ))
	{
		url = url_ + 7;
	}

	if( !url )
		return NULL;

	if( secure && !HTTP_TlsAvailable( ))
	{
		Con_Printf( S_ERROR "HTTPS not available, can't fetch %s\n", url_ );
		return NULL;
	}

	httpserver_t *server = Mem_Calloc( http_mempool, sizeof( httpserver_t ));
	int i = 0;

	server->secure = secure;

	while( *url && ( *url != ':' ) && ( *url != '/' ) && ( *url != '\r' ) && ( *url != '\n' ))
	{
		if( i >= sizeof( server->host ) - 1 )
		{
			Mem_Free( server );
			return NULL;
		}

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
		server->port = secure ? 443 : 80;

	i = 0;

	// leave room for the optional trailing '/' and the '\0'
	while( *url && ( *url != '\r' ) && ( *url != '\n' ))
	{
		if( i >= sizeof( server->path ) - 2 )
		{
			Mem_Free( server );
			return NULL;
		}

		server->path[i++] = *url++;
	}

	// fastdl base URLs are appended to per-file paths and must end with a slash;
	// full URLs (one-shot GETs) are used as-is.
	if( !full_path && ( i == 0 || server->path[i-1] != '/' ))
		server->path[i++] = '/';
	server->path[i] = 0;
	server->next = NULL;

	return server;
}

static qboolean HTTP_FileRedirect( httpfile_t *file, const char *location )
{
	if( !location || !*location )
		return false;

	if( file->redirects_followed >= http_max_redirects.value )
	{
		Con_Printf( S_ERROR "too many redirects for %s\n", file->to_memory ? file->url : file->path );
		return false;
	}

	// silent http -> https upgrade is OK; reject downgrade
	qboolean target_secure = !Q_strnicmp( location, "https://", 8 );
	qboolean target_plain = !Q_strnicmp( location, "http://", 7 );

	if( !target_secure && !target_plain )
	{
		Con_Printf( S_ERROR "redirect to non-absolute URL not supported: %s\n", location );
		return false;
	}

	if( file->server->secure && !target_secure )
	{
		Con_Printf( S_ERROR "refusing https -> http redirect: %s\n", location );
		return false;
	}

	httpserver_t *newserver = HTTP_ParseURL( location, true );
	if( !newserver )
	{
		Con_Printf( S_ERROR "redirect target %s is not a valid URL\n", location );
		return false;
	}

	Con_Reportf( "HTTP: redirect %s -> %s\n", file->to_memory ? file->url : file->path, location );

	// tear down current connection but keep file/mem buffers
	if( file->tls )
	{
		HTTP_TlsFree( file->tls );
		file->tls = NULL;
	}

	if( file->socket != -1 )
	{
		closesocket( file->socket );
		http.active_count--;
		file->socket = -1;
	}

	if( file->own_server && file->server )
		Mem_Free( file->server );
	file->server = newserver;
	file->own_server = true;

	// truncate the partial download; we'll restart from the new server
	if( file->file )
	{
		g_fsapi.Seek( file->file, 0, SEEK_SET );
	}
	file->mem_size = 0;
	file->downloaded = 0;
	file->lastchecksize = 0;
	file->header_size = 0;
	file->bytes_sent = 0;
	file->got_response = false;
	file->compressed = false;
	file->chunked = false;
	file->chunksize = 0;
	file->size = file->reported_size;

	file->url_in_server = true;
	if( file->to_memory )
		Q_strncpy( file->url, location, sizeof( file->url ));

	file->redirects_followed++;
	file->blocktime = 0;
	file->pfn_process = HTTP_FileResolveNS;

	return true;
}

/*
=======================
HTTP_AddCustomServer
=======================
*/
void HTTP_AddCustomServer( const char *url )
{
	if( Q_strpbrk( url, "\r\n" ))
	{
		Con_Printf( S_ERROR "%s: refused, URL contains CRLF\n", __func__ );
		return;
	}

	httpserver_t *server = HTTP_ParseURL( url, false );

	if( !server )
	{
		Con_Printf( S_ERROR "\"%s\" is not valid url!\n", url );
		return;
	}

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
	while( http.first_file )
	{
		httpfile_t *file = http.first_file;

		http.first_file = http.first_file->next;

		if( file->file )
			FS_Close( file->file );

		if( file->tls )
			HTTP_TlsFree( file->tls );

		if( file->socket != -1 )
			closesocket( file->socket );

		if( file->to_memory )
		{
			if( file->mem_cb )
				file->mem_cb( file->url, false, NULL, 0, file->mem_user );
			if( file->mem_data )
				Mem_Free( file->mem_data );
			if( file->own_server && file->server )
				Mem_Free( file->server );
		}

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

	http.first_file->server = NULL;
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
	int i = 0;

	if( !http.first_file )
		Con_Printf( "no downloads queued\n" );

	for( httpfile_t *file = http.first_file; file; file = file->next )
	{
		Con_Printf( "%d. %s (%d of %d)\n", i++, file->path, file->downloaded, file->size );

		if( file->server )
		{
			for( httpserver_t *server = file->server; server; server = server->next )
			{
				Con_Printf( "\t%s://%s:%d/%s%s\n", file->server->secure ? "https" : "http",
					file->server->host, file->server->port,
					file->server->path, file->path );
			}
		}
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
	for( httpfile_t *file = http.first_file; file; file = file->next )
		file->process = false;
}

/*
=============
HTTP_Init
=============
*/
void HTTP_Init( void )
{
	http.first_file = NULL;
	http_mempool = Mem_AllocPool( "HTTP" );

	HTTP_TlsInit();

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
	Cvar_RegisterVariable( &http_show_headers );
	Cvar_RegisterVariable( &http_max_redirects );
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

	HTTP_TlsShutdown();

	Mem_FreePool( &http_mempool );
}
