/*
titles.c - titles.txt file parser
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "tests.h"

client_textmessage_t gMessageParms;

static qboolean TitleParseDirective( const char *line )
{
	char directive[64];

	char *p = COM_ParseFile((char *)line, directive, sizeof( directive ));
	if( !p || directive[0] != '$' )
		return false;

	int nargs;
	if( !Q_stricmp( directive, "$position" ))
		nargs = 2;
	else if( !Q_stricmp( directive, "$effect" ) || !Q_stricmp( directive, "$fadein" ) ||
	         !Q_stricmp( directive, "$fadeout" ) || !Q_stricmp( directive, "$holdtime" ) ||
	         !Q_stricmp( directive, "$fxtime" ))
		nargs = 1;
	else if( !Q_stricmp( directive, "$color" ) || !Q_stricmp( directive, "$color2" ))
		nargs = 3;
	else
	{
		Con_DPrintf( S_ERROR "unknown directive: %s\n", directive );
		return true;
	}

	float args[3] = { 0 };
	for( int i = 0; i < nargs; i++ )
	{
		char numstr[32];
		p = COM_ParseFile( p, numstr, sizeof( numstr ));

		if( !p )
			return true; // not enough arguments

		args[i] = Q_atof( numstr );
	}

	if( !Q_stricmp( directive, "$position" ))
	{
		gMessageParms.x = args[0];
		gMessageParms.y = args[1];
	}
	else if( !Q_stricmp( directive, "$color" ))
	{
		gMessageParms.r1 = args[0];
		gMessageParms.g1 = args[1];
		gMessageParms.b1 = args[2];
	}
	else if( !Q_stricmp( directive, "$color2" ))
	{
		gMessageParms.r2 = args[0];
		gMessageParms.g2 = args[1];
		gMessageParms.b2 = args[2];
	}
	else if( !Q_stricmp( directive, "$effect" ))
		gMessageParms.effect = args[0];
	else if( !Q_stricmp( directive, "$fadein" ))
		gMessageParms.fadein = args[0];
	else if( !Q_stricmp( directive, "$fadeout" ))
		gMessageParms.fadeout = args[0];
	else if( !Q_stricmp( directive, "$holdtime" ))
		gMessageParms.holdtime = args[0];
	else if( !Q_stricmp( directive, "$fxtime" ))
		gMessageParms.fxtime = args[0];

	return true;
}

// fast first pass: count the number of complete name { text } blocks
static int TitleCountMessages( char *pfile, int fileSize )
{
	char line[512];
	int filepos = 0, count = 0;
	qboolean in_text = false;

	while( Q_memfgets( pfile, fileSize, &filepos, line, sizeof( line )) != NULL )
	{
		char trim[512];
		COM_TrimSpace( trim, line, sizeof( trim ));

		if( !in_text )
		{
			if( trim[0] == '{' )
				in_text = true;
		}
		else if( trim[0] == '}' )
		{
			count++;
			in_text = false;
		}
	}
	return count;
}

client_textmessage_t *CL_TextMessageParse( poolhandle_t mempool, char *pfile, int fileSize, int *numTitles )
{
	int total = TitleCountMessages( pfile, fileSize );

	if( !total )
	{
		*numTitles = 0;
		return NULL;
	}

	size_t bufsize = total * sizeof( client_textmessage_t );
	client_textmessage_t *out = Mem_Calloc( mempool, bufsize );

	char line[512], curname[512];
	int filepos = 0, linenum = 0, count = 0, textsegstart = 0;
	qboolean in_text = false;

	while( 1 )
	{
		int curlinestart = filepos;

		if( Q_memfgets( pfile, fileSize, &filepos, line, sizeof( line )) == NULL )
			break;

		linenum++;

		char trim[512];
		COM_TrimSpace( trim, line, sizeof( trim ));

		if( !in_text )
		{
			if( COM_StringEmpty( trim ) || ( trim[0] == '/' && trim[1] == '/' ))
				continue;

			if( TitleParseDirective( trim ))
				continue;

			if( trim[0] == '{' )
			{
				in_text = true;
				textsegstart = filepos;
				continue;
			}

			if( trim[0] == '}' )
			{
				Con_Reportf( "%s: unexpected '}' at line %d\n", __func__, linenum );
				Mem_Free( out );
				return NULL;
			}

			Q_strncpy( curname, trim, sizeof( curname ));
		}
		else
		{
			if( trim[0] == '{' )
			{
				Con_Reportf( "%s: unexpected '{' at line %d\n", __func__, linenum );
				Mem_Free( out );
				return NULL;
			}

			if( trim[0] == '}' )
			{
				size_t textlen = Q_max( curlinestart - 1 - textsegstart, 0 );
				size_t namelen = Q_strlen( curname );
				size_t nameoffset = bufsize;
				size_t textoffset = nameoffset + namelen + 1;

				bufsize = textoffset + textlen + 1;
				out = Mem_Realloc( mempool, out, bufsize );

				char *p = (char *)out;

				memcpy( &p[nameoffset], curname, namelen + 1 );
				memcpy( &p[textoffset], &pfile[textsegstart], textlen );
				p[textoffset + textlen] = '\0';

				// store offsets as pointers temporarily
				out[count] = gMessageParms;
				out[count].pName = (char *)nameoffset;
				out[count].pMessage = (char *)textoffset;
				count++;
				in_text = false;
			}
		}
	}

	if( unlikely( count != total ))
		Con_DPrintf( S_ERROR "%s: expected %d messages, parsed %d\n", __func__, total, count );

	// fix up offsets
	for( int i = 0; i < count; i++ )
	{
		out[i].pName = (char *)out + (uintptr_t)out[i].pName;
		out[i].pMessage = (char *)out + (uintptr_t)out[i].pMessage;
	}

	Con_Reportf( "%s: parsed %d text messages\n", __func__, count );
	*numTitles = count;
	return out;
}

#if XASH_ENGINE_TESTS

void Test_RunTitles( void )
{
	poolhandle_t mempool = Mem_AllocPool( __func__ );
	client_textmessage_t *tmessages;
	client_textmessage_t *null;
	int num_titles = 0, num_null_titles = 0;
	char titles[] =
	"// this is a comment\n"
	"$effect 2\n"
	"$color 1 2 3\n"
	"$color2 4 5 6\n"
	"$position 7 8\n"
	"$fadein 0.1\n"
	"$fadeout 0.5\n"
	"$holdtime 321\n"
	"$fxtime 123\n"
	"TITLE\n"
	"{\n"
	"Hello to anybody reading test data\n"
	"Hope you have a good time\n"
	"}\n"
	"\n"
	"TITLE2\n"
	"{\n"
	"Still reading that nonsense huh?\n"
	"}\n"
	"// let's override some things\n"
	"$cats are\n"
	"$cute\n"
	"$color 10 10 10\n"
	"$position -1 -1\n"
	"UwU\n"
	"{\n"
	"OwO\n"
	"}\n"
	"Technically titles can have spaces and // can't have comments\n"
	"{\n"
	"Oh yeah!\n"
	"}\n";
	char broken_titles[] = "}\n";
	char broken_titles2[] = "{\n{\n";

	null = CL_TextMessageParse( mempool, broken_titles, Q_strlen( broken_titles ), &num_null_titles );
	TASSERT_EQi( num_null_titles, 0 );
	TASSERT_EQp( null, NULL );

	null = CL_TextMessageParse( mempool, broken_titles2, Q_strlen( broken_titles2 ), &num_null_titles );
	TASSERT_EQi( num_null_titles, 0 );
	TASSERT_EQp( null, NULL );

	tmessages = CL_TextMessageParse( mempool, titles, Q_strlen( titles ), &num_titles );
	TASSERT_EQi( num_titles, 4 );
	TASSERT_NEQp( tmessages, NULL );

	for( int i = 0; i < 4; i++ )
	{
		TASSERT_EQi( tmessages[i].effect, 2 );
		TASSERT_EQi( tmessages[i].r1, i >= 2 ? 10 : 1 );
		TASSERT_EQi( tmessages[i].g1, i >= 2 ? 10 : 2 );
		TASSERT_EQi( tmessages[i].b1, i >= 2 ? 10 : 3 );
		TASSERT_EQi( tmessages[i].a1, 0 );
		TASSERT_EQi( tmessages[i].r2, 4 );
		TASSERT_EQi( tmessages[i].g2, 5 );
		TASSERT_EQi( tmessages[i].b2, 6 );
		TASSERT_EQi( tmessages[i].a2, 0 );
		TASSERT_EQi( tmessages[i].x, i >= 2 ? -1 : 7 );
		TASSERT_EQi( tmessages[i].y, i >= 2 ? -1 : 8 );
		TASSERT_EQi( tmessages[i].fadein, 0.1f );
		TASSERT_EQi( tmessages[i].fadeout, 0.5f );
		TASSERT_EQi( tmessages[i].holdtime, 321.f );
		TASSERT_EQi( tmessages[i].fxtime, 123.f );
	}

	TASSERT_STR( tmessages[0].pName, "TITLE" );
	TASSERT_STR( tmessages[1].pName, "TITLE2" );
	TASSERT_STR( tmessages[2].pName, "UwU" );
	TASSERT_STR( tmessages[3].pName, "Technically titles can have spaces and // can't have comments" );

	TASSERT_STR( tmessages[0].pMessage, "Hello to anybody reading test data\nHope you have a good time" );
	TASSERT_STR( tmessages[1].pMessage, "Still reading that nonsense huh?" );
	TASSERT_STR( tmessages[2].pMessage, "OwO" );
	TASSERT_STR( tmessages[3].pMessage, "Oh yeah!" );

	Mem_Free( tmessages );
	Mem_FreePool( &mempool );
}

#endif // XASH_ENGINE_TESTS
