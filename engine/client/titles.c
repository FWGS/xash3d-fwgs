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

	client_textmessage_t *null = CL_TextMessageParse( mempool, broken_titles, Q_strlen( broken_titles ), &num_null_titles );
	TASSERT_EQi( num_null_titles, 0 );
	TASSERT_EQp( null, NULL );

	null = CL_TextMessageParse( mempool, broken_titles2, Q_strlen( broken_titles2 ), &num_null_titles );
	TASSERT_EQi( num_null_titles, 0 );
	TASSERT_EQp( null, NULL );

	client_textmessage_t *tmessages = CL_TextMessageParse( mempool, titles, Q_strlen( titles ), &num_titles );
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

#if XASH_GAMECUBE
/*
==============================================================================

GameCube HudText fallback

At HUD quality 0 the GameCube client.dll registers only its combat messages,
so the env_message credits of the intro (CR27, CR1..CR8 on c0a0) arrive as
HudText with nobody to draw them. Queue them here and draw them over the
Flipper HUD pass with the effects CHudMessage uses: 0 fade in/out, 1 flicker
in, 2 write-out with a highlight colour that trails the leading character.

==============================================================================
*/
#define GC_HUDTEXT_SLOTS 4

typedef struct
{
	client_textmessage_t *msg;
	double start;
	int frames;	/* frames drawn, for the fired/displayed log */
} gc_hudtext_t;

static gc_hudtext_t gc_hudtext[GC_HUDTEXT_SLOTS];

void CL_GCHudTextShow( const char *name )
{
	client_textmessage_t *msg;
	int i, slot = 0;

	if( COM_StringEmptyOrNULL( name ))
		return;
	msg = CL_TextMessageGet( name );
	if( !msg || !msg->pMessage )
	{
		Con_Reportf( "Xash3D GameCube: HudText %s has no title\n", name );
		return;
	}
	/* Same message restarts in place; otherwise take a free or the oldest slot. */
	for( i = 0; i < GC_HUDTEXT_SLOTS; i++ )
	{
		if( gc_hudtext[i].msg == msg || !gc_hudtext[i].msg )
		{
			slot = i;
			break;
		}
		if( gc_hudtext[i].start < gc_hudtext[slot].start )
			slot = i;
	}
	if( gc_hudtext[slot].msg && gc_hudtext[slot].msg != msg )
		Con_Reportf( "Xash3D GameCube: HudText %s evicted by %s after %d frames\n",
			gc_hudtext[slot].msg->pName, name, gc_hudtext[slot].frames );
	gc_hudtext[slot].msg = msg;
	gc_hudtext[slot].start = cl.time;
	gc_hudtext[slot].frames = 0;
	Con_Reportf( "Xash3D GameCube: HudText %s effect=%d at %.2f\n", name, msg->effect, cl.time );
}

static int CL_GCHudTextLineWidth( const char *s, const cl_font_t *font )
{
	int w = 0;

	for( ; *s && *s != '\n'; s++ )
		w += font->charWidths[(byte)*s];
	return (int)( w * font->scale );
}

#define GC_HUDTEXT_MARGIN 8 /* safe area at the 320x240 HUD resolution */

static float CL_GCHudTextPos( float pos, int size, int screen )
{
	float p;

	if( pos == -1.0f )
		p = ( screen - size ) * 0.5f;
	else if( pos < 0.0f )
		p = ( 1.0f + pos ) * screen - size;
	else
		p = pos * screen;
	/* The 2x credits font must stay inside the safe area. */
	if( p + size > screen - GC_HUDTEXT_MARGIN )
		p = screen - GC_HUDTEXT_MARGIN - size;
	if( p < GC_HUDTEXT_MARGIN )
		p = GC_HUDTEXT_MARGIN;
	return p;
}

/* Dark translucent backing behind a title block: the credits font draws
 * additively and its grey final colours vanish on lit tunnel walls. */
