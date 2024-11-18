#include "crtlib.h"

static const char *test_file =
"q asdf \"qwerty\" \"f \\\"f\" meowmeow\n"
"// comment \"stuff ignored\"\n"
"bark // ignore\n"
"bashlikecomment	#notignored	test\n"
"#ignore comment\n"
"thisshall #be ignored\n"
"test_sentinel\n";

int main( void )
{
	int i;
	char *file = (char *)test_file;
	struct test
	{
		int bufsize;
		const char *expected;
		int expected_len;
		int flags;
	} testdata[] =
	{
	{ 5, "q", 1 },
	{ 5, "asdf", 4 },
	{ 5, "qwer", -1 },
	{ 5, "f \"f", 4 },
	{ 5, "meow", -1 },
	{ 5, "bark", 4 },
	{ 32, "bashlikecomment", 15 },
	{ 32, "#notignored", 11 },
	{ 32, "test", 4, PFILE_IGNOREHASHCMT },
	{ 32, "thisshall", 9, PFILE_IGNOREHASHCMT },
	{ 32, "test_sentinel", 13, PFILE_IGNOREHASHCMT },
	};

	for( i = 0; i < sizeof( testdata ) / sizeof( testdata[0] ); i++ )
	{
		string buf;
		int len;

		file = COM_ParseFileSafe( file, buf, testdata[i].bufsize, testdata[i].flags, &len, NULL );

		if( file == NULL )
			return i;

		if( !( !Q_strcmp( buf, testdata[i].expected ) && len == testdata[i].expected_len ))
			return i;
	}

	return 0;
}
