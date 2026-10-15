/*
ps4_cwd.c - current working directory emulation for PS4
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
PS4 applications aren't allowed to call chdir() and the kernel rejects
relative paths, but the engine filesystem works relative to the game root.

This file is linked into every PS4 image (eboot.bin and each .prx module)
and the linker redirects file functions here with --wrap=<function>
(see scripts/waifulib/ps4.py). Relative paths are resolved against
emulated working directory, which is private to each image: modules don't
share libc state on PS4 anyway.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#define PS4_PATH_MAX 1024

static char ps4_cwd[PS4_PATH_MAX];

int __real_open( const char *path, int flags, ... );
FILE *__real_fopen( const char *path, const char *mode );
int __real_stat( const char *path, struct stat *st );
int __real_lstat( const char *path, struct stat *st );
int __real_fstat( int fd, struct stat *st );
DIR *__real_opendir( const char *path );
struct dirent *__real_readdir( DIR *dir );
int __real_mkdir( const char *path, mode_t mode );
int __real_rename( const char *oldpath, const char *newpath );
int __real_remove( const char *path );
int __real_unlink( const char *path );
int __real_rmdir( const char *path );
int __real_access( const char *path, int mode );
char *__real_getcwd( char *buf, size_t size );

// collapses "." and ".." components and duplicate slashes of an absolute path
static void PS4_NormalizePath( char *path )
{
	char *src = path, *dst = path;

	while( *src )
	{
		if( src[0] == '/' )
		{
			while( src[1] == '/' )
				src++;

			if( src[1] == '.' && ( src[2] == '/' || src[2] == 0 ))
			{
				src += 2;
				continue;
			}

			if( src[1] == '.' && src[2] == '.' && ( src[3] == '/' || src[3] == 0 ))
			{
				src += 3;

				// remove previous component
				while( dst > path && *--dst != '/' );
				continue;
			}
		}

		*dst++ = *src++;
	}

	*dst = 0;

	// remove trailing slash, but keep root
	if( dst > path + 1 && dst[-1] == '/' )
		dst[-1] = 0;

	if( !path[0] )
	{
		path[0] = '/';
		path[1] = 0;
	}
}

// returns path that kernel will accept: original if absolute or no cwd set
static const char *PS4_AbsPath( const char *path, char *buf, size_t size )
{
	if( !path || path[0] == '/' || !ps4_cwd[0] )
		return path;

	snprintf( buf, size, "%s/%s", ps4_cwd, path );
	PS4_NormalizePath( buf );

	return buf;
}

int __wrap_open( const char *path, int flags, ... )
{
	char buf[PS4_PATH_MAX];
	mode_t mode = 0;

	if( flags & O_CREAT )
	{
		va_list va;
		va_start( va, flags );
		mode = (mode_t)va_arg( va, int );
		va_end( va );
	}

	return __real_open( PS4_AbsPath( path, buf, sizeof( buf )), flags, mode );
}

FILE *__wrap_fopen( const char *path, const char *mode )
{
	char buf[PS4_PATH_MAX];
	return __real_fopen( PS4_AbsPath( path, buf, sizeof( buf )), mode );
}

/*
=============================================================================

	STRUCT STAT CONVERSION

PS4 kernel fills FreeBSD 9 struct stat (16-bit mode and nlink),
but OpenOrbis headers declare 32-bit mode_t, which shifts every
following field by 8 bytes. Layout is detected at runtime on a file
with known size, so this keeps working if the headers get fixed.

=============================================================================
*/
typedef struct
{
	uint32_t dev;
	uint32_t ino;
	uint16_t mode;
	uint16_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint32_t rdev;
	struct { int64_t sec, nsec; } atim, mtim, ctim;
	int64_t  size;
	int64_t  blocks;
	uint32_t blksize;
	uint32_t flags;
	uint32_t gen;
	int32_t  lspare;
	struct { int64_t sec, nsec; } birthtim;
} ps4_kernel_stat_t;

typedef union
{
	ps4_kernel_stat_t kst;
	struct stat st;
	char pad[256]; // in case kernel structure is larger than we think
} ps4_stat_buf_t;

enum
{
	PS4_STAT_UNKNOWN = 0,
	PS4_STAT_NATIVE,  // kernel matches headers
	PS4_STAT_FREEBSD, // needs conversion
};

static int ps4_stat_layout;

static void PS4_DetectStatLayout( void )
{
	ps4_stat_buf_t raw;
	off_t size;
	int fd;

	// default to what we've seen on real hardware
	ps4_stat_layout = PS4_STAT_FREEBSD;

	fd = __real_open( "/app0/eboot.bin", O_RDONLY, 0 );
	if( fd < 0 )
		return;

	size = lseek( fd, 0, SEEK_END );
	memset( &raw, 0, sizeof( raw ));

	if( size > 0 && __real_fstat( fd, &raw.st ) == 0 )
	{
		if( raw.st.st_size == size )
			ps4_stat_layout = PS4_STAT_NATIVE;
		else if( raw.kst.size == size )
			ps4_stat_layout = PS4_STAT_FREEBSD;
	}

	close( fd );
}

