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

int main( void )
{
	if( !LoadFilesystem() )
		return EXIT_FAILURE;

	srand( time( NULL ));

	if( !TestCaseinsensitive())
		return EXIT_FAILURE;

	printf( "success\n" );

	return EXIT_SUCCESS;
}
