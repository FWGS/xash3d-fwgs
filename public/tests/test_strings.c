#include "crtlib.h"

static int Test_Strcpycatcmp( void )
{
	char buf[] = "meowmeowmeow", buf2[] = "barkbark";
	char dst[64];

	if( Q_strncpy( dst, buf, sizeof( dst )) != sizeof( buf ) - 1 )
		return 1;

	if( Q_strcmp( dst, buf ))
		return 2;

	if( Q_strcmp( dst, buf ))
		return 4;

	if( !Q_strcmp( dst, buf2 ))
		return 5;

	if( Q_strncat( dst, buf2, sizeof( dst )) != sizeof( buf ) + sizeof( buf2 ) - 2 )
		return 6;

	if( Q_strcmp( dst, "meowmeowmeowbarkbark" ))
		return 7;

	if( Q_strncmp( dst, buf, sizeof( buf ) - 1 ))
		return 8;

	return 0;
}

static int Test_Strnlwr( void )
{
	string s;

	Q_strnlwr( "ASDFGKJ", s, sizeof( s ));

	if( Q_strcmp( s, "asdfgkj" ))
		return 1;

	Q_strnlwr( "qwertyuiop", s, sizeof( s ));

	if( Q_strcmp( s, "qwertyuiop" ))
		return 2;

	return 0;
}

static int Test_Colorstr( void )
{
	if( Q_colorstr( "^1color^2string" ) != 4 )
		return 1;

	if( Q_colorstr( "colorlessstring" ) != 0 )
		return 2;

	return 0;
}

int main( void )
{
	int ret = Test_Strcpycatcmp();

	if( ret > 0 )
		return ret;

	ret = Test_Strnlwr();

	if( ret > 0 )
		return ret + 16;

	ret = Test_Colorstr();

	if( ret > 0 )
		return ret + 32;

	return 0;
}