static void CL_GCHudTextBacking( const client_textmessage_t *msg, float y, int lines,
	int lineh, int sw, float fade )
{
	const cl_font_t *font = &cls.creditsFont;
	const char *s = msg->pMessage;
	float x0 = (float)sw, x1 = 0.0f, bx, by, bw, bh;

	while( *s )
	{
		const int w = CL_GCHudTextLineWidth( s, font );
		const float x = CL_GCHudTextPos( msg->x, w, sw );

		x0 = Q_min( x0, x );
		x1 = Q_max( x1, x + w );
		while( *s && *s != '\n' )
			s++;
		if( *s == '\n' )
			s++;
	}
	if( x1 <= x0 || fade <= 0.0f )
		return;
	bx = x0 - 4.0f;
	by = y - 2.0f;
	bw = ( x1 - x0 ) + 8.0f;
	bh = (float)( lines * lineh ) + 4.0f;
	SPR_AdjustSize( &bx, &by, &bw, &bh );
	ref.dllFuncs.FillRGBA( kRenderTransTexture, bx, by, bw, bh, 0, 0, 0, (int)( 150.0f * fade ));
}

static void CL_GCHudTextDrawOne( const client_textmessage_t *msg, float t )
{
	cl_font_t *font = &cls.creditsFont;
	const int sw = clgame.scrInfo.iWidth > 0 ? clgame.scrInfo.iWidth : 320;
	const int sh = clgame.scrInfo.iHeight > 0 ? clgame.scrInfo.iHeight : 240;
	const int lineh = (int)( font->charHeight * font->scale );
	const char *s = msg->pMessage;
	int lines = 1, chars = 0, idx = 0;
	float y, fade = 1.0f;

	for( const char *p = s; *p; p++ )
	{
		if( *p == '\n' && p[1] )
			lines++;
		else if( *p != '\n' )
			chars++;
	}

	/* Envelope shared by every effect: hold after the in-phase, then fade. */
	{
		const float in = msg->effect == 2 ? msg->fadein * chars + msg->fxtime : msg->fadein;
		const float out_start = in + msg->holdtime;

		if( msg->effect != 2 && t < msg->fadein && msg->fadein > 0.0f )
			fade = t / msg->fadein;
		else if( t > out_start )
			fade = msg->fadeout > 0.0f ? 1.0f - ( t - out_start ) / msg->fadeout : 0.0f;
		fade = bound( 0.0f, fade, 1.0f );
	}

	y = CL_GCHudTextPos( msg->y, lines * lineh, sh );
	CL_GCHudTextBacking( msg, y, lines, lineh, sw, fade );
	while( *s )
	{
		float x = CL_GCHudTextPos( msg->x, CL_GCHudTextLineWidth( s, font ), sw );

		for( ; *s && *s != '\n'; s++, idx++ )
		{
			float r = msg->r1, g = msg->g1, b = msg->b1, a = fade;
			rgba_t color;

			if( msg->effect == 2 )
			{
				/* Character idx appears at idx*fadein; it shows color2 for
				 * fxtime, then blends to color1. */
				const float appear = idx * msg->fadein;
				float k;

				/* Later characters appear later still: done with the message. */
				if( t < appear )
					return;
				k = msg->fxtime > 0.0f ? ( t - appear ) / msg->fxtime : 1.0f;
				k = bound( 0.0f, k, 1.0f );
				r = msg->r2 + ( msg->r1 - msg->r2 ) * k;
				g = msg->g2 + ( msg->g1 - msg->g2 ) * k;
				b = msg->b2 + ( msg->b1 - msg->b2 ) * k;
			}
			else if( msg->effect == 1 && t < msg->fadein && ( COM_RandomLong( 0, 100 ) < 30 ))
			{
				/* Flickery credits: random characters flash toward color2. */
				r = msg->r2;
				g = msg->g2;
				b = msg->b2;
			}

			/* The credits font draws additively: fade by scaling the colour. */
			color[0] = (byte)( r * a );
			color[1] = (byte)( g * a );
			color[2] = (byte)( b * a );
			color[3] = 255;
			x += CL_DrawCharacter( x, y, (byte)*s, color, font, FONT_DRAW_HUD );
		}
		while( *s && *s != '\n' )
			s++;
		if( *s == '\n' )
			s++;
		y += lineh;
	}
}

