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
 * Descompressao HTTP: zlib do sistema quando existir (gzip nativo),
 * miniz (vendorizado) como reserva e, por ultimo, o metodo antigo.
 */
#if defined(HAVE_SYSTEM_ZLIB) && HAVE_SYSTEM_ZLIB
#define HTTP_HAVE_ZLIB 1
#include <zlib.h>
#else
#define HTTP_HAVE_ZLIB 0
#include "miniz.h"
#endif

#include <bzlib.h>

/*
 * TLS para FastDL em https (mkl.bz e afins). Sem openssl o downloader
 * continua funcionando apenas em http:// puro.
 */
#if defined(HAVE_SYSTEM_OPENSSL) && HAVE_SYSTEM_OPENSSL
#define HTTP_HAVE_TLS 1
#include <openssl/ssl.h>
#include <openssl/err.h>
#else
#define HTTP_HAVE_TLS 0
#endif

#ifndef XASH_USE_CURL_DOWNLOADER
#define XASH_USE_CURL_DOWNLOADER 0
#endif
#if XASH_USE_CURL_DOWNLOADER
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#endif

// ====================== URL ENCODE PARA NOMES COM Ç/Ã/É (FIX PALHAÇO) ======================
static void HTTP_UrlEncode( const char *src, char *dst, size_t dstsize )
{
	static const char *hex = "0123456789ABCDEF";
	size_t i = 0;

	while( *src && i < dstsize - 1 )
	{
		unsigned char c = (unsigned char)*src;

		// caracteres seguros (mantém / . - _ e letras/números)
		if( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '-' || c == '_' )
		{
			dst[i++] = *src;
		}
		else
		{
			if( i + 3 >= dstsize ) break; // evita overflow
			dst[i++] = '%';
			dst[i++] = hex[c >> 4];
			dst[i++] = hex[c & 0x0F];
		}
		src++;
	}
	dst[i] = '\0';
}

