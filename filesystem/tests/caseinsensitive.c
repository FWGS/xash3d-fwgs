#include "filesystem_test_common.h"

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

	// create game dir for us
	g_fs.AddGameDirectory( "./", FS_GAMEDIR_PATH );

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
#include <unistd.h>

// an entry that is listed in the directory, but doesn't exist on disk
// (here a dangling symlink) must not turn the path into its directory
static qboolean TestStaleEntry( void )
{
	file_t *f;
	int magic = rand();

	g_fs.AddGameDirectory( "./", FS_GAMEDIR_PATH );

	f = g_fs.Open( "Stale/Present.bin", "wb", true );
	g_fs.Write( f, &magic, sizeof( magic ));
	g_fs.Close( f );

	// cache the directory with the present file
	if( !g_fs.FileExists( "stale/present.bin", true ))
	{
		printf( "FileExists fail\n" );
		return false;
	}

	// leftovers of a failed run
	unlink( "Stale/Missing.bin" );
	unlink( "Stale/nowhere" );

	if( symlink( "nowhere", "Stale/Missing.bin" ) < 0 )
	{
		printf( "symlink fail\n" );
		return false;
	}

	// rescan the directory, now it lists the missing file
	g_fs.FileExists( "stale/other.bin", true );

	f = g_fs.Open( "stale/missing.bin", "wb", true );
	if( !f )
	{
		printf( "Open for write of a missing listed file fail\n" );
		return false;
	}
	g_fs.Write( f, &magic, sizeof( magic ));
	g_fs.Close( f );

	// writing through the symlink created its target
	unlink( "Stale/Missing.bin" );
	unlink( "Stale/nowhere" );
	g_fs.Delete( "stale/present.bin" );
	g_fs.Delete( "Stale" );

	return true;
}
#endif

int main( void )
{
	if( !LoadFilesystem() )
		return EXIT_FAILURE;

	srand( time( NULL ));

	if( !TestCaseinsensitive())
		return EXIT_FAILURE;

#if XASH_POSIX
	if( !TestStaleEntry())
		return EXIT_FAILURE;
#endif

	printf( "success\n" );

	return EXIT_SUCCESS;
}