// for diagnostics
const char *PS4_GetStatLayout( void )
{
	if( ps4_stat_layout == PS4_STAT_UNKNOWN )
		PS4_DetectStatLayout( );

	return ps4_stat_layout == PS4_STAT_NATIVE ? "native" : "freebsd";
}

static void PS4_ConvertStat( const ps4_stat_buf_t *raw, struct stat *st )
{
	const ps4_kernel_stat_t *k = &raw->kst;

	memset( st, 0, sizeof( *st ));
	st->st_dev = k->dev;
	st->st_ino = k->ino;
	st->st_mode = k->mode;
	st->st_nlink = k->nlink;
	st->st_uid = k->uid;
	st->st_gid = k->gid;
	st->st_rdev = k->rdev;
	st->st_atim.tv_sec = k->atim.sec;
	st->st_atim.tv_nsec = k->atim.nsec;
	st->st_mtim.tv_sec = k->mtim.sec;
	st->st_mtim.tv_nsec = k->mtim.nsec;
	st->st_ctim.tv_sec = k->ctim.sec;
	st->st_ctim.tv_nsec = k->ctim.nsec;
	st->st_size = k->size;
	st->st_blocks = k->blocks;
	st->st_blksize = k->blksize;
}

#define PS4_STAT_CALL( call, st ) \
	do { \
		ps4_stat_buf_t raw; \
		int ret; \
		if( ps4_stat_layout == PS4_STAT_UNKNOWN ) \
			PS4_DetectStatLayout( ); \
		if( ps4_stat_layout == PS4_STAT_NATIVE ) \
			return call( st ); \
		ret = call( &raw.st ); \
		if( ret == 0 ) \
			PS4_ConvertStat( &raw, ( st )); \
		return ret; \
	} while( 0 )

#define PS4_STAT( st )  __real_stat( abspath, st )
#define PS4_LSTAT( st ) __real_lstat( abspath, st )
#define PS4_FSTAT( st ) __real_fstat( fd, st )

int __wrap_stat( const char *path, struct stat *st )
{
	char buf[PS4_PATH_MAX];
	const char *abspath = PS4_AbsPath( path, buf, sizeof( buf ));
	PS4_STAT_CALL( PS4_STAT, st );
}

int __wrap_lstat( const char *path, struct stat *st )
{
	char buf[PS4_PATH_MAX];
	const char *abspath = PS4_AbsPath( path, buf, sizeof( buf ));
	PS4_STAT_CALL( PS4_LSTAT, st );
}

int __wrap_fstat( int fd, struct stat *st )
{
	PS4_STAT_CALL( PS4_FSTAT, st );
}

DIR *__wrap_opendir( const char *path )
{
	char buf[PS4_PATH_MAX];
	return __real_opendir( PS4_AbsPath( path, buf, sizeof( buf )));
}

// The kernel returns raw directory records, unused ones (left by deleted files)
// have inode 0. FreeBSD libc skips them in readdir, musl doesn't, so deleted
// files were listed: the engine then tried to use a save file that was gone and
// loading autosaves failed. The inode is the first field of struct dirent in
// both FreeBSD and musl layouts, it's checked there to not depend on its name.
struct dirent *__wrap_readdir( DIR *dir )
{
	struct dirent *ent;

	while(( ent = __real_readdir( dir )) != NULL )
	{
		uint32_t ino;

		memcpy( &ino, ent, sizeof( ino ));
		if( ino != 0 )
			break;
	}

	return ent;
}

int __wrap_mkdir( const char *path, mode_t mode )
{
	char buf[PS4_PATH_MAX];
	return __real_mkdir( PS4_AbsPath( path, buf, sizeof( buf )), mode );
}

int __wrap_rename( const char *oldpath, const char *newpath )
{
	char buf1[PS4_PATH_MAX], buf2[PS4_PATH_MAX];
	return __real_rename( PS4_AbsPath( oldpath, buf1, sizeof( buf1 )), PS4_AbsPath( newpath, buf2, sizeof( buf2 )));
}

int __wrap_remove( const char *path )
{
	char buf[PS4_PATH_MAX];
	return __real_remove( PS4_AbsPath( path, buf, sizeof( buf )));
}

int __wrap_unlink( const char *path )
{
	char buf[PS4_PATH_MAX];
	return __real_unlink( PS4_AbsPath( path, buf, sizeof( buf )));
}

int __wrap_rmdir( const char *path )
{
	char buf[PS4_PATH_MAX];
	return __real_rmdir( PS4_AbsPath( path, buf, sizeof( buf )));
}

int __wrap_access( const char *path, int mode )
{
	char buf[PS4_PATH_MAX];
	return __real_access( PS4_AbsPath( path, buf, sizeof( buf )), mode );
}

