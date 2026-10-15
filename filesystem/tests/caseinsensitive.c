#include "filesystem_test_common.h"
#if XASH_POSIX
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static qboolean CheckFileContents( const char *path, const void *buf, fs_offset_t size )
{
	fs_offset_t len;
	byte *data;

	data = g_fs.LoadFile( path, &len, true );
	if( !data )
	{
		printf( "LoadFile fail\n" );
		return false;
	}

	if( len != size )
	{
		printf( "LoadFile sizeof fail\n" );
		free( data );
		return false;
	}

	if( memcmp( data, buf, size ))
	{
		printf( "LoadFile magic fail\n" );
		free( data );
		return false;
	}

	free( data );
	return true;
}

static qboolean TestCaseinsensitive( void )
{
	file_t *f1;
	FILE *f2;
	int magic = rand();

	// create some files first and write data
	f1 = g_fs.Open( "FOO/Bar.bin", "wb", true );
	g_fs.Write( f1, &magic, sizeof( magic ));
	g_fs.Close( f1 );

	// try to search it with different file name
	if( !g_fs.FileExists( "fOO/baR.bin", true ))
	{
		printf( "FileExists fail\n" );
		return false;
	}

	// create a file directly, to check if cache can re-read
	f2 = fopen( "FOO/Baz.bin", "wb" );
	fwrite( &magic, sizeof( magic ), 1, f2 );
	fclose( f2 );

	// try to open first file back but with different file name case
	if( !CheckFileContents( "foo/bar.BIN", &magic, sizeof( magic )))
		return false;

	// try to open second file that we created directly
	if( !CheckFileContents( "Foo/BaZ.Bin", &magic, sizeof( magic )))
		return false;

	g_fs.Delete( "foo/Baz.biN" );
	g_fs.Delete( "foo/bar.bin" );
	g_fs.Delete( "Foo" );

	return true;
}

#if XASH_POSIX
static qboolean WriteSysFile( const char *path, const void *buf, size_t size )
{
	FILE *f = fopen( path, "wb" );

	if( !f )
	{
		printf( "fopen %s fail: %s\n", path, strerror( errno ));
		return false;
	}

	fwrite( buf, size, 1, f );
	fclose( f );
	return true;
}

static int CountDirEntries( const char *path )
{
	DIR *dir = opendir( path );
	struct dirent *entry;
	int count = 0;

	if( !dir )
		return -1;

	while(( entry = readdir( dir )))
	{
		if( strcmp( entry->d_name, "." ) && strcmp( entry->d_name, ".." ))
			count++;
	}

	closedir( dir );
	return count;
}

