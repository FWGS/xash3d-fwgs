/*
config.c - .clang-format extension parser
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <string.h>
#include "crtlib.h"
#include "xcf.h"

#define XCF_KEY "XashCollapseParens"

/*
============
ConfigDefault
============
*/
static void ConfigDefault( xcf_config_t *c )
{
	c->collapse_parens = false;
}

/*
============
SkipWhitespace
============
*/
static const char *SkipWhitespace( const char *p, const char *end )
{
	for( ; p < end && ( *p == ' ' || *p == '\t' ); p++ )
		;
	return p;
}

/*
============
ParseConfigText

Parse extension keys out of a YAML text body.
We only look at lines that begin (after optional whitespace) with `#`.
============
*/
qboolean ParseConfigText( const char *text, xcf_config_t *cfg )
{
	ConfigDefault( cfg );
	if( !text )
		return true;

	for( const char *p = text; *p; )
	{
		const char *end = Q_strchrnul( p, '\n' );
		const char *q;
		const size_t klen = sizeof( XCF_KEY ) - 1;

		q = SkipWhitespace( p, end );

		if( q < end && *q == '#' )
		{
			q = SkipWhitespace( q + 1, end );

			if( end - q >= klen && memcmp( q, XCF_KEY, klen ) == 0 )
			{
				q = SkipWhitespace( q + klen, end );

				if( q < end && *q == ':' )
				{
					q = SkipWhitespace( q + 1, end );

					if( end - q >= 4 && memcmp( q, "true", 4 ) == 0 )
						cfg->collapse_parens = true;
					else if( end - q >= 5 && memcmp( q, "false", 5 ) == 0 )
						cfg->collapse_parens = false;
				}
			}
		}

		if( !*end )
			break;
		p = end + 1;
	}

	return true;
}
