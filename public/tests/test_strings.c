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

static int Test_FixSlashes( void )
{
	string s = "path\\with\\back\\slashes";
	string s2 = "path/with/fwd/slashes";
	string s3 = "path\\with/mixed\\slashes";

	COM_FixSlashes( s );

	if( Q_strcmp( s, "path/with/back/slashes" ))
		return 1;

	COM_FixSlashes( s2 );

	if( Q_strcmp( s2, "path/with/fwd/slashes" ))
		return 2;

	COM_FixSlashes( s3 );

	if( Q_strcmp( s3, "path/with/mixed/slashes" ))
		return 3;

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

	ret = Test_FixSlashes();

	if( ret > 0 )
		return ret + 48;

	return 0;
}
