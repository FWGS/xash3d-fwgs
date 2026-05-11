#include <stdio.h>
#include "crtlib.h"

static const char *test_file =
"q asdf \"qwerty\" \"f \\\"f\" meowmeow\n"
"// comment \"stuff ignored\"\n"
"bark // ignore\n"
"bashlikecomment	#notignored	test\n"
"#ignore comment\n"
"thisshall #be ignored\n"
"test_sentinel\n"
"ignore:colon\n"
"notignore:colon\n"
"(a b)\n"
"\"test1\"\n"
"// comment\n"
"'a 'b\n"
",a ,b\n"
;

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
	{ 32, "test", 4, PFILE_HASH_AS_COMMENT },
	{ 32, "thisshall", 9, PFILE_HASH_AS_COMMENT },
	{ 32, "test_sentinel", 13, PFILE_HASH_AS_COMMENT },
	{ 32, "ignore:colon", 12, },
	{ 32, "notignore", 9, PFILE_COLON_AS_TOKEN },
	{ 32, ":", 1, PFILE_COLON_AS_TOKEN },
	{ 32, "colon", 5, PFILE_COLON_AS_TOKEN },
	{ 32, "(a", 2, PFILE_NO_BRACKETS_AS_TOKEN },
	{ 32, "b", 1, },
	{ 32, ")", 1, },
	{ 32, "\"test1\"", 7, PFILE_NO_QUOTED_TOKENS },
	{ 32, "\n", 1, PFILE_NEWLINE_AS_TOKEN },
	{ 32, "'a", 2, PFILE_NO_SINGLE_QUOTE_AS_TOKEN },
	{ 32, "'", 1, },
	{ 32, "b", 1, },
	{ 32, ",a", 2, PFILE_NO_COMMA_AS_TOKEN },
	{ 32, ",", 1, },
	{ 32, "b", 1, },
	{ 32, NULL }, // EOF
	};

	for( i = 0; i < sizeof( testdata ) / sizeof( testdata[0] ); i++ )
	{
		string buf;
		int len;

		file = COM_ParseFileSafe( file, buf, testdata[i].bufsize, testdata[i].flags, &len, NULL );

		if( testdata[i].expected == NULL && file != NULL )
		{
			printf( "expected NULL, but got no NULL\n" );
			return i;
		}

		if( testdata[i].expected != NULL && file == NULL )
		{
			printf( "expected no NULL, but got NULL\n" );
			return i;
		}

		if( testdata[i].expected == NULL && file == NULL )
			continue;

		if( !( !Q_strcmp( buf, testdata[i].expected ) && len == testdata[i].expected_len ))
		{
			printf( "expected '%s' with length %d, got '%s' with length %d\n", testdata[i].expected, testdata[i].expected_len, buf, len );
			return i;
		}
	}

	return 0;
}
