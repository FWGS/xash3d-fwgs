#include "crtlib.h"

int main( void )
{
	char *file = (char *)"q asdf \"qwerty\" \"f \\\"f\" meowmeow\n// comment \"stuff ignored\"\nbark";
	int len;
	char buf[5];

	file = COM_ParseFileSafe( file, buf, sizeof( buf ), 0, &len, NULL );
	if( !( !Q_strcmp( buf, "q" ) && len == 1 ))
		return 1;

	file = COM_ParseFileSafe( file, buf, sizeof( buf ), 0, &len, NULL );
	if( !( !Q_strcmp( buf, "asdf" ) && len == 4 ))
		return 2;

	file = COM_ParseFileSafe( file, buf, sizeof( buf ), 0, &len, NULL );
	if( !( !Q_strcmp( buf, "qwer" ) && len == -1 ))
		return 3;

	file = COM_ParseFileSafe( file, buf, sizeof( buf ), 0, &len, NULL );
	if( !( !Q_strcmp( buf, "f \"f" ) && len == 4 ))
		return 4;

	file = COM_ParseFileSafe( file, buf, sizeof( buf ), 0, &len, NULL );
	if( !( !Q_strcmp( buf, "meow" ) && len == -1 ))
		return 5;

	file = COM_ParseFileSafe( file, buf, sizeof( buf ), 0, &len, NULL );
	if( !( !Q_strcmp( buf, "bark" ) && len == 4 ))
		return 6;

	return 0;
}
