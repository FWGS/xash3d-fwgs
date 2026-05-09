#include "xash3d_mathlib.h"
#include "crtlib.h"

static int Test_FileExtension( void )
{
	static const char *strings[][2] =
	{
	{ "test.txt", "txt" },
	{ "path/to/file.wad", "wad" },
	{ "noext", "" },
	{ "dir/", "" },
	{ "dir.pk3dir/", "" },
	{ "https.proto://is_this_an_url?", "" },
	{ "inside.wad/AAATRIGGER", "" },
	{ "c:/games/gamedir/liblist.cum", "cum" },
	{ "", "" },
	{ "trailingdot.", "" },
	{ ".hidden", "hidden" },
	{ "multi.dot.ext", "ext" },
	{ "../relative/path", "" },
	{ "../relative/file.bsp", "bsp" },
	};

	for( int i = 0; i < ARRAYSIZE( strings ); i++ )
	{
		const char *got = COM_FileExtension( strings[i][0] );

		if( Q_strcmp( got, strings[i][1] ))
			return i + 1;
	}

	return 0;
}

static int Test_StripExtension( void )
{
	static const char *strings[][2] =
	{
	{ "test.txt", "test" },
	{ "path/to/file.wad", "path/to/file" },
	{ "noext", "noext" },
	{ "dir/", "dir/" },
	{ "dir.pk3dir/", "dir.pk3dir/" },
	{ "inside.wad/AAATRIGGER", "inside.wad/AAATRIGGER" },
	{ "https.proto://is_this_an_url?", "https.proto://is_this_an_url?" },
	{ "c:/games/gamedir/liblist.cum", "c:/games/gamedir/liblist" },
	{ "multi.dot.ext", "multi.dot" },
	{ "trailingdot.", "trailingdot." },
	{ ".hidden", "" },
	{ "", "" },
	{ "..", ".." },
	};

	for( int i = 0; i < ARRAYSIZE( strings ); i++ )
	{
		string s;

		Q_strncpy( s, strings[i][0], sizeof( s ));
		COM_StripExtension( s );

		if( Q_strcmp( s, strings[i][1] ))
			return i + 1;
	}

	return 0;
}

static int Test_DefaultExtension( void )
{
	static const char *strings[][3] =
	{
	{ "file", ".bsp", "file.bsp" },
	{ "path/to/file", ".wad", "path/to/file.wad" },
	{ "file.txt", ".bsp", "file.txt" },
	{ "path/to/file.bsp", ".wad", "path/to/file.bsp" },
	{ "c:/games/gamedir/liblist.cum", ".bsp", "c:/games/gamedir/liblist.cum" },
	{ "dir/", ".bsp", "dir/.bsp" },
	{ "dir.pk3dir/", ".bsp", "dir.pk3dir/.bsp" },
	{ "inside.wad/AAATRIGGER", ".bsp", "inside.wad/AAATRIGGER.bsp" },
	{ "trailingdot.", ".bsp", "trailingdot..bsp" },
	{ "", ".bsp", ".bsp" },
	};

	for( int i = 0; i < ARRAYSIZE( strings ); i++ )
	{
		string s;

		Q_strncpy( s, strings[i][0], sizeof( s ));
		COM_DefaultExtension( s, strings[i][1], sizeof( s ));

		if( Q_strcmp( s, strings[i][2] ))
			return i + 1;
	}

	return 0;
}

int main( int argc, char **argv )
{
	int ret = Test_FileExtension();
	if( ret )
		return ret;

	ret = Test_StripExtension();
	if( ret )
		return ret + 32;

	ret = Test_DefaultExtension();
	if( ret )
		return ret + 64;

	return 0;
}
