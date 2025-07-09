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
#include "miniz.h"

/*
=================================================

HTTP downloader

=================================================
*/

#define MAX_HTTP_BUFFER_SIZE (BIT( 16 ))

typedef struct httpserver_s
{
	char host[256];
	int port;
	char path[MAX_SYSPATH];
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


static CVAR_DEFINE_AUTO( http_useragent, "", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "User-Agent string" );
static CVAR_DEFINE_AUTO( http_autoremove, "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "remove broken files" );
static CVAR_DEFINE_AUTO( http_timeout, "45", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "timeout for http downloader" );
static CVAR_DEFINE_AUTO( http_maxconnections, "2", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "maximum http connection number" );
static CVAR_DEFINE_AUTO( http_show_headers, "0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "show HTTP headers (request and response)" );

static int HTTP_FileFree( httpfile_t *file );
static int HTTP_FileConnect( httpfile_t *file );
static int HTTP_FileCreateSocket( httpfile_t *file );
static int HTTP_FileProcessStream( httpfile_t *file );
static int HTTP_FileQueue( httpfile_t *file );
static int HTTP_FileResolveNS( httpfile_t *file );
static int HTTP_FileSendRequest( httpfile_t *file );
static int HTTP_FileDecompress( httpfile_t *file );

/*
==============
HTTP_FreeFile

Skip to next server/file
==============
*/
static void HTTP_FreeFile( httpfile_t *file, qboolean error )
{
	char incname[MAX_SYSPATH + 64]; // plus downloaded/ plus .incomplete
	qboolean was_open = false;

	file->blocktime = 0;

	// Allways close file and socket
	if( file->file )
	{
		FS_Close( file->file );
		was_open = true;
	}

	file->file = NULL;

	if( file->socket != -1 )
	{
		closesocket( file->socket );
		http.active_count--;
	}

	file->socket = -1;

	Q_snprintf( incname, sizeof( incname ), DEFAULT_DOWNLOADED_DIRECTORY "%s.incomplete", file->path );

	if( error )
	{
		// switch to next fastdl server if present
		if( file->server && was_open )
		{
			file->server = file->server->next;

			file->pfn_process = HTTP_FileQueue; // Reset download state, HTTP_Run() will open file again
			return;
		}

		// Called because there was no servers to download, free file now
		if( http_autoremove.value == 1 ) // remove broken file
		{
			Con_Printf( S_ERROR "no servers to download %s\n", file->path );
			FS_Delete( incname );
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
			FS_Delete( incname );
		}
		else
		{
			// Success, rename and process file
			char name[MAX_SYSPATH];
			Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s", file->path );
			FS_Rename( incname, name );
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

	Con_Reportf( "HTTP: Starting download %s from %s:%d\n", file->path, file->server->host, file->server->port );
	Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s.incomplete", file->path );

	if( !( file->file = FS_Open( name, "wb+", true )))
	{
		Con_Printf( S_ERROR "HTTP: cannot open %s!\n", name );
		HTTP_FreeFile( file, true );
		return 0;
	}

	file->pfn_process = HTTP_FileResolveNS;
	file->blocktime = file->downloaded = file->lastchecksize = file->checktime = 0;
	return 1;
}

static int HTTP_FileResolveNS( httpfile_t *file )
{
	net_gai_state_t res;

	if( http.resolving )
		return 0;

	memset( &file->addr, 0, sizeof( file->addr ));

	res = NET_StringToSockaddr( file->server->host, &file->addr, true, AF_UNSPEC );

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
	uint mode = 1;
	int res;

	file->socket = socket( file->addr.ss_family, SOCK_STREAM, IPPROTO_TCP );

	if( file->socket < 0 )
	{
		Con_Printf( S_ERROR "%s: socket() returned %s\n", __func__, NET_ErrorString());
		HTTP_FreeFile( file, true );
		return 0;
	}

	if( ioctlsocket( file->socket, FIONBIO, (void *)&mode ) < 0 )
	{
		Con_Printf( S_ERROR "%s: ioctl() returned %s\n", __func__, NET_ErrorString());
		HTTP_FreeFile( file, true );
		return 0;
	}

#if XASH_LINUX

	res = fcntl( file->socket, F_GETFL, 0 );

	if( res < 0 )
	{
		Con_Printf( S_ERROR "%s: fcntl( F_GETFL ) returned %s\n", __func__, NET_ErrorString());
		HTTP_FreeFile( file, true );
		return 0;
	}

	// SOCK_NONBLOCK is not portable, so use fcntl
	if( fcntl( file->socket, F_SETFL, res | O_NONBLOCK ) < 0 )
	{
		Con_Printf( S_ERROR "%s: fcntl( F_SETFL ) returned %s\n", __func__, NET_ErrorString());
		HTTP_FreeFile( file, true );
		return 0;
	}
#endif

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

	if( !COM_CheckStringEmpty( http_useragent.string ) || !Q_strcmp( http_useragent.string, "xash3d" ))
	{
		Q_snprintf( useragent, sizeof( useragent ), "%s/%s (%s-%s; build %d; %s)",
			XASH_ENGINE_NAME, XASH_VERSION, Q_buildos( ), Q_buildarch( ), Q_buildnum( ), g_buildcommit );
	}
	else Q_strncpy( useragent, http_useragent.string, sizeof( useragent ));

	file->query_length = Q_snprintf( file->buf, sizeof( file->buf ),
		"GET %s%s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: %s\r\n"
		"Accept-Encoding: gzip, deflate\r\n"
		"Accept: */*\r\n\r\n",
		file->server->path, file->path,
		file->server->host, file->server->port,
		useragent );
	Q_strncpy( file->query_backup, file->buf, sizeof( file->query_backup ));
	file->bytes_sent = 0;
	file->header_size = 0;
	file->pfn_process = HTTP_FileSendRequest;
	return 1;
}

static int HTTP_FileSendRequest( httpfile_t *file )
{
	int res = -1;

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
	fs_offset_t len;
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

	z_stream decompress_stream;
	char name[MAX_SYSPATH];
	fs_offset_t deflate_pos;
	size_t compressed_len, decompressed_len;
	byte *data_in, *data_out;
	int zlib_result;

	g_fsapi.Seek( file->file, 0, SEEK_END );
	len = g_fsapi.Tell( file->file );

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
		uint16_t xlen;

		g_fsapi.Read( file->file, res, sizeof( res ));
		xlen = res[0] | res[1] << 16;
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

	deflate_pos = g_fsapi.Tell( file->file );
	compressed_len = len - deflate_pos;

	{
		byte data[4];

		g_fsapi.Seek( file->file, -4, SEEK_END );
		g_fsapi.Read( file->file, data, sizeof( data ));

		// FIXME: this isn't correct, as this size might be modulo 2^32
		// but we probably won't decompress files this big
		decompressed_len = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
	}

	data_in = Mem_Malloc( host.mempool, compressed_len + 1 );
	data_out = Mem_Malloc( host.mempool, decompressed_len + 1 );

	Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s", file->path );

	memset( &decompress_stream, 0, sizeof( decompress_stream ));
	decompress_stream.total_in = decompress_stream.avail_in = compressed_len;
	decompress_stream.next_in = data_in;
	decompress_stream.total_out = decompress_stream.avail_out = decompressed_len;
	decompress_stream.next_out = data_out;

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

	zlib_result = inflate( &decompress_stream, Z_NO_FLUSH );
	inflateEnd( &decompress_stream );

	if( zlib_result == Z_OK || zlib_result == Z_STREAM_END )
	{
		g_fsapi.WriteFile( name, data_out, decompressed_len );
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
	httpfile_t *cur, **prev = &http.first_file;
	sizebuf_t msg;

	MSG_Init( &msg, "DlFile", buf, sizeof( buf ));

	// clean all files marked to free
	while( 1 )
	{
		cur = *prev;

		if( !cur )
			break;

		if( cur->pfn_process != HTTP_FileFree )
		{
			prev = &cur->next;
			continue;
		}

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

		*prev = cur->next;
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
		int ret;
		int len_to_write;

		if( file->chunked && file->chunksize <= 0 )
		{
			char *begin = &file->buf[pos];

			if( begin[0] == '\r' && begin[1] == '\r' )
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

		ret = FS_Write( file->file, &file->buf[pos], len_to_write );
		if( ret != len_to_write )
		{
			// close it and go to next
			Con_Printf( S_ERROR "write failed for %s!\n", file->path );
			HTTP_FreeFile( file, true );
			return 0;
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
	while(( res = recv( curfile->socket, buf, sizeof( buf ) - curfile->header_size - 1, 0 )) > 0 )
	{
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
				char *content_encoding;
				char *transfer_encoding;

				*begin = 0; // cut string to print out response

				if( !Q_strstr( curfile->buf, "200 OK" ))
				{
					char *p;

					int num = -1;

					p = Q_strchr( curfile->buf, '\r' );
					if( !p ) p = Q_strchr( curfile->buf, '\n' );
					if( p ) *p = 0;

					// extract the error code, don't assume the response is valid HTTP
					if( !Q_strncmp( curfile->buf, "HTTP/1.", 7 ))
					{
						char tmp[4];

						Q_strncpy( tmp, curfile->buf + 9, sizeof( tmp ));
						if( Q_isdigit( tmp ))
							num = Q_atoi( tmp );
					}

					switch( num )
					{
					// TODO: handle redirects
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

				content_encoding = Q_stristr( curfile->buf, "Content-Encoding" );
				if( content_encoding ) // fetch compressed status
				{
					content_encoding += sizeof( "Content-Encoding: " ) - 1;

					if( !Q_strnicmp( content_encoding, "gzip", 4 ) && ( content_encoding[4] == '\0' || content_encoding[4] == '\n' || content_encoding[4] == '\r' ))
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
					int size;

					content_length += sizeof( "Content-Length: " ) - 1;
					size = Q_atoi( content_length );

					Con_Reportf( "HTTP: Got 200 OK! File size is %d%s\n", curfile->size, curfile->compressed ? ", compressed" : "" );

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
		curfile->blocktime += host.frametime;
		curfile->blockreason = "waiting for data";
	}

	if( res < 0 )
	{
		int err = WSAGetLastError();

		if( err != WSAEWOULDBLOCK && err != WSAEINPROGRESS )
		{
			Con_Reportf( "problem downloading %s: %s\n", curfile->path, NET_ErrorString( ));
			HTTP_FreeFile( curfile, true );
			return 0;
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
	httpfile_t *curfile;

	http.resolving = false;
	http.progress_count = 0;
	http.progress = 0;

	for( curfile = http.first_file; curfile; curfile = curfile->next )
	{
		int move_next = 1;

		while( move_next > 0 )
			move_next = curfile->pfn_process( curfile );

		if( curfile->blocktime > http_timeout.value )
		{
			Con_Printf( S_ERROR "timeout on %s (file: %s)\n", curfile->blockreason, curfile->path );
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
	httpfile_t *httpfile;

	if( !http.first_server )
	{
		Con_Printf( S_ERROR "no servers to download %s\n", path );
		return;
	}

	httpfile = Z_Calloc( sizeof( *httpfile ));

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
	httpfile_t *file;

	if( !http.first_file )
		Con_Printf( "no downloads queued\n" );

	for( file = http.first_file; file; file = file->next )
	{
		Con_Printf( "%d. %s (%d of %d)\n", i++, file->path, file->downloaded, file->size );

		if( file->server )
		{
			httpserver_t *server;
			for( server = file->server; server; server = server->next )
			{
				Con_Printf( "\thttp://%s:%d/%s%s\n", file->server->host, file->server->port,
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
	httpfile_t *file;

	for( file = http.first_file; file; file = file->next )
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
}
