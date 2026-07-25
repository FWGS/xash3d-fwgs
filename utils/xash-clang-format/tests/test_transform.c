#include <stdio.h>
#include <string.h>
#include "xcf.h"

static int run_case( const char *label, const char *in, const char *expect )
{
	xcf_buf_t out = { 0 };
	int ok;

	if( !Transform( in, strlen( in ), &out ))
	{
		fprintf( stderr, "[%s] Transform returned error\n", label );
		BufFree( &out );
		return 0;
	}

	ok = ( out.len == strlen( expect ) && memcmp( out.data, expect, out.len ) == 0 );

	if( !ok )
	{
		fprintf( stderr, "[%s] mismatch\n  in:     %s\n  out:    %.*s\n  expect: %s\n",
			label, in, (int)out.len, out.data, expect );
	}

	BufFree( &out );
	return ok;
}

int main( void )
{
	int n = 0;

	if( !run_case( "outer parens",
			"if( ( a + b ) )\n",
			"if(( a + b ))\n" ))
		return ++n;

	if( !run_case( "triple nesting",
			"x = ( ( ( a ) ) );\n",
			"x = ((( a )));\n" ))
		return ++n;

	if( !run_case( "mixed [ )",
			"arr[i] )\n",
			"arr[i] )\n" ))
		return ++n;
	if( !run_case( "mixed ) ]",
			"f( a ) ];\n",
			"f( a ) ];\n" ))
		return ++n;

	if( !run_case( "newline boundary",
			"(\n(\n",
			"(\n(\n" ))
		return ++n;

	if( !run_case( "string literal",
			"x = \"( ( hello ) )\";\n",
			"x = \"( ( hello ) )\";\n" ))
		return ++n;

	if( !run_case( "escaped quote in string",
			"x = \"a \\\" ( ( b ) ) \";\n",
			"x = \"a \\\" ( ( b ) ) \";\n" ))
		return ++n;

	if( !run_case( "line comment",
			"a; // ( ( comment ) )\nb;\n",
			"a; // ( ( comment ) )\nb;\n" ))
		return ++n;

	if( !run_case( "block comment",
			"/* ( ( leave\nthis ) ) alone */ x;\n",
			"/* ( ( leave\nthis ) ) alone */ x;\n" ))
		return ++n;

	if( !run_case( "char literal '('",
			"if( c == '(' )\n",
			"if( c == '(' )\n" ))
		return ++n;

	if( !run_case( "raw string",
			"const char *s = R\"x(( (\nstays )x\";\n",
			"const char *s = R\"x(( (\nstays )x\";\n" ))
		return ++n;

	if( !run_case( "raw string empty delim",
			"R\"(( (\n))\"",
			"R\"(( (\n))\"" ))
		return ++n;

	if( !run_case( "ident Rabbit not raw",
			"int Rabbit = ( ( 1 ) );\n",
			"int Rabbit = (( 1 ));\n" ))
		return ++n;

	if( !run_case( "u8R raw",
			"const auto s = u8R\"(( ( )\";\n",
			"const auto s = u8R\"(( ( )\";\n" ))
		return ++n;

	if( !run_case( "include angle",
			"#include <foo.h>\nf( ( x ) );\n",
			"#include <foo.h>\nf(( x ));\n" ))
		return ++n;

	if( !run_case( "tabs between parens",
			"f(\t(\tx\t)\t);\n",
			"f((\tx\t));\n" ))
		return ++n;

	if( !run_case( "empty", "", "" ))
		return ++n;

	if( !run_case( "idempotent",
			"f((( x )));\n",
			"f((( x )));\n" ))
		return ++n;

	if( !run_case( "already touching",
			"((a))",
			"((a))" ))
		return ++n;

	if( !run_case( "single paren",
			"f( a, b )",
			"f( a, b )" ))
		return ++n;

	if( !run_case( "multiple pairs",
			"f( ( a ) ) + g( ( b ) );\n",
			"f(( a )) + g(( b ));\n" ))
		return ++n;

	if( !run_case( "L wide string",
			"wchar_t *s = L\"( ( x ) )\";\n",
			"wchar_t *s = L\"( ( x ) )\";\n" ))
		return ++n;

	if( !run_case( "u utf16 string",
			"auto s = u\"( ( x ) )\";\n",
			"auto s = u\"( ( x ) )\";\n" ))
		return ++n;

	if( !run_case( "U utf32 string",
			"auto s = U\"( ( x ) )\";\n",
			"auto s = U\"( ( x ) )\";\n" ))
		return ++n;

	if( !run_case( "u8 utf8 string",
			"auto s = u8\"( ( x ) )\";\n",
			"auto s = u8\"( ( x ) )\";\n" ))
		return ++n;

	if( !run_case( "L wide char",
			"if( c == L'(' )\n",
			"if( c == L'(' )\n" ))
		return ++n;

	if( !run_case( "u8 char",
			"if( c == u8'(' )\n",
			"if( c == u8'(' )\n" ))
		return ++n;

	if( !run_case( "ident L not prefix",
			"int callL = ( ( 1 ) );\n",
			"int callL = (( 1 ));\n" ))
		return ++n;

	return 0;
}
