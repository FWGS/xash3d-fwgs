#include <stdio.h>
#include "xcf.h"

static int run_case( const char *label, const char *s, qboolean expect_ok, int expect_major )
{
	int major = -1;
	qboolean ok = ParseVersion( s, &major );

	if( ok != expect_ok )
	{
		fprintf( stderr, "[%s] ok=%d expected %d (input: %s)\n", label, ok, expect_ok, s ? s : "(null)" );
		return 0;
	}
	if( ok && major != expect_major )
	{
		fprintf( stderr, "[%s] major=%d expected %d (input: %s)\n", label, major, expect_major, s );
		return 0;
	}
	return 1;
}

int main( void )
{
	int n = 0;

	if( !run_case( "vanilla 17",
			"clang-format version 17.0.6 (Fedora 17.0.6-1.fc40)\n", true, 17 ))
		return ++n;

	if( !run_case( "ubuntu 18",
			"Ubuntu clang-format version 18.1.3 (1ubuntu1)\n", true, 18 ))
		return ++n;

	if( !run_case( "large major",
			"clang-format version 100.0.0\n", true, 100 ))
		return ++n;

	if( !run_case( "garbage",
			"this has no version here\n", false, 0 ))
		return ++n;

	if( !run_case( "null", NULL, false, 0 ))
		return ++n;

	if( !run_case( "distro tag with digits",
			"Ubuntu99 clang-format version 17.0.6\n", true, 17 ))
		return ++n;

	if( !run_case( "bare digits",
			"foo 42 bar\n", false, 0 ))
		return ++n;

	if( !run_case( "no version word",
			"fake clang-format 19.1.0\n", false, 0 ))
		return ++n;

	return 0;
}