// ====================== NOVA FUNÇÃO (anti-double-slash + limpeza) ======================
static void HTTP_NormalizePath( char *path )
{
	if( !path || !*path ) return;

	// converte \ para / (por segurança)
	for( char *p = path; *p; p++ )
		if( *p == '\\' ) *p = '/';

	// remove slashes duplicados (// → /)
	char *src = path, *dst = path;
	while( *src )
	{
		if( *src == '/' && dst > path && *(dst-1) == '/' )
		{
			src++;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
}

/*
=================================================

HTTP downloader

=================================================
*/

#define MAX_HTTP_BUFFER_SIZE (BIT( 16 ))

typedef struct httpserver_s
{
	char scheme[8];
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
	char url_path[MAX_SYSPATH]; // absolute redirect path, already URL-encoded when set
	file_t *file;
#if XASH_USE_CURL_DOWNLOADER
	CURL *curl;
	struct curl_slist *curl_headers;
	qboolean curl_active;
	qboolean curl_header_seen;
	CURLcode curl_result;
	long curl_http_code;
	curl_off_t curl_dltotal;
	curl_off_t curl_dlnow;
	char curl_error[CURL_ERROR_SIZE];
#endif
	int socket;
#if HTTP_HAVE_TLS
	void *ssl;                   // SSL* para FastDL em https
#endif
	int size;
	int reported_size;
	int downloaded;
	int resume_from;
	int lastchecksize;
	float checktime;
	float blocktime;
	const char *blockreason;
	qboolean process;
	qboolean got_response;
	qboolean success;
	qboolean compressed;
	qboolean chunked;
	qboolean close_delimited;
	qboolean no_retry;
	int chunksize;
	int chunk_crlf;
	char chunk_header[64];
	int chunk_header_len;
	int retries;
	int redirects;
	int suffix_try;              // 0 original, 1 .bz2, 2 .gz
	char orig_path[MAX_SYSPATH]; // nome final, sem o sufixo de compressao
	resource_t *resource;
	http_process_fn_t pfn_process;
	struct sockaddr_storage addr;
	httpserver_t redirect_server;

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
static CVAR_DEFINE_AUTO( http_timeout, "45", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "stall timeout for http downloader" );
static CVAR_DEFINE_AUTO( http_maxconnections, "12", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "maximum simultaneous HTTP downloads" );
static CVAR_DEFINE_AUTO( http_max_retries, "2", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "retries per HTTP server before fallback/next server" );
static CVAR_DEFINE_AUTO( http_max_redirects, "5", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "maximum HTTP redirects per file" );
static CVAR_DEFINE_AUTO( http_show_headers, "0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "show HTTP headers (request and response)" );
static CVAR_DEFINE_AUTO( http_decompress, "0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "descompressor: 0 auto, 1 zlib, 2 miniz, 3 legado" );
static CVAR_DEFINE_AUTO( http_fastdl_suffix, "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "tenta arquivo.bz2/.gz quando o arquivo nao existe no fastdl" );

#if XASH_USE_CURL_DOWNLOADER
static CVAR_DEFINE_AUTO( http_curl, "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "use libcurl backend for HTTP/FastDL downloads" );
static CVAR_DEFINE_AUTO( http_connect_timeout, "10", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "libcurl connect timeout in seconds" );
static CVAR_DEFINE_AUTO( http_low_speed_limit, "256", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "minimum HTTP speed in bytes/sec before libcurl stall timeout" );
static CVAR_DEFINE_AUTO( http_low_speed_time, "20", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "seconds below http_low_speed_limit before libcurl timeout" );
static CVAR_DEFINE_AUTO( http_max_active_requests, "12", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "maximum active libcurl downloads" );
#endif

static int HTTP_FileFree( httpfile_t *file );
static void HTTP_FreeFile( httpfile_t *file, qboolean error );
static int HTTP_FileConnect( httpfile_t *file );
static int HTTP_FileCreateSocket( httpfile_t *file );
static int HTTP_FileProcessStream( httpfile_t *file );
static int HTTP_FileQueue( httpfile_t *file );
static int HTTP_FileResolveNS( httpfile_t *file );
static int HTTP_FileSendRequest( httpfile_t *file );
static int HTTP_FileDecompress( httpfile_t *file );
#if HTTP_HAVE_TLS
static int HTTP_FileTlsHandshake( httpfile_t *file );
static int HTTP_NetSend( httpfile_t *file, const void *buf, int len );
static int HTTP_NetRecv( httpfile_t *file, void *buf, int len );
#endif
static void HTTP_SelectProcessor( httpfile_t *file );
static qboolean HTTP_ReopenIncomplete( httpfile_t *file, qboolean truncate );
static qboolean HTTP_RestartTransfer( httpfile_t *file );
static qboolean HTTP_ParseURLParts( const char *url_, char *host, size_t hostsize, int *port, char *path, size_t pathsize );
static void HTTP_BuildRequestURI( httpfile_t *file, char *dst, size_t dstsize );
#if XASH_USE_CURL_DOWNLOADER
static int HTTP_FileCurlActive( httpfile_t *file );
static qboolean HTTP_CurlStartFile( httpfile_t *file );
static void HTTP_CurlCleanupFile( httpfile_t *file );
static void HTTP_CurlPump( void );
static void HTTP_CurlGlobalShutdown( void );
static qboolean HTTP_ArchiveExtractMember( const char *archive_path, const char *member_name, const char *out_path );
#endif

static void HTTP_CloseSocketOnly( httpfile_t *file )
{
#if HTTP_HAVE_TLS
	if( file->ssl )
	{
		SSL_shutdown( (SSL *)file->ssl );
		SSL_free( (SSL *)file->ssl );
		file->ssl = NULL;
	}
#endif
	if( file->socket != -1 )
	{
		closesocket( file->socket );
		if( http.active_count > 0 )
			http.active_count--;
	}
	file->socket = -1;
}

static void HTTP_ResetResponseState( httpfile_t *file )
{
	file->got_response = false;
	file->compressed = false;
	file->chunked = false;
	file->close_delimited = false;
	file->no_retry = false;
	file->chunksize = 0;
	file->chunk_crlf = 0;
	file->chunk_header_len = 0;
	file->header_size = 0;
	file->query_length = 0;
	file->bytes_sent = 0;
	file->blocktime = 0;
	file->blockreason = "queued";
#if XASH_USE_CURL_DOWNLOADER
	file->curl_header_seen = false;
	file->curl_http_code = 0;
	file->curl_dltotal = 0;
	file->curl_dlnow = 0;
	file->curl_error[0] = '\0';
#endif
	memset( file->buf, 0, sizeof( file->buf ));
}

static qboolean HTTP_GetHeaderValue( const char *headers, const char *name, char *out, size_t outsize )
{
	const char *p = headers;
	size_t namelen = strlen( name );

	if( !outsize )
		return false;

	out[0] = '\0';

	while( p && *p )
	{
		const char *line = p;
		const char *end = Q_strchr( line, '\n' );
		size_t i = 0;

		if( !end )
			end = line + strlen( line );

		while( line < end && ( *line == '\r' || *line == '\n' ))
			line++;

		if( !Q_strnicmp( line, name, namelen ) && line[namelen] == ':' )
		{
			const char *val = line + namelen + 1;

			while( val < end && ( *val == ' ' || *val == '\t' ))
				val++;

			while( val < end && *val != '\r' && *val != '\n' && i < outsize - 1 )
				out[i++] = *val++;

			out[i] = '\0';
			return true;
		}

		p = ( *end ) ? end + 1 : end;
	}

	return false;
}

static int HTTP_ParseStatusCode( const char *headers )
{
	const char *p;

	if( Q_strncmp( headers, "HTTP/", 5 ))
		return -1;

	p = Q_strchr( headers, ' ' );
	if( !p )
		return -1;

	return Q_atoi( p + 1 );
}

static int HTTP_ParseContentRangeTotal( const char *value )
{
	const char *slash = Q_strchr( value, '/' );

	if( !slash || slash[1] == '*' || slash[1] == '\0' )
		return -1;

	return Q_atoi( slash + 1 );
}

static void HTTP_MakeIncompleteName( httpfile_t *file, char *out, size_t outsize )
{
	Q_snprintf( out, outsize, DEFAULT_DOWNLOADED_DIRECTORY "%s.incomplete", file->path );
	HTTP_NormalizePath( out );
}

static qboolean HTTP_ReopenIncomplete( httpfile_t *file, qboolean truncate )
{
	char incname[MAX_SYSPATH];

	HTTP_MakeIncompleteName( file, incname, sizeof( incname ));

	if( file->file )
	{
		FS_Close( file->file );
		file->file = NULL;
	}

	if( truncate )
	{
		FS_Delete( incname );
		file->downloaded = 0;
		file->resume_from = 0;
	}

	if( !( file->file = FS_Open( incname, ( truncate || file->downloaded <= 0 || !FS_FileExists( incname, true )) ? "wb+" : "rb+", true ) ) )
	{
		Con_Printf( S_ERROR "HTTP: cannot open %s!\n", incname );
		return false;
	}

	if( file->downloaded > 0 )
		FS_Seek( file->file, file->downloaded, SEEK_SET );

	file->resume_from = file->downloaded;
	return true;
}

static qboolean HTTP_RestartTransfer( httpfile_t *file )
{
	HTTP_CloseSocketOnly( file );
	HTTP_ResetResponseState( file );
	file->pfn_process = HTTP_FileResolveNS;
	return true;
}

static qboolean HTTP_ParseURLParts( const char *url_, char *host, size_t hostsize, int *port, char *path, size_t pathsize )
{
	const char *url;
	size_t i = 0;

	if( !url_ || !host || !path || !port )
		return false;

	if( !Q_strnicmp( url_, "http://", 7 ))
		url = url_ + 7;
	else if( !Q_strnicmp( url_, "https://", 8 ))
	{
#if XASH_USE_CURL_DOWNLOADER || HTTP_HAVE_TLS
		url = url_ + 8;
#else
		Con_Printf( S_ERROR "HTTP: HTTPS FastDL URL '%s' needs a libcurl/OpenSSL backend; falling back to legacy download.\n", url_ );
		return false;
#endif
	}
	else return false;

	while( *url && *url != ':' && *url != '/' && *url != '\r' && *url != '\n' )
	{
		if( i + 1 >= hostsize )
			return false;
		host[i++] = *url++;
	}
	host[i] = '\0';

	if( !host[0] )
		return false;

	if( *url == ':' )
	{
		*port = Q_atoi( ++url );
		while( *url && *url != '/' && *url != '\r' && *url != '\n' )
			url++;
	}
	else
	{
		if( !Q_strnicmp( url_, "https://", 8 ))
			*port = 443;
		else *port = 80;
	}

	if( *port <= 0 || *port > 65535 )
		return false;

	i = 0;
	if( *url != '/' )
	{
		if( pathsize < 2 )
			return false;
		path[i++] = '/';
	}
	else
	{
		while( *url && *url != '\r' && *url != '\n' )
		{
			if( i + 1 >= pathsize )
				return false;
			path[i++] = *url++;
		}
	}

	path[i] = '\0';
	HTTP_NormalizePath( path );
	return true;
}

static void HTTP_BuildRequestURI( httpfile_t *file, char *dst, size_t dstsize )
{
	char encoded_path[MAX_SYSPATH];
	const char *path;

	if( file->url_path[0] )
	{
		Q_strncpy( dst, file->url_path, dstsize );
		return;
	}

	HTTP_UrlEncode( file->path, encoded_path, sizeof( encoded_path ));

	path = encoded_path;
	while( *path == '/' )
		path++;

	Q_snprintf( dst, dstsize, "%s%s", file->server->path, path );
	if( dst[0] != '/' )
	{
		char tmp[MAX_SYSPATH];
		Q_strncpy( tmp, dst, sizeof( tmp ));
		Q_snprintf( dst, dstsize, "/%s", tmp );
	}
}

#if XASH_USE_CURL_DOWNLOADER

static CURLM *http_curl_multi;
static int http_curl_running;

/*
=============
HTTP_FileCurlActive

A curl-backed transfer is progressed by HTTP_CurlPump from HTTP_Run.
This per-file state only contributes progress to scr_download.
=============
*/
static int HTTP_FileCurlActive( httpfile_t *file )
{
	if( file && file->size > 0 )
	{
		http.progress += (float)file->downloaded / file->size;
		http.progress_count++;
	}

	return 0;
}

static void HTTP_CurlGlobalInit( void )
{
	if( !http_curl_multi )
	{
		curl_global_init( CURL_GLOBAL_DEFAULT );
		http_curl_multi = curl_multi_init();
	}
}

static void HTTP_CurlCleanupFile( httpfile_t *file )
{
	if( !file )
		return;

	if( file->curl && http_curl_multi )
		curl_multi_remove_handle( http_curl_multi, file->curl );

	if( file->curl )
	{
		curl_easy_cleanup( file->curl );
		file->curl = NULL;
	}

	if( file->curl_headers )
	{
		curl_slist_free_all( file->curl_headers );
		file->curl_headers = NULL;
	}

	if( file->curl_active )
	{
		if( http.active_count > 0 )
			http.active_count--;
		file->curl_active = false;
	}
}

static void HTTP_CurlGlobalShutdown( void )
{
	if( http_curl_multi )
	{
		curl_multi_cleanup( http_curl_multi );
		http_curl_multi = NULL;
	}

	curl_global_cleanup();
}

static const char *HTTP_ServerScheme( const httpserver_t *server )
{
	if( server && server->scheme[0] )
		return server->scheme;

	return "http";
}

static void HTTP_CurlBuildURL( httpfile_t *file, char *dst, size_t dstsize )
{
	char encoded_path[MAX_SYSPATH * 2];
	const char *path;
	const char *scheme = HTTP_ServerScheme( file->server );

	if( file->url_path[0] )
	{
		Q_snprintf( dst, dstsize, "%s://%s:%d%s", scheme, file->server->host, file->server->port, file->url_path );
		return;
	}

	HTTP_UrlEncode( file->path, encoded_path, sizeof( encoded_path ));

	path = encoded_path;
	while( *path == '/' )
		path++;

	Q_snprintf( dst, dstsize, "%s://%s:%d%s%s", scheme, file->server->host, file->server->port, file->server->path, path );
}

static void HTTP_CurlCloseDownloadFile( httpfile_t *file )
{
	if( file && file->file )
	{
		FS_Close( file->file );
		file->file = NULL;
	}
}

static size_t HTTP_CurlWrite( char *ptr, size_t size, size_t nmemb, void *userdata )
{
	httpfile_t *file = (httpfile_t *)userdata;
	size_t total = size * nmemb;
	int written;

	if( !file || !file->file || total <= 0 )
		return 0;

	written = FS_Write( file->file, ptr, total );
	if( written != (int)total )
		return 0;

	file->downloaded += written;
	file->lastchecksize += written;
	file->blocktime = 0;

	return total;
}

static size_t HTTP_CurlHeader( char *ptr, size_t size, size_t nmemb, void *userdata )
{
	httpfile_t *file = (httpfile_t *)userdata;
	size_t total = size * nmemb;
	char line[128];
	size_t copylen;

	if( !file || total < 5 )
		return total;

	copylen = Q_min( total, sizeof( line ) - 1 );
	memcpy( line, ptr, copylen );
	line[copylen] = '\0';

	if( !Q_strncmp( line, "HTTP/", 5 ) )
	{
		const char *space = Q_strchr( line, ' ' );
		long code = space ? Q_atoi( space + 1 ) : 0;

		file->curl_http_code = code;
		file->curl_header_seen = true;

		/*
		 * Integrity guard for resume:
		 * if a server ignores Range and sends 200 OK, truncate before
		 * the body callback appends anything to the old partial.
		 */
		if( code == 200 && file->resume_from > 0 )
		{
			Con_Reportf( S_WARN "HTTP/CURL: server ignored Range for %s, restarting from zero\n", file->path );
			HTTP_ReopenIncomplete( file, true );
		}
	}

	return total;
}

static int HTTP_CurlProgress( void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow )
{
	httpfile_t *file = (httpfile_t *)clientp;

	(void)ultotal;
	(void)ulnow;

	if( !file )
		return 0;

	file->curl_dltotal = dltotal;
	file->curl_dlnow = dlnow;

	if( dltotal > 0 )
		file->size = file->resume_from + (int)dltotal;

	file->blocktime = 0;
	return 0;
}

static qboolean HTTP_CurlStartFile( httpfile_t *file )
{
	char url[MAX_SYSPATH * 4];
	string useragent;
	fs_offset_t existing_size = 0;
	char incname[MAX_SYSPATH];

	if( !file || !file->server )
		return false;

	if( http.active_count >= (int)http_max_active_requests.value )
		return false;

	HTTP_CurlGlobalInit();

	if( !http_curl_multi )
		return false;

	HTTP_MakeIncompleteName( file, incname, sizeof( incname ));

	if( FS_FileExists( incname, true ) )
		existing_size = FS_FileSize( incname, true );

	if( existing_size < 0 )
		existing_size = 0;

	file->downloaded = (int)existing_size;
	file->resume_from = file->downloaded;

	if( !HTTP_ReopenIncomplete( file, false ))
		return false;

	if( COM_StringEmpty( http_useragent.string ) || !Q_strcmp( http_useragent.string, "xash3d" ))
	{
		Q_snprintf( useragent, sizeof( useragent ), "%s/%s (%s-%s; build %d; %s)",
			XASH_ENGINE_NAME, XASH_VERSION, Q_buildos( ), Q_buildarch( ), Q_buildnum( ), g_buildcommit );
	}
	else Q_strncpy( useragent, http_useragent.string, sizeof( useragent ));

	HTTP_CurlBuildURL( file, url, sizeof( url ) );

	file->curl = curl_easy_init();
	if( !file->curl )
	{
		HTTP_CurlCloseDownloadFile( file );
		return false;
	}

	file->curl_error[0] = '\0';
	file->curl_result = CURLE_OK;
	file->curl_http_code = 0;
	file->curl_dltotal = 0;
	file->curl_dlnow = 0;
	file->curl_header_seen = false;

	curl_easy_setopt( file->curl, CURLOPT_URL, url );
	curl_easy_setopt( file->curl, CURLOPT_PRIVATE, file );
	curl_easy_setopt( file->curl, CURLOPT_USERAGENT, useragent );

	curl_easy_setopt( file->curl, CURLOPT_WRITEFUNCTION, HTTP_CurlWrite );
	curl_easy_setopt( file->curl, CURLOPT_WRITEDATA, file );
	curl_easy_setopt( file->curl, CURLOPT_HEADERFUNCTION, HTTP_CurlHeader );
	curl_easy_setopt( file->curl, CURLOPT_HEADERDATA, file );

	curl_easy_setopt( file->curl, CURLOPT_NOPROGRESS, 0L );
	curl_easy_setopt( file->curl, CURLOPT_XFERINFOFUNCTION, HTTP_CurlProgress );
	curl_easy_setopt( file->curl, CURLOPT_XFERINFODATA, file );

	curl_easy_setopt( file->curl, CURLOPT_ERRORBUFFER, file->curl_error );
	curl_easy_setopt( file->curl, CURLOPT_FOLLOWLOCATION, 1L );
	curl_easy_setopt( file->curl, CURLOPT_MAXREDIRS, (long)http_max_redirects.value );
	curl_easy_setopt( file->curl, CURLOPT_CONNECTTIMEOUT, (long)http_connect_timeout.value );
	curl_easy_setopt( file->curl, CURLOPT_LOW_SPEED_LIMIT, (long)http_low_speed_limit.value );
	curl_easy_setopt( file->curl, CURLOPT_LOW_SPEED_TIME, (long)http_low_speed_time.value );
	curl_easy_setopt( file->curl, CURLOPT_FAILONERROR, 0L );
	curl_easy_setopt( file->curl, CURLOPT_NOSIGNAL, 1L );

	/*
	 * FastDL resume is safer with identity encoding. It avoids resuming
	 * inside a gzip/deflate content-encoded stream.
	 */
	curl_easy_setopt( file->curl, CURLOPT_ACCEPT_ENCODING, "identity" );

#if LIBCURL_VERSION_NUM >= 0x071900
	curl_easy_setopt( file->curl, CURLOPT_TCP_KEEPALIVE, 1L );
#endif

	if( file->resume_from > 0 )
		curl_easy_setopt( file->curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)file->resume_from );

	if( curl_multi_add_handle( http_curl_multi, file->curl ) != CURLM_OK )
	{
		curl_easy_cleanup( file->curl );
		file->curl = NULL;
		HTTP_CurlCloseDownloadFile( file );
		return false;
	}

	file->curl_active = true;
	file->pfn_process = HTTP_FileCurlActive;
	file->blocktime = 0;
	file->blockreason = "libcurl transfer";
	http.active_count++;

	Con_Reportf( "HTTP/CURL: started %s\n", url );
	return true;
}

static void HTTP_CurlFinishFile( httpfile_t *file, qboolean error )
{
	if( !file )
		return;

	/*
	 * If a complete local partial caused 416, accept it only when the
	 * resource table supplied a matching expected size.
	 */
	if( file->curl_http_code == 416 )
	{
		if( file->resume_from > 0 && file->reported_size > 0 && file->resume_from == file->reported_size )
		{
			Con_Reportf( "HTTP/CURL: %s already complete according to local partial size\n", file->path );
			error = false;
		}
		else error = true;
	}

	HTTP_CurlCleanupFile( file );
	HTTP_FreeFile( file, error );
}

static void HTTP_CurlPump( void )
{
	CURLMsg *msg;
	int msgs_left;
	CURLMcode mc;

	if( !http_curl_multi )
		return;

	mc = curl_multi_perform( http_curl_multi, &http_curl_running );
	if( mc != CURLM_OK )
	{
		Con_Printf( S_ERROR "HTTP/CURL: curl_multi_perform failed: %s\n", curl_multi_strerror( mc ));
		return;
	}

	while(( msg = curl_multi_info_read( http_curl_multi, &msgs_left ) ))
	{
		if( msg->msg == CURLMSG_DONE )
		{
			char *private_data = NULL;
			httpfile_t *file = NULL;
			long code = 0;
			qboolean error = false;

			curl_easy_getinfo( msg->easy_handle, CURLINFO_PRIVATE, &private_data );
			curl_easy_getinfo( msg->easy_handle, CURLINFO_RESPONSE_CODE, &code );

			file = (httpfile_t *)private_data;
			if( !file )
				continue;

			file->curl_result = msg->data.result;
			file->curl_http_code = code;

			if( msg->data.result != CURLE_OK )
			{
				Con_Printf( S_ERROR "HTTP/CURL: failed %s: %s\n",
					file->path,
					file->curl_error[0] ? file->curl_error : curl_easy_strerror( msg->data.result ));
				error = true;
			}
			else if( code < 200 || code >= 300 )
			{
				Con_Printf( S_ERROR "HTTP/CURL: bad HTTP code %ld for %s\n", code, file->path );
				if( code == 400 || code == 401 || code == 403 || code == 404 )
					file->no_retry = true;
				error = true;
			}

			HTTP_CurlFinishFile( file, error );
		}
	}
}

/*
========================
HTTP_ArchiveExtractMember

Small libarchive helper used by the experimental manifest path.
It is intentionally path-safe and streams data instead of loading the
whole archive into memory.
========================
*/
static qboolean HTTP_ArchiveExtractMember( const char *archive_path, const char *member_name, const char *out_path )
{
	struct archive *a;
	struct archive_entry *entry;
	file_t *out = NULL;
	int r;

	if( !archive_path || !member_name || !out_path )
		return false;

	if( Q_strstr( member_name, ".." ) || member_name[0] == '/' || member_name[0] == '\\' )
	{
		Con_Printf( S_ERROR "HTTP/archive: unsafe member path %s\n", member_name );
		return false;
	}

	a = archive_read_new();
	if( !a )
		return false;

	archive_read_support_format_zip( a );
	archive_read_support_format_tar( a );
	archive_read_support_filter_gzip( a );

	if( archive_read_open_filename( a, archive_path, 65536 ) != ARCHIVE_OK )
	{
		Con_Printf( S_ERROR "HTTP/archive: cannot open %s: %s\n", archive_path, archive_error_string( a ));
		archive_read_free( a );
		return false;
	}

	while(( r = archive_read_next_header( a, &entry )) == ARCHIVE_OK )
	{
		const char *name = archive_entry_pathname( entry );

		if( name && !Q_strcmp( name, member_name ))
		{
			const void *buff;
			size_t size;
			la_int64_t offset;

			out = FS_Open( out_path, "wb", true );
			if( !out )
			{
				archive_read_free( a );
				return false;
			}

			while(( r = archive_read_data_block( a, &buff, &size, &offset )) == ARCHIVE_OK )
			{
				(void)offset;

				if( FS_Write( out, buff, size ) != (int)size )
				{
					FS_Close( out );
					archive_read_free( a );
					return false;
				}
			}

			FS_Close( out );
			archive_read_free( a );
			return true;
		}

		archive_read_data_skip( a );
	}

	archive_read_free( a );
	return false;
}

/*
=======================
HTTP_ExtractArchive_f

Manual debug command:
http_extractarchive <archive-on-disk> <member> <out-path>
=======================
*/
static void HTTP_ExtractArchive_f( void )
{
	if( Cmd_Argc() != 4 )
	{
		Con_Printf( S_USAGE "http_extractarchive <archive-on-disk> <member> <out-path>\n" );
		return;
	}

	if( HTTP_ArchiveExtractMember( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_Argv( 3 )))
		Con_Printf( "HTTP/archive: extracted %s from %s to %s\n", Cmd_Argv( 2 ), Cmd_Argv( 1 ), Cmd_Argv( 3 ));
	else Con_Printf( S_ERROR "HTTP/archive: failed to extract %s from %s\n", Cmd_Argv( 2 ), Cmd_Argv( 1 ));
}

#endif

static qboolean HTTP_ApplyRedirect( httpfile_t *file, const char *location )
{
	char host[256];
	char path[MAX_SYSPATH];
	int port;
	httpserver_t *next = file->server ? file->server->next : NULL;

	if( !location || !location[0] )
		return false;

	if( file->redirects >= (int)http_max_redirects.value )
	{
		Con_Printf( S_ERROR "HTTP: too many redirects for %s\n", file->path );
		return false;
	}

	if( !Q_strnicmp( location, "http://", 7 ) || !Q_strnicmp( location, "https://", 8 ))
	{
		if( !HTTP_ParseURLParts( location, host, sizeof( host ), &port, path, sizeof( path )))
			return false;

		memset( &file->redirect_server, 0, sizeof( file->redirect_server ));
		if( !Q_strnicmp( location, "https://", 8 ))
			Q_strncpy( file->redirect_server.scheme, "https", sizeof( file->redirect_server.scheme ));
		else Q_strncpy( file->redirect_server.scheme, "http", sizeof( file->redirect_server.scheme ));
		Q_strncpy( file->redirect_server.host, host, sizeof( file->redirect_server.host ));
		Q_strncpy( file->redirect_server.path, "/", sizeof( file->redirect_server.path ));
		file->redirect_server.port = port;
		file->redirect_server.next = next;
		file->server = &file->redirect_server;
		Q_strncpy( file->url_path, path, sizeof( file->url_path ));
	}
	else if( location[0] == '/' )
	{
		Q_strncpy( file->url_path, location, sizeof( file->url_path ));
		HTTP_NormalizePath( file->url_path );
	}
	else
	{
		char current[MAX_SYSPATH];
		char *slash;

		HTTP_BuildRequestURI( file, current, sizeof( current ));
		slash = strrchr( current, '/' );
		if( !slash )
			return false;

		slash[1] = '\0';
		Q_snprintf( file->url_path, sizeof( file->url_path ), "%s%s", current, location );
		HTTP_NormalizePath( file->url_path );
	}

	file->redirects++;
	Con_Reportf( "HTTP: redirect %d for %s -> %s:%d%s\n", file->redirects, file->path,
		file->server->host, file->server->port, file->url_path );

	return HTTP_RestartTransfer( file );
}

static qboolean HTTP_HasCompressedSuffix( httpfile_t *file )
{
	size_t len = Q_strlen( file->path );

	return (( len > 4 && !Q_stricmp( file->path + len - 4, ".bz2" ))
		|| ( len > 3 && !Q_stricmp( file->path + len - 3, ".gz" )));
}

static qboolean HTTP_CheckComplete( httpfile_t *file )
{
	if( file->size > 0 )
	{
		http.progress += (float)file->downloaded / file->size;
		http.progress_count++;

		if( file->downloaded < file->size )
			return false;
	}

	if( file->size > 0 && file->downloaded >= file->size )
	{
		if(( file->compressed && !file->chunked ) || HTTP_HasCompressedSuffix( file ))
		{
			file->success = true;
			HTTP_SelectProcessor( file );
		}
		else HTTP_FreeFile( file, false );

		return true;
	}

	return false;
}

/*
==============
HTTP_FreeFile

Skip to next server/file
==============
*/
static void HTTP_FreeFile( httpfile_t *file, qboolean error )
{
	char incname[MAX_SYSPATH];
	qboolean was_open = false;

	file->blocktime = 0;
#if XASH_USE_CURL_DOWNLOADER
	HTTP_CurlCleanupFile( file );
#endif

	if( file->file )
	{
		FS_Close( file->file );
		was_open = true;
	}
	file->file = NULL;

	HTTP_CloseSocketOnly( file );
	HTTP_MakeIncompleteName( file, incname, sizeof( incname ));

	if( error )
	{
		/* o sufixo .bz2/.gz e so para a URL: restaura o nome real antes de
		 * qualquer fallback (dlfile) ou mensagem de erro */
		if( file->orig_path[0] )
		{
			Q_strncpy( file->path, file->orig_path, sizeof( file->path ));
			file->suffix_try = 0;
		}

		if( file->server && was_open && !file->no_retry && file->retries < (int)http_max_retries.value )
		{
			file->retries++;
			Con_Reportf( S_WARN "HTTP: retry %d/%d for %s on %s:%d\n",
				file->retries, (int)http_max_retries.value, file->path,
				file->server->host, file->server->port );

			HTTP_ResetResponseState( file );
			file->pfn_process = HTTP_FileQueue;
			return;
		}

		if( file->server && was_open )
		{
			file->server = file->server->next;
			file->retries = 0;
			file->redirects = 0;
			file->url_path[0] = '\0';
			file->no_retry = false;
			HTTP_ResetResponseState( file );
			file->pfn_process = HTTP_FileQueue;
			return;
		}

		if( http_autoremove.value == 1 )
		{
			Con_Printf( S_ERROR "no servers to download %s\n", file->path );
			FS_Delete( incname );
		}
		else
			Con_Printf( S_ERROR "no servers to download %s. Keep %s\n", file->path, incname );
	}
	else
	{
		if( !file->compressed )
		{
			char name[MAX_SYSPATH];
			Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s", file->path );
			HTTP_NormalizePath( name );
			FS_Rename( incname, name );
		}
		else
			FS_Delete( incname );
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
	char incname[MAX_SYSPATH];
	fs_offset_t existing_size = 0;

	if( http.active_count >= (int)http_maxconnections.value )
		return 0;

	if( !file->server )
	{
		HTTP_FreeFile( file, true );
		return 0;
	}

#if XASH_USE_CURL_DOWNLOADER
	if( http_curl.value )
	{
		HTTP_NormalizePath( file->path );
		HTTP_NormalizePath( file->server->path );

		if( http.active_count >= (int)http_max_active_requests.value )
			return 0;

		if( HTTP_CurlStartFile( file ))
			return 0;

		Con_Reportf( S_WARN "HTTP/CURL: failed to start %s, using legacy HTTP backend\n", file->path );
	}
#endif

	HTTP_NormalizePath( file->path );
	HTTP_NormalizePath( file->server->path );

	Con_Reportf( "HTTP: Starting download %s from %s:%d\n", file->path, file->server->host, file->server->port );

	HTTP_MakeIncompleteName( file, incname, sizeof( incname ));

	if( FS_FileExists( incname, true ) )
		existing_size = FS_FileSize( incname, true );

	if( existing_size < 0 )
		existing_size = 0;

	file->downloaded = (int)existing_size;
	file->resume_from = file->downloaded;

	if( !HTTP_ReopenIncomplete( file, false ))
	{
		HTTP_FreeFile( file, true );
		return 0;
	}

	HTTP_ResetResponseState( file );
	file->pfn_process = HTTP_FileResolveNS;
	file->lastchecksize = 0;
	file->checktime = 0;
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

// HTTP_FileConnect: URL-encode, safe resume and no unsupported deflate advertisement.
static int HTTP_FileConnect( httpfile_t *file )
{
	string useragent;
	char request_uri[MAX_SYSPATH * 2];
	char range[64] = "";
	int res;

	res = connect( file->socket, (struct sockaddr *)&file->addr, NET_SockAddrLen( &file->addr ));

	if( res < 0 )
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEINPROGRESS )
		{
			Con_Printf( S_ERROR "%s: connect failed: %s\n", __func__, NET_ErrorString());
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
	else
		Q_strncpy( useragent, http_useragent.string, sizeof( useragent ));

	HTTP_BuildRequestURI( file, request_uri, sizeof( request_uri ));

	if( file->resume_from > 0 )
		Q_snprintf( range, sizeof( range ), "Range: bytes=%d-\r\n", file->resume_from );

	file->query_length = Q_snprintf( file->buf, sizeof( file->buf ),
		"GET %s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: %s\r\n"
		"Accept-Encoding: gzip\r\n"
		"Accept: */*\r\n"
		"Connection: close\r\n"
		"%s\r\n",
		request_uri,
		file->server->host, file->server->port,
		useragent,
		range );

	Q_strncpy( file->query_backup, file->buf, sizeof( file->query_backup ));
	file->bytes_sent = 0;
	file->header_size = 0;
#if HTTP_HAVE_TLS
	if( file->server && !Q_strnicmp( file->server->scheme, "https", 5 ))
		file->pfn_process = HTTP_FileTlsHandshake;
	else
#endif
		file->pfn_process = HTTP_FileSendRequest;
	return 1;
}

#if HTTP_HAVE_TLS

static SSL_CTX *http_ssl_ctx;

static qboolean HTTP_TlsInit( void )
{
	if( http_ssl_ctx )
		return true;

	SSL_library_init();
	SSL_load_error_strings();

	http_ssl_ctx = SSL_CTX_new( TLS_client_method( ));
	if( !http_ssl_ctx )
		return false;

	/* fastdl publico: nem sempre tem cadeia confiavel completa */
	SSL_CTX_set_verify( http_ssl_ctx, SSL_VERIFY_NONE, NULL );
	return true;
}

static int HTTP_NetSend( httpfile_t *file, const void *buf, int len )
{
	SSL *ssl = (SSL *)file->ssl;
	int res, err;

	if( !ssl )
		return send( file->socket, buf, len, 0 );

	res = SSL_write( ssl, buf, len );
	if( res > 0 )
		return res;

	err = SSL_get_error( ssl, res );
	if( err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE )
	{
		errno = EWOULDBLOCK;
		return -1;
	}

	errno = ECONNRESET;
	return -1;
}

static int HTTP_NetRecv( httpfile_t *file, void *buf, int len )
{
	SSL *ssl = (SSL *)file->ssl;
	int res, err;

	if( !ssl )
		return recv( file->socket, buf, len, 0 );

	res = SSL_read( ssl, buf, len );
	if( res > 0 )
		return res;

	err = SSL_get_error( ssl, res );
	if( err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE )
	{
		errno = EWOULDBLOCK;
		return -1;
	}

	if( err == SSL_ERROR_ZERO_RETURN )
		return 0; // fim limpo

	errno = ECONNRESET;
	return -1;
}

/*
 * Handshake TLS em socket nao bloqueante: retorna 1 quando pronto para
 * enviar a requisicao, 0 para aguardar e trata erro liberando o arquivo.
 */
static int HTTP_FileTlsHandshake( httpfile_t *file )
{
	SSL *ssl = (SSL *)file->ssl;
	int res, err;

	if( !ssl )
	{
		if( !HTTP_TlsInit( ))
		{
			Con_Printf( S_ERROR "HTTP: openssl indisponivel para %s\n", file->path );
			HTTP_FreeFile( file, true );
			return 0;
		}

		ssl = SSL_new( http_ssl_ctx );
		if( !ssl )
		{
			HTTP_FreeFile( file, true );
			return 0;
		}

		SSL_set_fd( ssl, file->socket );
		SSL_set_tlsext_host_name( ssl, file->server->host );
		SSL_set_connect_state( ssl );
		file->ssl = ssl;
	}

	res = SSL_connect( ssl );
	if( res == 1 )
	{
		file->blocktime = 0;
		file->pfn_process = HTTP_FileSendRequest;
		return 1;
	}

	err = SSL_get_error( ssl, res );
	if( err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE )
	{
		file->blocktime += host.frametime;
		file->blockreason = "tls handshake";
		return 0;
	}

	Con_Printf( S_ERROR "HTTP: TLS handshake falhou para %s (%s)\n",
		file->path, ERR_reason_error_string( ERR_get_error( )));
	HTTP_FreeFile( file, true );
	return 0;
}

#endif // HTTP_HAVE_TLS

static int HTTP_FileSendRequest( httpfile_t *file )
{
	int res = -1;

#if HTTP_HAVE_TLS
	res = HTTP_NetSend( file, file->buf + file->bytes_sent, file->query_length - file->bytes_sent );
#else
	res = send( file->socket, file->buf + file->bytes_sent, file->query_length - file->bytes_sent, 0 );
#endif

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

/* ------------------------------------------------------------------ */
/* descompressao: zlib -> miniz -> metodo antigo                       */
/* ------------------------------------------------------------------ */

#define HTTP_DECOMP_AUTO   0
#define HTTP_DECOMP_ZLIB   1
#define HTTP_DECOMP_MINIZ  2
#define HTTP_DECOMP_LEGACY 3

static const char *HTTP_DecompName( int mode )
{
	switch( mode )
	{
	case HTTP_DECOMP_ZLIB:   return "zlib";
	case HTTP_DECOMP_MINIZ:  return "miniz";
	case HTTP_DECOMP_LEGACY: return "legacy";
	default:                 return "auto";
	}
}

/*
 * Descobre onde os dados deflate comecam num stream gzip.
 * Retorna o offset do primeiro byte comprimido ou -1 se nao for gzip.
 */
static int HTTP_GzipDataOffset( const byte *buf, int len )
{
	int off = 10;

	if( len < 10 || buf[0] != 0x1f || buf[1] != 0x8b || buf[2] != 8 )
		return -1;

	if( buf[3] & 0x04 )   /* FEXTRA */
	{
		if( len < off + 2 )
			return -1;
		off += 2 + ( buf[off] | ( buf[off + 1] << 8 ));
	}
	if( buf[3] & 0x08 )   /* FNAME */
	{
		while( off < len && buf[off] ) off++;
		off++;
	}
	if( buf[3] & 0x10 )   /* FCOMMENT */
	{
		while( off < len && buf[off] ) off++;
		off++;
	}
	if( buf[3] & 0x02 )   /* FHCRC */
		off += 2;

	return ( off < len ) ? off : -1;
}

/*
 * Infla o arquivo a partir de offset e escreve em out.
 * Retorna os bytes escritos, ou -1 em erro.
 */
static int HTTP_InflateToFile( httpfile_t *file, file_t *out, int offset, int windowBits )
{
	z_stream stream;
	byte inbuf[32768];
	byte outbuf[32768];
	int rc = Z_OK;
	int readbytes;
	int total = 0;

	memset( &stream, 0, sizeof( stream ));

	if( inflateInit2( &stream, windowBits ) != Z_OK )
		return -1;

	g_fsapi.Seek( file->file, offset, SEEK_SET );

	do
	{
		readbytes = g_fsapi.Read( file->file, inbuf, sizeof( inbuf ));
		if( readbytes < 0 )
		{
			rc = Z_ERRNO;
			break;
		}
		if( readbytes == 0 )
			break;

		stream.next_in = inbuf;
		stream.avail_in = readbytes;

		do
		{
			int have;

			stream.next_out = outbuf;
			stream.avail_out = sizeof( outbuf );

			rc = inflate( &stream, Z_NO_FLUSH );
			if( rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR )
				break;

			have = sizeof( outbuf ) - stream.avail_out;
			if( have > 0 && FS_Write( out, outbuf, have ) != have )
			{
				rc = Z_ERRNO;
				break;
			}
			total += have;
		} while( stream.avail_out == 0 && rc == Z_OK );

		if( rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR )
			break;

		/* miniz/zlib devolvem BUF_ERROR quando so faltou entrada */
		if( rc == Z_BUF_ERROR && stream.avail_in == 0 )
			rc = Z_OK;
	} while( rc != Z_STREAM_END );

	inflateEnd( &stream );

	return ( rc == Z_STREAM_END ) ? total : -1;
}

/*
 * Tenta um backend especifico. Retorna bytes escritos ou -1.
 */
static int HTTP_TryDecompress( httpfile_t *file, const char *outname, int backend )
{
	file_t *out;
	byte head[1024];
	int windowBits, offset = 0;
	int got, gzoff, rc;

	switch( backend )
	{
	case HTTP_DECOMP_ZLIB:
#if HTTP_HAVE_ZLIB
		/* zlib le o envelope gzip inteiro */
		windowBits = 16 + MAX_WBITS;
		break;
#else
		return -1;
#endif
	case HTTP_DECOMP_MINIZ:
		/* miniz so aceita +-15: pula o cabecalho e infla raw */
		g_fsapi.Seek( file->file, 0, SEEK_SET );
		got = g_fsapi.Read( file->file, head, sizeof( head ));
		gzoff = ( got > 0 ) ? HTTP_GzipDataOffset( head, got ) : -1;
		if( gzoff < 0 )
			return -1;
		windowBits = -MAX_WBITS;
		offset = gzoff;
		break;
	default:
		/* metodo antigo: tenta 16+MAX_WBITS sem pular nada */
		windowBits = 16 + MAX_WBITS;
		break;
	}

	if( !( out = FS_Open( outname, "wb", true )))
		return -1;

	rc = HTTP_InflateToFile( file, out, offset, windowBits );
	FS_Close( out );

	if( rc > 0 )
		Con_Reportf( "HTTP: %s descomprimido com %s (%d bytes)\n",
			file->path, HTTP_DecompName( backend ), rc );

	return rc;
}

/*
 * Descomprime um buffer gzip em memoria, usando um backend especifico.
 * Usado pelo selftest. Retorna bytes escritos ou -1.
 */
static int HTTP_GunzipBuffer( const byte *in, int inlen, int backend, byte *out, int outcap )
{
	z_stream stream;
	int windowBits, offset = 0, rc = Z_OK, total = 0, gzoff;

	if( backend == HTTP_DECOMP_ZLIB )
	{
#if HTTP_HAVE_ZLIB
		windowBits = 16 + MAX_WBITS;
#else
		return -1;
#endif
	}
	else if( backend == HTTP_DECOMP_MINIZ )
	{
		gzoff = HTTP_GzipDataOffset( in, inlen );
		if( gzoff < 0 )
			return -1;
		windowBits = -MAX_WBITS;
		offset = gzoff;
	}
	else
	{
		windowBits = 16 + MAX_WBITS;
	}

	memset( &stream, 0, sizeof( stream ));

	if( inflateInit2( &stream, windowBits ) != Z_OK )
		return -1;

	stream.next_in = (byte *)in + offset;
	stream.avail_in = inlen - offset;

	do
	{
		stream.next_out = out + total;
		stream.avail_out = outcap - total;

		rc = inflate( &stream, Z_NO_FLUSH );
		if( rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR )
			break;

		total = outcap - stream.avail_out;

		if( stream.avail_out == 0 && rc == Z_OK )
			break; /* saida cheia */
	} while( rc != Z_STREAM_END );

	inflateEnd( &stream );

	return ( rc == Z_STREAM_END ) ? total : -1;
}

static void Cmd_HttpGzipSelfTest( void )
{
	/* "hello fastdl" comprimido com gzip */
	static const byte blob[] = {
		0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x02, 0xff, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57,
		0x48, 0x4b, 0x2c, 0x2e, 0x49, 0xc9, 0x01, 0x00,
		0xb2, 0x05, 0xea, 0xd6, 0x0c, 0x00, 0x00, 0x00
	};
	static const char expect[] = "hello fastdl";
	byte out[64];
	int i, ok = 0;

	Con_Printf( "GZIP SELFTEST: zlib=%d\n", HTTP_HAVE_ZLIB );

	for( i = HTTP_DECOMP_ZLIB; i <= HTTP_DECOMP_LEGACY; i++ )
	{
		int n = HTTP_GunzipBuffer( blob, sizeof( blob ), i, out, sizeof( out ));
		qboolean good = ( n == (int)sizeof( expect ) - 1 && !memcmp( out, expect, n ));

		Con_Printf( "  %-6s rc=%d %s\n", HTTP_DecompName( i ), n, good ? "ok" : "falhou" );
		if( good )
			ok++;
	}

	Con_Printf( "GZIP SELFTEST: %s\n", ok > 0 ? "PASS" : "FAIL" );
}

/* nome final do arquivo: sem o sufixo .bz2/.gz quando houve retry */
static const char *HTTP_OutName( httpfile_t *file )
{
	return ( file->orig_path[0] ) ? file->orig_path : file->path;
}

/*
 * Descomprime um arquivo .bz2 (padrao dos servidores fastdl).
 * Escreve em downloaded/<nome original>.
 */
static int HTTP_FileBunzip2( httpfile_t *file )
{
	bz_stream stream;
	char name[MAX_SYSPATH];
	file_t *out;
	byte inbuf[32768];
	byte outbuf[32768];
	int rc = BZ_OK, readbytes, total = 0;

	Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s", HTTP_OutName( file ));
	HTTP_NormalizePath( name );

	if( !( out = FS_Open( name, "wb", true ) ))
	{
		Con_Printf( S_ERROR "HTTP: cannot open bunzip2 output %s\n", name );
		HTTP_FreeFile( file, true );
		return 0;
	}

	memset( &stream, 0, sizeof( stream ));

	if( BZ2_bzDecompressInit( &stream, 0, 0 ) != BZ_OK )
	{
		FS_Close( out );
		HTTP_FreeFile( file, true );
		return 0;
	}

	g_fsapi.Seek( file->file, 0, SEEK_SET );

	do
	{
		readbytes = g_fsapi.Read( file->file, inbuf, sizeof( inbuf ));
		if( readbytes <= 0 )
			break;

		stream.next_in = (char *)inbuf;
		stream.avail_in = readbytes;

		do
		{
			int have;

			stream.next_out = (char *)outbuf;
			stream.avail_out = sizeof( outbuf );

			rc = BZ2_bzDecompress( &stream );
			if( rc != BZ_OK && rc != BZ_STREAM_END )
				break;

			have = sizeof( outbuf ) - stream.avail_out;
			if( have > 0 && FS_Write( out, outbuf, have ) != have )
			{
				rc = BZ_IO_ERROR;
				break;
			}
			total += have;
		} while( stream.avail_out == 0 && rc == BZ_OK );

		if( rc != BZ_OK && rc != BZ_STREAM_END )
			break;
	} while( rc != BZ_STREAM_END );

	BZ2_bzDecompressEnd( &stream );
	FS_Close( out );

	if( rc == BZ_STREAM_END )
	{
		Con_Reportf( "HTTP: %s descomprimido com bzip2 (%d bytes)\n", file->path, total );
		HTTP_FreeFile( file, false );
	}
	else
	{
		Con_Printf( S_ERROR "HTTP: bz2 quebrado em %s\n", file->path );
		FS_Delete( name );
		HTTP_FreeFile( file, true );
	}

	return 1;
}

/* escolhe o processador final pelo Content-Encoding ou pela extensao */
static void HTTP_SelectProcessor( httpfile_t *file )
{
	size_t len = Q_strlen( file->path );

	if( file->compressed && !file->chunked )
	{
		file->pfn_process = HTTP_FileDecompress;
	}
	else if( len > 4 && !Q_stricmp( file->path + len - 4, ".bz2" ))
	{
		file->pfn_process = HTTP_FileBunzip2;
	}
	else if( len > 3 && !Q_stricmp( file->path + len - 3, ".gz" ))
	{
		file->pfn_process = HTTP_FileDecompress;
	}
	else
	{
		HTTP_FreeFile( file, false );
	}
}

static int HTTP_FileDecompress( httpfile_t *file )
{
	static const int auto_order[] = { HTTP_DECOMP_ZLIB, HTTP_DECOMP_MINIZ, HTTP_DECOMP_LEGACY };
	char name[MAX_SYSPATH];
	int mode, i, written = -1;

	Q_snprintf( name, sizeof( name ), DEFAULT_DOWNLOADED_DIRECTORY "%s", HTTP_OutName( file ));
	HTTP_NormalizePath( name );

	mode = (int)http_decompress.value;
	if( mode < HTTP_DECOMP_AUTO || mode > HTTP_DECOMP_LEGACY )
		mode = HTTP_DECOMP_AUTO;

	if( mode == HTTP_DECOMP_AUTO )
	{
		for( i = 0; i < (int)( sizeof( auto_order ) / sizeof( auto_order[0] )) && written <= 0; i++ )
			written = HTTP_TryDecompress( file, name, auto_order[i] );
	}
	else
	{
		written = HTTP_TryDecompress( file, name, mode );
	}

	if( written > 0 )
	{
		HTTP_FreeFile( file, false );
		return 1;
	}

	Con_Printf( S_ERROR "%s: nenhum descompressor funcionou para %s\n", __func__, file->path );
	FS_Delete( name );
	HTTP_FreeFile( file, true );

	return 0;
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
		int ret;
		int len_to_write;

		if( file->chunked )
		{
			while( file->chunk_crlf > 0 && length > 0 )
			{
				pos++;
				length--;
				file->chunk_crlf--;
			}

			if( length <= 0 )
				return 1;

			if( file->chunksize <= 0 )
			{
				while( length > 0 )
				{
					char ch = file->buf[pos++];

					length--;

					if( file->chunk_header_len + 1 < (int)sizeof( file->chunk_header ))
						file->chunk_header[file->chunk_header_len++] = ch;

					if( ch == '\n' )
					{
						char *semi;

						file->chunk_header[file->chunk_header_len] = '\0';
						semi = Q_strchr( file->chunk_header, ';' );
						if( semi )
							*semi = '\0';

						file->chunksize = Q_atoi_hex( 1, file->chunk_header );
						file->chunk_header_len = 0;

						if( file->chunksize == 0 )
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

								if( file->reported_size > 0 && filelen != file->reported_size )
								{
									Con_Printf( S_ERROR "downloaded file %s size doesn't match reported size. Got %ld bytes, expected %d bytes\n",
										file->path, (long)filelen, file->reported_size );
									HTTP_FreeFile( file, true );
								}
								else HTTP_FreeFile( file, false );

								return 1;
							}
						}

						break;
					}

					if( file->chunk_header_len + 1 >= (int)sizeof( file->chunk_header ))
					{
						Con_Printf( S_ERROR "can't parse chunked transfer encoding header for %s\n", file->path );
						if( http_show_headers.value )
							Con_Reportf( "Request headers: %s", file->query_backup );
						HTTP_FreeFile( file, true );
						return 0;
					}
				}

				if( file->chunksize <= 0 || length <= 0 )
					continue;
			}

			len_to_write = Q_min( length, file->chunksize );
		}
		else len_to_write = length;

		ret = FS_Write( file->file, &file->buf[pos], len_to_write );
		if( ret != len_to_write )
		{
			Con_Printf( S_ERROR "write failed for %s!\n", file->path );
			HTTP_FreeFile( file, true );
			return 0;
		}

		length -= len_to_write;
		pos += ret;
		file->downloaded += ret;
		file->lastchecksize += ret;

		if( file->chunked )
		{
			file->chunksize -= ret;
			if( file->chunksize == 0 )
				file->chunk_crlf = 2;
		}
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
	char recvbuf[sizeof( curfile->buf )];
	int res;

	while(( res =
#if HTTP_HAVE_TLS
		HTTP_NetRecv( curfile, recvbuf, sizeof( recvbuf ) - 1 )
#else
		recv( curfile->socket, recvbuf, sizeof( recvbuf ) - 1, 0 )
#endif
		) > 0 )
	{
		curfile->blocktime = 0;

		if( !curfile->got_response )
		{
			char *begin;

			if( curfile->header_size + res + 1 > (int)sizeof( curfile->buf ))
			{
				Con_Reportf( S_ERROR "Header too big, the size is %d\n", curfile->header_size );
				HTTP_FreeFile( curfile, true );
				return 0;
			}

			memcpy( curfile->buf + curfile->header_size, recvbuf, res );
			curfile->header_size += res;
			curfile->buf[curfile->header_size] = 0;
			begin = Q_strstr( curfile->buf, "\r\n\r\n" );

			if( begin )
			{
				char content_length_value[64];
				char content_encoding_value[64];
				char transfer_encoding_value[64];
				char content_range_value[128];
				char location_value[MAX_SYSPATH];
				int status;
				int body_pos;
				int body_len;

				*begin = 0;
				status = HTTP_ParseStatusCode( curfile->buf );

				if( status == 301 || status == 302 || status == 303 || status == 307 || status == 308 )
				{
					if( HTTP_GetHeaderValue( curfile->buf, "Location", location_value, sizeof( location_value )) &&
						HTTP_ApplyRedirect( curfile, location_value ))
						return 0;

					Con_Printf( S_ERROR "%s: redirect without supported Location header\n", curfile->path );
					curfile->no_retry = true;
					HTTP_FreeFile( curfile, true );
					return 0;
				}

				if( status == 416 )
				{
					if( curfile->resume_from > 0 && curfile->reported_size > 0 && curfile->resume_from == curfile->reported_size )
					{
						Con_Reportf( "HTTP: %s already complete according to local partial size\n", curfile->path );
						HTTP_FreeFile( curfile, false );
						return 0;
					}

					Con_Reportf( S_WARN "HTTP: invalid range for %s, restarting from zero\n", curfile->path );
					if( !HTTP_ReopenIncomplete( curfile, true ))
					{
						HTTP_FreeFile( curfile, true );
						return 0;
					}
					HTTP_RestartTransfer( curfile );
					return 0;
				}

				if( status != 200 && status != 206 )
				{
					/* fastdl costuma servir arquivo.bz2 quando o arquivo puro nao existe */
					if( status == 404 && http_fastdl_suffix.value && curfile->suffix_try < 3 )
					{
						char oldinc[MAX_SYSPATH];
						const char *ext = ( curfile->suffix_try == 0 ) ? ".bz2" :
							( curfile->suffix_try == 1 ) ? ".gz" : NULL;

						HTTP_MakeIncompleteName( curfile, oldinc, sizeof( oldinc ));

						if( !curfile->orig_path[0] )
							Q_strncpy( curfile->orig_path, curfile->path, sizeof( curfile->orig_path ));

						curfile->suffix_try++;

						if( ext )
						{
							Con_Reportf( "HTTP: %s nao existe, tentando %s\n", curfile->orig_path, ext );
							Q_snprintf( curfile->path, sizeof( curfile->path ), "%s%s", curfile->orig_path, ext );
						}
						else
						{
							/* FastDL em Linux diferencia maiusculas: tenta minusculas */
							Con_Reportf( "HTTP: %s nao existe, tentando %s\n", curfile->orig_path, "minusculas" );
							Q_strnlwr( curfile->orig_path, curfile->path, sizeof( curfile->path ));
						}
						curfile->url_path[0] = '\0';

						HTTP_CloseSocketOnly( curfile );
						if( curfile->file )
						{
							FS_Close( curfile->file );
							curfile->file = NULL;
						}
						FS_Delete( oldinc );

						curfile->retries = 0;
						curfile->redirects = 0;
						curfile->downloaded = 0;
						curfile->size = 0;
						curfile->reported_size = 0;
						HTTP_ResetResponseState( curfile );
						curfile->pfn_process = HTTP_FileQueue;
						return 0;
					}

					if( status == 400 || status == 401 || status == 403 || status == 404 )
						curfile->no_retry = true;

					if( status == 404 )
						Con_Printf( S_ERROR "%s: file not found\n", curfile->path );
					else
					{
						Con_Printf( S_ERROR "%s: bad response: %s\n", curfile->path, curfile->buf );
						if( http_show_headers.value )
							Con_Printf( "Request headers: %s", curfile->query_backup );
					}

					HTTP_FreeFile( curfile, true );
					return 0;
				}

				if( status == 200 && curfile->resume_from > 0 )
				{
					Con_Reportf( S_WARN "HTTP: server ignored Range for %s, truncating partial and restarting cleanly inside same response\n", curfile->path );
					if( !HTTP_ReopenIncomplete( curfile, true ))
					{
						HTTP_FreeFile( curfile, true );
						return 0;
					}
				}
				else if( status == 206 && curfile->resume_from <= 0 )
				{
					Con_Reportf( S_WARN "HTTP: server returned 206 without a local partial for %s; restarting from zero\n", curfile->path );
					if( !HTTP_ReopenIncomplete( curfile, true ))
					{
						HTTP_FreeFile( curfile, true );
						return 0;
					}
				}

				if( HTTP_GetHeaderValue( curfile->buf, "Content-Encoding", content_encoding_value, sizeof( content_encoding_value )) )
				{
					if( !Q_strnicmp( content_encoding_value, "gzip", 4 ) &&
						( content_encoding_value[4] == '\0' || content_encoding_value[4] == ';' || content_encoding_value[4] == ' ' ))
						curfile->compressed = true;
					else if( Q_strnicmp( content_encoding_value, "identity", 8 ))
					{
						Con_Printf( S_ERROR "%s: unsupported Content-Encoding: %s\n", curfile->path, content_encoding_value );
						if( http_show_headers.value )
							Con_Printf( "Request headers: %s", curfile->query_backup );
						curfile->no_retry = true;
						HTTP_FreeFile( curfile, true );
						return 0;
					}
				}

				if( HTTP_GetHeaderValue( curfile->buf, "Transfer-Encoding", transfer_encoding_value, sizeof( transfer_encoding_value )) &&
					Q_stristr( transfer_encoding_value, "chunked" ))
				{
					curfile->size = -1;
					curfile->chunked = true;
					Con_Reportf( "HTTP: Got %d! Chunked transfer encoding%s\n", status, curfile->compressed ? ", compressed" : "" );
				}
				else if( HTTP_GetHeaderValue( curfile->buf, "Content-Length", content_length_value, sizeof( content_length_value )) )
				{
					int size = Q_atoi( content_length_value );

					if( status == 206 )
					{
						if( HTTP_GetHeaderValue( curfile->buf, "Content-Range", content_range_value, sizeof( content_range_value )) )
						{
							int total = HTTP_ParseContentRangeTotal( content_range_value );
							curfile->size = ( total > 0 ) ? total : curfile->resume_from + size;
						}
						else curfile->size = curfile->resume_from + size;
					}
					else curfile->size = size;

					Con_Reportf( "HTTP: Got %d! File size is %d%s\n", status, curfile->size, curfile->compressed ? ", compressed" : "" );

					if( !curfile->compressed && curfile->reported_size > 0 && curfile->size > 0 && curfile->size != curfile->reported_size )
						Con_Reportf( S_WARN "Server reports different file size for %s: HTTP %d, resource %d\n",
							curfile->path, curfile->size, curfile->reported_size );
				}
				else
				{
					curfile->size = -1;
					curfile->close_delimited = true;
					Con_Reportf( "HTTP: Got %d! No Content-Length; using close-delimited body%s\n",
						status, curfile->compressed ? ", compressed" : "" );
				}

				if( http_show_headers.value )
					Con_Reportf( "Response headers: %s\n", curfile->buf );

				curfile->got_response = true;
				body_pos = ( begin + 4 ) - curfile->buf;
				body_len = curfile->header_size - body_pos;
				curfile->header_size = 0;

				if( body_len > 0 )
				{
					if( !HTTP_FileSaveReceivedData( curfile, body_pos, body_len ))
						return 0;
				}
			}
		}
		else
		{
			memcpy( curfile->buf, recvbuf, res );
			if( !HTTP_FileSaveReceivedData( curfile, 0, res ))
				return 0;

			if( curfile->checktime > 5 )
			{
				float speed = (float)curfile->lastchecksize / ( 5.0f * 1024 );

				curfile->checktime = 0;
				Con_Reportf( "download speed %f KB/s\n", speed );
				curfile->lastchecksize = 0;
			}
		}
	}

	if( HTTP_CheckComplete( curfile ))
		return 0;

	if( res == 0 )
	{
		if( curfile->close_delimited && curfile->got_response )
		{
			if( curfile->compressed || HTTP_HasCompressedSuffix( curfile ))
				HTTP_SelectProcessor( curfile );
			else HTTP_FreeFile( curfile, false );
			return 0;
		}

		if( curfile->got_response && curfile->size <= 0 && !curfile->chunked )
		{
			if( curfile->compressed || HTTP_HasCompressedSuffix( curfile ))
				HTTP_SelectProcessor( curfile );
			else HTTP_FreeFile( curfile, false );
			return 0;
		}

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
	return 0;
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

#if XASH_USE_CURL_DOWNLOADER
	if( http_curl.value )
		HTTP_CurlPump();
#endif

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
qboolean HTTP_CanDownload( void )
{
	return http.first_server != NULL;
}

void HTTP_AddDownload( const char *path, int size, qboolean process, resource_t *res )
{
	httpfile_t *httpfile;
	httpfile_t *tail;

	if( !http.first_server )
	{
		Con_Printf( S_ERROR "no servers to download %s\n", path );
		return;
	}

	for( httpfile = http.first_file; httpfile; httpfile = httpfile->next )
	{
		if( !Q_strcmp( httpfile->path, path ))
		{
			Con_Reportf( "File %s already queued to download\n", path );
			return;
		}
	}

	httpfile = Z_Calloc( sizeof( *httpfile ));

	Con_Reportf( "File %s queued to download\n", path );

	httpfile->resource = res;
	httpfile->size = size;
	httpfile->reported_size = size;
	httpfile->socket = -1;
	httpfile->blockreason = "queued";
	Q_strncpy( httpfile->path, path, sizeof( httpfile->path ));
	HTTP_NormalizePath( httpfile->path );

	httpfile->pfn_process = HTTP_FileQueue;
	httpfile->server = http.first_server;
	httpfile->process = process;

	if( !http.first_file )
		http.first_file = httpfile;
	else
	{
		for( tail = http.first_file; tail->next; tail = tail->next )
			;
		tail->next = httpfile;
	}
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
static httpserver_t *HTTP_ParseURL( const char *url_ )
{
	httpserver_t *server;
	char parsed_host[256];
	char path[MAX_SYSPATH];
	int port;
	size_t len;

	if( !HTTP_ParseURLParts( url_, parsed_host, sizeof( parsed_host ), &port, path, sizeof( path )))
		return NULL;

	server = Z_Calloc( sizeof( httpserver_t ));
	if( !Q_strnicmp( url_, "https://", 8 ))
		Q_strncpy( server->scheme, "https", sizeof( server->scheme ));
	else Q_strncpy( server->scheme, "http", sizeof( server->scheme ));
	Q_strncpy( server->host, parsed_host, sizeof( server->host ));
	Q_strncpy( server->path, path, sizeof( server->path ));
	server->port = port;

	if( server->path[0] == '\0' )
		Q_strncpy( server->path, "/", sizeof( server->path ));

	len = strlen( server->path );
	if( len == 0 || server->path[len - 1] != '/' )
	{
		if( len + 1 < sizeof( server->path ))
		{
			server->path[len++] = '/';
			server->path[len] = '\0';
		}
	}

	HTTP_NormalizePath( server->path );
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

#if XASH_USE_CURL_DOWNLOADER
		HTTP_CurlCleanupFile( file );
#endif

		if( file->file )
			FS_Close( file->file );

		if( file->socket != -1 )
			closesocket( file->socket );

		Mem_Free( file );
	}

	http.active_count = 0;
	http.progress_count = 0;
	http.progress = 0;
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
				char uri[MAX_SYSPATH * 2];
				httpserver_t *oldserver = file->server;

				file->server = server;
				HTTP_BuildRequestURI( file, uri, sizeof( uri ));
				file->server = oldserver;

				Con_Printf( "\t%s://%s:%d%s\n", server->scheme[0] ? server->scheme : "http", server->host, server->port, uri );
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
	Cmd_AddCommand( "http_gzip_selftest", Cmd_HttpGzipSelfTest, "testa os descompressores gzip (zlib, miniz, legado)" );

	Cvar_RegisterVariable( &http_useragent );
	Cvar_RegisterVariable( &http_autoremove );
	Cvar_RegisterVariable( &http_timeout );
	Cvar_RegisterVariable( &http_maxconnections );
	Cvar_RegisterVariable( &http_max_retries );
	Cvar_RegisterVariable( &http_max_redirects );
	Cvar_RegisterVariable( &http_show_headers );
	Cvar_RegisterVariable( &http_decompress );
	Cvar_RegisterVariable( &http_fastdl_suffix );
#if XASH_USE_CURL_DOWNLOADER
	Cvar_RegisterVariable( &http_curl );
	Cvar_RegisterVariable( &http_connect_timeout );
	Cvar_RegisterVariable( &http_low_speed_limit );
	Cvar_RegisterVariable( &http_low_speed_time );
	Cvar_RegisterVariable( &http_max_active_requests );
	Cmd_AddRestrictedCommand( "http_extractarchive", HTTP_ExtractArchive_f, "extract archive member using libarchive" );
#endif
}

/*
====================
HTTP_Shutdown
====================
*/
void HTTP_Shutdown( void )
{
	HTTP_Clear_f();
#if XASH_USE_CURL_DOWNLOADER
	HTTP_CurlGlobalShutdown();
#endif

	while( http.first_server )
	{
		httpserver_t *tmp = http.first_server;

		http.first_server = http.first_server->next;
		Mem_Free( tmp );
	}
}