/* Map-load renderer trims free textures, so the credits font created at boot
 * can end up pointing at whatever reused its texture slot (that drew the
 * garbled glyphs). Recreate it when the slot no longer holds the font. */
static void CL_GCHudTextEnsureFont( void )
{
	cl_font_t *font = &cls.creditsFont;
	static double next_retry;
	int w = 0, h = 0;

	if( font->valid )
	{
		R_GetTextureParms( &w, &h, font->hFontTexture );
		if( w == 256 && h == 64 )
			return;
		Con_Reportf( "Xash3D GameCube: credits font texture %d is now %dx%d, reloading\n",
			font->hFontTexture, w, h );
		font->valid = false;
	}
	/* A failed load is a disc read: do not retry it every frame. */
	if( host.realtime < next_retry )
		return;
	CL_GCLoadLeanHudFont( font, hud_fontscale.value, &hud_fontrender );
	if( !font->valid )
		next_retry = host.realtime + 1.0;
}

void CL_GCHudTextDraw( void )
{
	int i;

	/* Reload the font as soon as the map is up, before game sounds and
	 * models take the heap, not when the first credit arrives. */
	if( cls.state == ca_active
		|| gc_hudtext[0].msg || gc_hudtext[1].msg || gc_hudtext[2].msg || gc_hudtext[3].msg )
		CL_GCHudTextEnsureFont();

	if( !cls.creditsFont.valid )
	{
		static double warned;

		if(( gc_hudtext[0].msg || gc_hudtext[1].msg || gc_hudtext[2].msg || gc_hudtext[3].msg )
			&& host.realtime > warned )
		{
			warned = host.realtime + 2.0;
			Con_Reportf( "Xash3D GameCube: HudText pending but credits font invalid\n" );
		}
		return;
	}
	for( i = 0; i < GC_HUDTEXT_SLOTS; i++ )
	{
		const client_textmessage_t *msg = gc_hudtext[i].msg;
		float t, total;

		if( !msg )
			continue;
		t = (float)( cl.time - gc_hudtext[i].start );
		/* cl.time is re-lerped after the message is parsed and can step back
		 * a few ms in the same frame: that dropped every other credit before
		 * it was drawn. A bigger step back is a level's clock restarting;
		 * restart the message with it. */
		if( t < 0.0f )
		{
			if( t < -0.5f )
				gc_hudtext[i].start = cl.time;
			t = 0.0f;
		}
		total = ( msg->effect == 2 ? msg->fadein * Q_strlen( msg->pMessage ) + msg->fxtime : msg->fadein )
			+ msg->holdtime + msg->fadeout;
		if( t < 0.0f || t > total )
		{
			Con_Reportf( "Xash3D GameCube: HudText %s done frames=%d t=%.2f/%.2f\n",
				msg->pName, gc_hudtext[i].frames, t, total );
			gc_hudtext[i].msg = NULL;
			continue;
		}
		if( gc_hudtext[i].frames++ == 0 )
			Con_Reportf( "Xash3D GameCube: HudText %s displayed at %.2f\n", msg->pName, cl.time );
		CL_GCHudTextDrawOne( msg, t );
	}
}

void CL_GCHudTextClear( void )
{
	int i;

	for( i = 0; i < GC_HUDTEXT_SLOTS; i++ )
		if( gc_hudtext[i].msg )
			Con_Reportf( "Xash3D GameCube: HudText %s cleared after %d frames\n",
				gc_hudtext[i].msg->pName, gc_hudtext[i].frames );
	memset( gc_hudtext, 0, sizeof( gc_hudtext ));
}
#endif // XASH_GAMECUBE