// bypasses the filesystem, so it can remove leftovers of a failed run before anything gets cached
static qboolean RemoveTree( const char *path )
{
	struct stat buf;
	qboolean ret = true;

	if( lstat( path, &buf ) < 0 )
		return errno == ENOENT;

	if( S_ISDIR( buf.st_mode ))
	{
		DIR *dir = opendir( path );
		struct dirent *entry;

		while( dir && ( entry = readdir( dir )))
		{
			char child[MAX_SYSPATH];

			if( !strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." ))
				continue;

			snprintf( child, sizeof( child ), "%s/%s", path, entry->d_name );
			ret = RemoveTree( child ) && ret;
		}

		if( dir )
			closedir( dir );

		if( rmdir( path ) < 0 )
			ret = false;
	}
	else if( unlink( path ) < 0 )
		ret = false;

	if( !ret )
		printf( "can't remove %s: %s\n", path, strerror( errno ));

	return ret;
}

// an entry that is listed in the directory, but doesn't exist on disk
// (here a dangling symlink) must not turn the path into its directory
static qboolean TestStaleEntry( void )
{
	file_t *f;
	int magic = rand();

	f = g_fs.Open( "Stale/Present.bin", "wb", true );
	g_fs.Write( f, &magic, sizeof( magic ));
	g_fs.Close( f );

	// cache the directory with the present file
	if( !g_fs.FileExists( "stale/present.bin", true ))
	{
		printf( "FileExists fail\n" );
		return false;
	}

	if( symlink( "nowhere", "Stale/Missing.bin" ) < 0 )
	{
		printf( "symlink fail (%s), %s skipped\n", strerror( errno ), __func__ );
		return RemoveTree( "Stale" );
	}

	// rescan the directory, now it lists the missing file
	g_fs.FileExists( "stale/other.bin", true );

	if( g_fs.FileExists( "stale/missing.bin", true ))
	{
		printf( "FileExists of a missing listed file fail\n" );
		return false;
	}

	f = g_fs.Open( "stale/missing.bin", "wb", true );
	if( !f )
	{
		printf( "Open for write of a missing listed file fail\n" );
		return false;
	}
	g_fs.Write( f, &magic, sizeof( magic ));
	g_fs.Close( f );

	// it must write through the symlink and create its target, not another file
	if( CountDirEntries( "Stale" ) != 3 )
	{
		printf( "Open for write of a missing listed file created another file\n" );
		return false;
	}

	if( !CheckFileContents( "stale/nowhere", &magic, sizeof( magic )))
		return false;

	return RemoveTree( "Stale" );
}

// a cached entry that got renamed on disk to another case must be found again
// both as a file and as a directory in the middle of the path
static qboolean TestRenamedEntry( void )
{
	file_t *f;
	int magic = rand();

	f = g_fs.Open( "Renamed/Dir/File.bin", "wb", true );
	g_fs.Write( f, &magic, sizeof( magic ));
	g_fs.Close( f );

	// cache the whole path
	if( !CheckFileContents( "renamed/dir/file.bin", &magic, sizeof( magic )))
		return false;

	if( rename( "Renamed/Dir/File.bin", "Renamed/Dir/fILE.bin" ) < 0 )
	{
		printf( "rename fail: %s\n", strerror( errno ));
		return false;
	}

	if( !CheckFileContents( "renamed/dir/file.bin", &magic, sizeof( magic )))
		return false;

	if( rename( "Renamed/Dir", "Renamed/dIR" ) < 0 )
	{
		printf( "rename fail: %s\n", strerror( errno ));
		return false;
	}

	if( !CheckFileContents( "renamed/dir/file.bin", &magic, sizeof( magic )))
		return false;

	if( rename( "Renamed/dIR", "Renamed/DiR" ) < 0 )
	{
		printf( "rename fail: %s\n", strerror( errno ));
		return false;
	}

	f = g_fs.Open( "renamed/dir/new.bin", "wb", true );
	if( !f )
	{
		printf( "Open for write in a renamed directory fail\n" );
		return false;
	}
	g_fs.Write( f, &magic, sizeof( magic ));
	g_fs.Close( f );

	if( !g_fs.SysFileExists( "Renamed/DiR/new.bin" ))
	{
		printf( "Open for write in a renamed directory created another file\n" );
		return false;
	}

	return RemoveTree( "Renamed" );
}

static qboolean CheckTwins( int magic )
{
	return CheckFileContents( "Twins/Case/a.bin", &magic, sizeof( magic ))
		&& CheckFileContents( "Twins/CASE/b.bin", &magic, sizeof( magic ))
		&& CheckFileContents( "Twins/link.bin", &magic, sizeof( magic ));
}

// names that only differ by case must resolve to the exact one when it's there
static qboolean TestCaseCollision( void )
{
	int magic = rand();

	if( mkdir( "Twins", 0777 ) < 0 || mkdir( "Twins/Case", 0777 ) < 0 )
	{
		printf( "mkdir fail: %s\n", strerror( errno ));
		return false;
	}

	if( mkdir( "Twins/CASE", 0777 ) < 0 )
	{
		printf( "mkdir fail (%s), case-insensitive filesystem? %s skipped\n", strerror( errno ), __func__ );
		return RemoveTree( "Twins" );
	}

	if( !WriteSysFile( "Twins/Case/a.bin", &magic, sizeof( magic ))
		|| !WriteSysFile( "Twins/CASE/b.bin", &magic, sizeof( magic ))
		|| !WriteSysFile( "Twins/link.bin", &magic, sizeof( magic )))
		return false;

	// a twin that doesn't exist on disk
	if( symlink( "nowhere", "Twins/Link.bin" ) < 0 )
	{
		printf( "symlink fail (%s), %s skipped\n", strerror( errno ), __func__ );
		return RemoveTree( "Twins" );
	}

	if( !CheckTwins( magic ))
		return false;

	// add a file, so the directory gets merged while both twins have cached subentries
	if( !WriteSysFile( "Twins/new.bin", &magic, sizeof( magic )))
		return false;

	if( !CheckFileContents( "twins/new.bin", &magic, sizeof( magic )))
		return false;

	if( !CheckTwins( magic ))
		return false;

	// remove one twin, the merge must free its subentries instead of giving them
	// to the other one, which leaks the other's own (sanitizer build catches that)
	if( !RemoveTree( "Twins/CASE" ))
		return false;

	if( g_fs.FileExists( "Twins/CASE/b.bin", true ))
	{
		printf( "FileExists of a removed twin fail\n" );
		return false;
	}

	if( !CheckFileContents( "Twins/Case/a.bin", &magic, sizeof( magic )))
		return false;

	return RemoveTree( "Twins" );
}
#endif

int main( void )
{
	if( !LoadFilesystem() )
		return EXIT_FAILURE;

	srand( time( NULL ));

#if XASH_POSIX
	// leftovers of a failed run, remove them before the game directory gets cached
	if( !RemoveTree( "Stale" ) || !RemoveTree( "Renamed" ) || !RemoveTree( "Twins" ))
		return EXIT_FAILURE;
#endif

	// create game dir for us
	g_fs.AddGameDirectory( "./", FS_GAMEDIR_PATH );

	if( !TestCaseinsensitive())
		return EXIT_FAILURE;

#if XASH_POSIX
	if( !TestStaleEntry())
		return EXIT_FAILURE;

	if( !TestRenamedEntry())
		return EXIT_FAILURE;

	if( !TestCaseCollision())
		return EXIT_FAILURE;
#endif

	printf( "success\n" );

	return EXIT_SUCCESS;
}