int __wrap_chdir( const char *path )
{
	char buf[PS4_PATH_MAX];
	struct stat st;

	if( !path || !path[0] )
	{
		errno = ENOENT;
		return -1;
	}

	if( path[0] == '/' )
		snprintf( buf, sizeof( buf ), "%s", path );
	else if( ps4_cwd[0] )
		snprintf( buf, sizeof( buf ), "%s/%s", ps4_cwd, path );
	else
	{
		errno = ENOENT;
		return -1;
	}

	PS4_NormalizePath( buf );

	if( __wrap_stat( buf, &st ) < 0 )
		return -1;

	if( !S_ISDIR( st.st_mode ))
	{
		errno = ENOTDIR;
		return -1;
	}

	snprintf( ps4_cwd, sizeof( ps4_cwd ), "%s", buf );
	return 0;
}

char *__wrap_getcwd( char *buf, size_t size )
{
	size_t len;

	if( !ps4_cwd[0] )
		return __real_getcwd( buf, size );

	len = strlen( ps4_cwd ) + 1;

	if( !buf )
	{
		if( size < len )
			size = len;

		buf = malloc( size );
		if( !buf )
		{
			errno = ENOMEM;
			return NULL;
		}
	}
	else if( size < len )
	{
		errno = ERANGE;
		return NULL;
	}

	memcpy( buf, ps4_cwd, len );
	return buf;
}

char *__wrap_realpath( const char *path, char *resolved )
{
	char buf[PS4_PATH_MAX];
	struct stat st;

	if( !path )
	{
		errno = EINVAL;
		return NULL;
	}

	if( path[0] == '/' )
		snprintf( buf, sizeof( buf ), "%s", path );
	else if( ps4_cwd[0] )
		snprintf( buf, sizeof( buf ), "%s/%s", ps4_cwd, path );
	else
	{
		errno = ENOENT;
		return NULL;
	}

	// no symlinks on PS4 user storage, normalization is enough
	PS4_NormalizePath( buf );

	if( __wrap_stat( buf, &st ) < 0 )
		return NULL;

	if( !resolved )
		return strdup( buf );

	// POSIX requires PATH_MAX sized buffer here
	snprintf( resolved, 4096, "%s", buf );
	return resolved;
}

/*
=============================================================================

	NON-BLOCKING SOCKETS

PS4 kernel refuses fcntl( F_GETFL / F_SETFL ) on sockets, so neither the
engine nor musl (socket() with SOCK_NONBLOCK, used by DNS resolver) can make
them non-blocking. Fall back to SO_NBIO socket option and FIONBIO ioctl.

=============================================================================
*/
#define PS4_SO_NBIO   0x1200
#define PS4_FIONBIO   0x8004667e
#define PS4_MAX_FDS   4096

int __real_fcntl( int fd, int cmd, ... );
int setsockopt( int fd, int level, int name, const void *value, unsigned int len );
int ioctl( int fd, unsigned long request, ... );

static unsigned char ps4_nonblock[PS4_MAX_FDS / 8];

static int PS4_IsSocket( int fd )
{
	struct stat st;

	return __wrap_fstat( fd, &st ) == 0 && S_ISSOCK( st.st_mode );
}

static int PS4_SetSocketNonBlocking( int fd, int on )
{
	if( setsockopt( fd, 0xffff /* SOL_SOCKET */, PS4_SO_NBIO, &on, sizeof( on )) < 0
		&& ioctl( fd, PS4_FIONBIO, &on ) < 0 )
		return -1;

	if( fd >= 0 && fd < PS4_MAX_FDS )
	{
		if( on )
			ps4_nonblock[fd / 8] |= 1 << ( fd % 8 );
		else
			ps4_nonblock[fd / 8] &= ~( 1 << ( fd % 8 ));
	}

	return 0;
}

int __wrap_fcntl( int fd, int cmd, ... )
{
	va_list va;
	long arg;
	int ret, saved_errno;

	va_start( va, cmd );
	arg = va_arg( va, long );
	va_end( va );

	ret = __real_fcntl( fd, cmd, arg );

	if( ret >= 0 || ( cmd != F_GETFL && cmd != F_SETFL ))
		return ret;

	saved_errno = errno;

	if( !PS4_IsSocket( fd ))
	{
		errno = saved_errno;
		return ret;
	}

	if( cmd == F_GETFL )
	{
		int nonblock = fd >= 0 && fd < PS4_MAX_FDS && ( ps4_nonblock[fd / 8] & ( 1 << ( fd % 8 )));
		return O_RDWR | ( nonblock ? O_NONBLOCK : 0 );
	}

	if( PS4_SetSocketNonBlocking( fd, ( arg & O_NONBLOCK ) ? 1 : 0 ) == 0 )
		return 0;

	errno = saved_errno;
	return ret;
}
