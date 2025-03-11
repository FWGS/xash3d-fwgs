/*
cl_font.c - bare bones engine font manager
Copyright (C) 2023 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <ctype.h>
#include "common.h"
#include "filesystem.h"
#include "client.h"
#include "qfont.h"
#include "utflib.h"

qboolean CL_FixedFont( cl_font_t *font )
{
	return font && font->valid && font->type == FONT_FIXED;
}

static int CL_LoadFontTexture( const char *fontname, uint texFlags, int *width )
{
	int font_width;
	int tex;

	if( !g_fsapi.FileExists( fontname, false ))
		return 0;

	tex = ref.dllFuncs.GL_LoadTexture( fontname, NULL, 0, texFlags );
	if( !tex )
		return 0;

	font_width = REF_GET_PARM( PARM_TEX_WIDTH, tex );
	if( !font_width )
	{
		ref.dllFuncs.GL_FreeTexture( tex );
		return 0;
	}

	*width = font_width;
	return tex;
}

static int CL_FontRenderMode( convar_t *fontrender )
{
	switch((int)fontrender->value )
	{
	case 0:
		return kRenderTransAdd;
	case 1:
		return kRenderTransAlpha;
	case 2:
		return kRenderTransTexture;
	default:
		Cvar_DirectSet( fontrender, fontrender->def_string );
	}

	return kRenderTransTexture;
}

void CL_SetFontRendermode( cl_font_t *font )
{
	ref.dllFuncs.GL_SetRenderMode( CL_FontRenderMode( font->rendermode ));
}

qboolean Con_LoadFixedWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags )
{
	int font_width, i;

	if( !rendermode )
		return false;

	if( font->valid )
		return true; // already loaded

	font->hFontTexture = CL_LoadFontTexture( fontname, texFlags, &font_width );
	if( !font->hFontTexture )
		return false;

	font->type = FONT_FIXED;
	font->valid = true;
	font->scale = scale;
	font->rendermode = rendermode;
	font->charHeight = Q_rint( font_width / 16 * scale );

	for( i = 0; i < ARRAYSIZE( font->fontRc ); i++ )
	{
		font->fontRc[i].left   = ( i * font_width / 16 ) % font_width;
		font->fontRc[i].right  = font->fontRc[i].left + font_width / 16;
		font->fontRc[i].top    = ( i / 16 ) * ( font_width / 16 );
		font->fontRc[i].bottom = font->fontRc[i].top + font_width / 16;

		font->charWidths[i] = Q_rint( font_width / 16 * scale );
	}

	return true;
}

qboolean Con_LoadVariableWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags )
{
	fs_offset_t length;
	qfont_t src;
	byte *pfile;
	int font_width, i;

	if( !rendermode )
		return false;

	if( font->valid )
		return true;

	pfile = g_fsapi.LoadFile( fontname, &length, false );
	if( !pfile )
		return false;

	if( length < sizeof( src ))
	{
		Mem_Free( pfile );
		return false;
	}

	memcpy( &src, pfile, sizeof( src ));
	Mem_Free( pfile );

	font->hFontTexture = CL_LoadFontTexture( fontname, texFlags, &font_width );
	if( !font->hFontTexture )
		return false;

	font->type = FONT_VARIABLE;
	font->valid = true;
	font->scale = scale ? scale : 1.0f;
	font->rendermode = rendermode;
	font->charHeight = Q_rint( src.rowheight * scale );

	for( i = 0; i < ARRAYSIZE( font->fontRc ); i++ )
	{
		const charinfo *ci = &src.fontinfo[i];

		font->fontRc[i].left   = (word)ci->startoffset % font_width;
		font->fontRc[i].right  = font->fontRc[i].left + ci->charwidth;
		font->fontRc[i].top    = (word)ci->startoffset / font_width;
		font->fontRc[i].bottom = font->fontRc[i].top + src.rowheight;

		font->charWidths[i] = Q_rint( src.fontinfo[i].charwidth * scale );
	}

	return true;
}

void CL_FreeFont( cl_font_t *font )
{
	if( !font || !font->valid )
		return;

	ref.dllFuncs.GL_FreeTexture( font->hFontTexture );
	memset( font, 0, sizeof( *font ));
}

static int CL_CalcTabStop( const cl_font_t *font, int x )
{
	int space = font->charWidths[' '];
	int tab   = space * 6; // 6 spaces
	int stop  = tab - x % tab;

	if( stop < space )
		return tab * 2 - x % tab; // select next

	return stop;
}

static int CL_DrawBitmapCharacter( float x, float y, uint32_t uc, const rgba_t color, const cl_font_t *font, int flags )
{
	wrect_t *rc;
	float w, h;
	float s1, t1, s2, t2, half = 0.5f;
	int texw, texh;
	uint32_t number;

	// bitmap fonts need conversion. Do this and validate the result
	if( g_codepage == 1251 )
		number = Q_UnicodeToCP1251( uc );
	else if( g_codepage == 1252 )
		number = Q_UnicodeToCP1252( uc );
	else
		number = uc;

	if( !number || number >= ARRAYSIZE( font->charWidths ))
		return 0;

	// start rendering
	if( !font->charWidths[number] )
		return 0;

	R_GetTextureParms( &texw, &texh, font->hFontTexture );
	if( !texw || !texh )
		return font->charWidths[number];

	rc = &font->fontRc[number];

	if( font->scale <= 1.f || !REF_GET_PARM( PARM_TEX_FILTERING, font->hFontTexture ))
		half = 0;

	s1 = ((float)rc->left + half ) / texw;
	t1 = ((float)rc->top + half ) / texh;
	s2 = ((float)rc->right - half ) / texw;
	t2 = ((float)rc->bottom - half ) / texh;
	w = ( rc->right - rc->left ) * font->scale;
	h = ( rc->bottom - rc->top ) * font->scale;

	if( FBitSet( flags, FONT_DRAW_HUD ))
		SPR_AdjustSize( &x, &y, &w, &h );

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	// don't apply color to fixed fonts it's already colored
	if( font->type != FONT_FIXED || REF_GET_PARM( PARM_TEX_GLFORMAT, font->hFontTexture ) == 0x8045 ) // GL_LUMINANCE8_ALPHA8
		ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
	else ref.dllFuncs.Color4ub( 255, 255, 255, color[3] );
	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, font->hFontTexture );

	return font->charWidths[number];
}

static int CL_DrawTrueTypeCharacter( float x, float y, uint32_t uc, const rgba_t color, cl_font_t *font, int flags )
{
	// stub
	return 0;
}

int CL_DrawCharacter( float x, float y, uint32_t uc, const rgba_t color, const cl_font_t *font, int flags )
{
	if( !font || !font->valid || y < -font->charHeight )
		return 0;

	// check if printable
	if( uc <= 32 )
	{
		if( uc == ' ' )
			return font->charWidths[' '];
		else if( uc == '\t' )
			return CL_CalcTabStop( font, x );
		return 0;
	}

	if( font->type == FONT_TRUETYPE )
		return CL_DrawTrueTypeCharacter( x, y, uc, color, font, flags );

	return CL_DrawBitmapCharacter( x, y, uc, color, font, flags );
}

int CL_DrawString( float x, float y, const char *s, const rgba_t color, const cl_font_t *font, int flags )
{
	rgba_t current_color;
	int draw_len = 0;
	utfstate_t utfstate = { 0 };

	if( !font || !font->valid )
		return 0;

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	Vector4Copy( color, current_color );

	for( ; *s; s++ )
	{
		uint32_t uc = (byte)*s;

		if( FBitSet( flags, FONT_DRAW_UTF8 ))
			uc = Q_DecodeUTF8( &utfstate, uc );

		if( !uc )
			continue;

		if( uc == '\n' )
		{
			if( !s[1] ) // ignore newline at end of string if next
				break;

			// some client functions ignore newlines
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				draw_len = 0;
				y += font->charHeight;
			}

			if( FBitSet( flags, FONT_DRAW_RESETCOLORONLF ))
				Vector4Copy( color, current_color );

			continue;
		}

		// check for colorstrings
		if( uc == '^' && isdigit( s[1] ))
		{
			if( !FBitSet( flags, FONT_DRAW_FORCECOL ))
				VectorCopy( g_color_table[ColorIndex( s[1] )], current_color );

			s++;
			continue;
		}

		// skip setting rendermode, it was changed for this string already
		draw_len += CL_DrawCharacter( x + draw_len, y, uc, current_color, font, flags | FONT_DRAW_NORENDERMODE );
	}

	return draw_len;
}

int CL_DrawStringf( const cl_font_t *font, float x, float y, const rgba_t color, int flags, const char *fmt, ... )
{
	va_list va;
	char buf[MAX_VA_STRING];

	va_start( va, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	return CL_DrawString( x, y, buf, color, font, flags );
}

static int CL_DrawBitmapCharacterLen( const cl_font_t *font, uint32_t uc )
{
	int number;

	if( g_codepage == 1251 )
		number = Q_UnicodeToCP1251( uc );
	else if( g_codepage == 1252 )
		number = Q_UnicodeToCP1252( uc );
	else
		number = uc;

	if( !number || number >= ARRAYSIZE( font->charWidths ))
		return 0;

	return font->charWidths[number];
}

static int CL_DrawTrueTypeCharacterLen( const cl_font_t *font, uint32_t uc )
{
	// stub
	return 0;
}

void CL_DrawCharacterLen( const cl_font_t *font, uint32_t uc, int *width, int *height )
{
	if( !font || !font->valid )
		return;

	if( height )
		*height = font->charHeight;

	if( width )
	{
		// basically matches logic of CL_DrawCharacter
		if( uc <= 32 )
		{
			if( uc == ' ' )
				*width = font->charWidths[' '];
			else if( uc == '\t' )
				*width = CL_CalcTabStop( font, 0 ); // at least return max tabstop
			else
				*width = 0;
		}
		else if( font->type == FONT_TRUETYPE )
		{
			*width = CL_DrawTrueTypeCharacterLen( font, uc );
		}
		else
		{
			*width = CL_DrawBitmapCharacterLen( font, uc );
		}
	}
}

void CL_DrawStringLen( const cl_font_t *font, const char *s, int *width, int *height, int flags )
{
	utfstate_t utfstate = { 0 };
	int draw_len = 0;

	if( !font || !font->valid )
		return;

	if( height )
		*height = font->charHeight;

	if( width )
		*width = 0;

	if( !COM_CheckString( s ))
		return;

	for( ; *s; s++ )
	{
		uint32_t uc = (byte)*s;
		int char_width = 0;

		if( FBitSet( flags, FONT_DRAW_UTF8 ))
			uc = Q_DecodeUTF8( &utfstate, uc );

		if( !uc )
			continue;

		if( uc == '\n' )
		{
			// BUG: no check for end string here
			// but high chances somebody's relying on this
			draw_len = 0;

			// some client functions ignore newlines
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				if( height )
					*height += font->charHeight;
			}
		}

		// check for colorstrings
		if( uc == '^' && isdigit( s[1] ))
		{
			s++;
			continue;
		}

		CL_DrawCharacterLen( font, uc, &char_width, NULL );
		draw_len += char_width;

		if( width )
		{
			if( *width < draw_len )
				*width = draw_len;
		}
	}
}
