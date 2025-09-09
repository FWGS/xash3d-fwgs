#include "crtlib.h"

int main( int argc, char **argv )
{
	int i;
	const char *strings[] =
	{
		"test.txt", "txt",
		"path/to/file.wad", "wad",
		"noext", "",
		"dir/", "",
		"dir.pk3dir/", "",
		"https.proto://is_this_an_url?", "",
		"inside.wad/AAATRIGGER", "",
		"c:/games/gamedir/liblist.cum", "cum",
	};

	for( i = 0; i < sizeof( strings ) / sizeof( strings[0] ); i += 2 )
	{
		if( Q_strcmp( COM_FileExtension( strings[i] ), strings[i + 1] ))
			return i;
	}

	return 0;
}
