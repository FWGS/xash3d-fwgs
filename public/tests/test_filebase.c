#include <stdlib.h>
#include <stdio.h>
#include "xash3d_mathlib.h"
#include "crtlib.h"

static void COM_FileBase_old( const char *in, char *out, size_t size )
{
	int	len, start, end;

	len = Q_strlen( in );
	if( !len || !size ) return;

	// scan backward for '.'
	end = len - 1;

	while( end && in[end] != '.' && in[end] != '/' && in[end] != '\\' )
		end--;

	if( in[end] != '.' )
		end = len-1; // no '.', copy to end
	else end--; // found ',', copy to left of '.'

	// scan backward for '/'
	start = len - 1;

	while( start >= 0 && in[start] != '/' && in[start] != '\\' )
		start--;

	if( start < 0 || ( in[start] != '/' && in[start] != '\\' ))
		start = 0;
	else start++;

	// length of new sting
	len = end - start + 1;

	if( size > len ) // patched to somewhat support size limit
	{
		// Copy partial string
		Q_strncpy( out, &in[start], len + 1 );
		out[len] = 0;
	}
	else
	{
		Q_strncpy( out, &in[start], size );
	}
}

#define COM_FILEBASE_CHECK( in, out, size, expected ) \
	COM_FileBase( in, out, size );     \
	if( Q_strcmp( out, expected ))     \
	{                                  \
		printf( "%d: fail with libpublic impl, got: %s, expected: %s\n", __LINE__, out, expected ); \
		return 1;                      \
	}                                  \
	COM_FileBase_old( in, out, size ); \
	if( Q_strcmp( out, expected ))     \
	{                                  \
		printf( "%d: fail with old impl, got: %s, expected: %s\n", __LINE__, out, expected ); \
		return 2;                      \
	}

static int Test_FileBase( void )
{
	string s;

	// test slashless case
	COM_FILEBASE_CHECK( "asdf", s, sizeof( s ), "asdf" );

	// test slashless case with extension
	COM_FILEBASE_CHECK( "sdf.wad", s, sizeof( s ), "sdf" );

	// test case with one slash
	COM_FILEBASE_CHECK( "zxcv/asdfqwer", s, sizeof( s ), "asdfqwer" );

	// test case with multiple slashes
	COM_FILEBASE_CHECK( "zxc/asd/qwert", s, sizeof( s ), "qwert" );

	// test case with full path
	COM_FILEBASE_CHECK( "zxc/asd/qvert.lkjefgkljh", s, sizeof( s ), "qvert" );

	// test case where dot placed before last slash
	COM_FILEBASE_CHECK( "pak0.pk3/texture", s, sizeof( s ), "texture" );

	// test case of directory path
	COM_FILEBASE_CHECK( "pak0.pk3/", s, sizeof( s ), "" );

	// test case of file with no name
	COM_FILEBASE_CHECK( "blep/.nomedia", s, sizeof( s ), "" );

	// test idiot cases
	COM_FILEBASE_CHECK( NULL, s, sizeof( s ), "" );
	COM_FILEBASE_CHECK( "", s, sizeof( s ), "" );
	COM_FILEBASE_CHECK( "jhnwrgkujihrgwfikouj", s, 0, "" );

	// test length limit
	COM_FILEBASE_CHECK( "qwertyuiop", s, 3, "qw" );
	COM_FILEBASE_CHECK( "qwertyuiop", s, 1, "" );

	return 0;
}

int main( void )
{
	int ret;

	ret = Test_FileBase();

	if( ret )
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
