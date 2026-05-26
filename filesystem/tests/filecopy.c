#include "filesystem_test_common.h"

static qboolean WriteSourceFile( const char *path, const byte *data, fs_offset_t size )
{
	file_t *f = g_fs.Open( path, "wb", true );
	if( !f )
	{
		printf( "WriteSourceFile: Open(%s) failed\n", path );
		return false;
	}

	if( g_fs.Write( f, data, size ) != size )
	{
		printf( "WriteSourceFile: short write\n" );
		g_fs.Close( f );
		return false;
	}

	g_fs.Close( f );
	return true;
}

static qboolean VerifyFile( const char *path, const byte *expected, fs_offset_t size )
{
	fs_offset_t len;
	byte *data = g_fs.LoadFile( path, &len, true );

	if( !data )
	{
		printf( "VerifyFile: LoadFile(%s) failed\n", path );
		return false;
	}

	if( len != size )
	{
		printf( "VerifyFile: size mismatch (%lld != %lld)\n", (long long)len, (long long)size );
		free( data );
		return false;
	}

	if( memcmp( data, expected, size ) != 0 )
	{
		printf( "VerifyFile: content mismatch\n" );
		free( data );
		return false;
	}

	free( data );
	return true;
}

static qboolean DoCopy( const char *src, const char *dst, fs_offset_t size )
{
	file_t *fin = g_fs.Open( src, "rb", true );
	if( !fin )
	{
		printf( "DoCopy: Open(%s) failed\n", src );
		return false;
	}

	file_t *fout = g_fs.Open( dst, "wb", true );
	if( !fout )
	{
		printf( "DoCopy: Open(%s) failed\n", dst );
		g_fs.Close( fin );
		return false;
	}

	qboolean ok = g_fs.FileCopy( fout, fin, (int)size );

	g_fs.Close( fout );
	g_fs.Close( fin );

	return ok;
}

static qboolean TestBasic( const byte *payload, fs_offset_t size )
{
	if( !WriteSourceFile( "fc_src.bin", payload, size ))
		return false;

	if( !DoCopy( "fc_src.bin", "fc_dst.bin", size ))
	{
		printf( "TestBasic(%lld): FileCopy returned false\n", (long long)size );
		return false;
	}

	if( !VerifyFile( "fc_dst.bin", payload, size ))
	{
		printf( "TestBasic(%lld): verify failed\n", (long long)size );
		return false;
	}

	g_fs.Delete( "fc_src.bin" );
	g_fs.Delete( "fc_dst.bin" );
	return true;
}

// drains some bytes via Read first, leaving the input file_t's userspace buffer
// dirty (or its position past the start). FS_FileCopy's sendfile fast-path must
// gate this case off; the buffered fallback must still produce the right bytes.
static qboolean TestPartialReadThenCopy( const byte *payload, fs_offset_t size, fs_offset_t skip )
{
	byte scratch[64];

	if( skip > (fs_offset_t)sizeof( scratch ))
		skip = sizeof( scratch );

	if( !WriteSourceFile( "fc_src.bin", payload, size ))
		return false;

	file_t *fin = g_fs.Open( "fc_src.bin", "rb", true );
	if( !fin )
		return false;

	if( g_fs.Read( fin, scratch, skip ) != skip )
	{
		printf( "TestPartialReadThenCopy: short read\n" );
		g_fs.Close( fin );
		return false;
	}

	if( memcmp( scratch, payload, skip ) != 0 )
	{
		printf( "TestPartialReadThenCopy: pre-read mismatch\n" );
		g_fs.Close( fin );
		return false;
	}

	file_t *fout = g_fs.Open( "fc_dst.bin", "wb", true );
	if( !fout )
	{
		g_fs.Close( fin );
		return false;
	}

	qboolean ok = g_fs.FileCopy( fout, fin, (int)( size - skip ));

	g_fs.Close( fout );
	g_fs.Close( fin );

	if( !ok )
	{
		printf( "TestPartialReadThenCopy: FileCopy returned false\n" );
		return false;
	}

	if( !VerifyFile( "fc_dst.bin", payload + skip, size - skip ))
		return false;

	g_fs.Delete( "fc_src.bin" );
	g_fs.Delete( "fc_dst.bin" );
	return true;
}

static qboolean TestFileCopy( void )
{
	enum { BIG_SIZE = 2 * 1024 * 1024 + 7777 }; // > FILE_COPY_SIZE to force multi-iteration

	g_fs.AddGameDirectory( "./", FS_GAMEDIR_PATH );

	byte *payload = malloc( BIG_SIZE );
	if( !payload )
		return false;

	for( fs_offset_t i = 0; i < BIG_SIZE; i++ )
		payload[i] = (byte)( ( i * 2654435761u ) >> 24 );

	if( !TestBasic( payload, 0 ))
		goto fail;
	if( !TestBasic( payload, 1 ))
		goto fail;
	if( !TestBasic( payload, 4095 ))
		goto fail;
	if( !TestBasic( payload, BIG_SIZE ))
		goto fail;

	if( !TestPartialReadThenCopy( payload, BIG_SIZE, 33 ))
		goto fail;

	free( payload );
	return true;

fail:
	free( payload );
	g_fs.Delete( "fc_src.bin" );
	g_fs.Delete( "fc_dst.bin" );
	return false;
}

int main( void )
{
	if( !LoadFilesystem() )
		return EXIT_FAILURE;

	srand( time( NULL ));

	if( !TestFileCopy())
		return EXIT_FAILURE;

	FreeLibrary( g_hModule );

	printf( "success\n" );

	return EXIT_SUCCESS;
}
