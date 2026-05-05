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

#include "common.h"
#include "filesystem.h"
#include "client.h"
#include "qfont.h"
#include "swaplib.h"

le_struct_begin( charinfo_swap )
	le_struct_field( charinfo, startoffset )
	le_struct_field( charinfo, charwidth )
le_struct_end();

le_struct_begin( qfont_swap )
	le_struct_field( qfont_t, width )
	le_struct_field( qfont_t, height )
	le_struct_field( qfont_t, rowcount )
	le_struct_field( qfont_t, rowheight )
	le_struct_array_child( qfont_t, fontinfo, charinfo_swap, NUM_GLYPHS )
le_struct_end();

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

void CL_SetFontColor( cl_font_t *font, const rgba_t color )
{
	ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
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
	le_struct_swap( qfont_swap, &src );
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

	if( font->ttfont )
	{
		TTF_Destroy( font->ttfont );
		font->ttfont = NULL;
	}

	ref.dllFuncs.GL_FreeTexture( font->hFontTexture );
	memset( font, 0, sizeof( *font ));
}

static int CL_CalcTabStop( const cl_font_t *font, int x )
{
	int space = font->charWidths['0'];
	int tab   = space * 6; // 6 spaces
	int stop  = tab - x % tab;

	if( stop < space )
		return tab * 2 - x % tab; // select next

	return stop;
}

int CL_DrawCharacter( float x, float y, int number, const rgba_t color, cl_font_t *font, int flags )
{
	wrect_t *rc;
	float w, h;
	float s1, t1, s2, t2, half = 0.5f;
	int texw, texh;

	if( !font || !font->valid || y < -font->charHeight )
		return 0;

	// truetype dispatch
	if( font->ttfont )
	{
		int r = 255, g = 255, b = 255, a = 255;
		if( color ) { r = color[0]; g = color[1]; b = color[2]; a = color[3]; }

		if( number <= 32 )
		{
			if( number == ' ' )
				return FBitSet( flags, FONT_DRAW_HUD ) ? (int)( TTF_GetCharWidth( font->ttfont, ' ' ) * clgame.scrInfo.iWidth / refState.width + 0.5f ) : TTF_GetCharWidth( font->ttfont, ' ' );
			else if( number == '\t' )
				return CL_CalcTabStop( font, x );
			return 0;
		}

		if( FBitSet( flags, FONT_DRAW_HUD ))
		{
			float fx = x, fy = y, fw = (float)TTF_GetCharWidth( font->ttfont, number ), fh = (float)TTF_GetHeight( font->ttfont );
			SPR_AdjustSize( &fx, &fy, &fw, &fh );
			TTF_DrawChar( font->ttfont, (int)fx, (int)fy, number, r, g, b, a );
			return (int)( TTF_GetCharWidth( font->ttfont, number ) * clgame.scrInfo.iWidth / refState.width + 0.5f );
		}
		return TTF_DrawChar( font->ttfont, (int)x, (int)y, number, r, g, b, a );
	}

	// check if printable
	if( number <= 32 )
	{
		if( number == ' ' )
			return font->charWidths[' '];
		else if( number == '\t' )
			return CL_CalcTabStop( font, x );
		return 0;
	}

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		number = Con_UtfProcessChar( number & 255 );
	else number &= 255;

	if( !number || !font->charWidths[number])
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

	if( !FBitSet( flags, FONT_DRAW_NOCOLOR ))
		CL_SetFontColor( font, color );

	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, font->hFontTexture );

	return font->charWidths[number];
}

int CL_DrawString( float x, float y, const char *s, const rgba_t color, cl_font_t *font, int flags )
{
	int draw_len = 0;
	rgba_t curColor;

	if( !font || !font->valid )
		return 0;

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // clear utf state

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	if( color )
		memcpy( curColor, color, sizeof( curColor ));
	else
		MakeRGBA( curColor, 255, 255, 255, 255 );

	CL_SetFontColor( font, curColor );

	SetBits( flags, FONT_DRAW_NOCOLOR | FONT_DRAW_NORENDERMODE );

	while( *s )
	{
		if( *s == '\n' )
		{
			s++;

			if( !*s )
				break;

			// some client functions ignore newlines
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				draw_len = 0;
				if( font->ttfont )
					y += FBitSet( flags, FONT_DRAW_HUD ) ? (int)( TTF_GetHeight( font->ttfont ) * clgame.scrInfo.iHeight / refState.height + 0.5f ) : TTF_GetHeight( font->ttfont );
				else
					y += font->charHeight;
			}

			if( FBitSet( flags, FONT_DRAW_RESETCOLORONLF ))
			{
				if( color )
					memcpy( curColor, color, sizeof( curColor ));
				else
					MakeRGBA( curColor, 255, 255, 255, 255 );
				CL_SetFontColor( font, curColor );
			}
			continue;
		}

		if( IsColorString( s ))
		{
			// don't copy alpha
			if( !FBitSet( flags, FONT_DRAW_FORCECOL ))
			{
				const byte *ct = g_color_table[ColorIndex(*( s + 1 ))];
				curColor[0] = ct[0];
				curColor[1] = ct[1];
				curColor[2] = ct[2];
				// keep alpha from original color
				CL_SetFontColor( font, curColor );
			}

			s += 2;
			continue;
		}

		// for truetype, pass curColor explicitly since GL state isn't used
		draw_len += CL_DrawCharacter( x + draw_len, y, (byte)*s, font->ttfont ? curColor : NULL, font, flags );

		s++;
	}

	return draw_len;
}

int CL_DrawStringf( cl_font_t *font, float x, float y, const rgba_t color, int flags, const char *fmt, ... )
{
	va_list va;
	char buf[MAX_VA_STRING];

	va_start( va, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	return CL_DrawString( x, y, buf, color, font, flags );
}

void CL_DrawCharacterLen( cl_font_t *font, int number, int *width, int *height )
{
	if( !font || !font->valid ) return;

	if( font->ttfont )
	{
		if( width )
		{
			if( number == '\t' )
				*width = CL_CalcTabStop( font, 0 );
			else
				*width = TTF_GetCharWidth( font->ttfont, number );
		}
		if( height ) *height = TTF_GetHeight( font->ttfont );
		return;
	}

	if( width )
	{
		if( number == '\t' )
			*width = CL_CalcTabStop( font, 0 ); // at least return max tabstop
		else *width = font->charWidths[number & 255];
	}
	if( height ) *height = font->charHeight;
}

void CL_DrawStringLen( cl_font_t *font, const char *s, int *width, int *height, int flags )
{
	int draw_len = 0;

	if( !font || !font->valid )
		return;

	if( height )
	{
		if( font->ttfont )
			*height = FBitSet( flags, FONT_DRAW_HUD ) ? (int)( TTF_GetHeight( font->ttfont ) * clgame.scrInfo.iHeight / refState.height + 0.5f ) : TTF_GetHeight( font->ttfont );
		else
			*height = font->charHeight;
	}

	if( width )
		*width = 0;

	if( COM_StringEmptyOrNULL( s ))
		return;

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // reset utf state

	while( *s )
	{
		int number;

		if( *s == '\n' )
		{
			// BUG: no check for end string here
			// but high chances somebody's relying on this
			s++;
			draw_len = 0;
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				if( height )
				{
					if( font->ttfont )
						*height += FBitSet( flags, FONT_DRAW_HUD ) ? (int)( TTF_GetHeight( font->ttfont ) * clgame.scrInfo.iHeight / refState.height + 0.5f ) : TTF_GetHeight( font->ttfont );
					else
						*height += font->charHeight;
				}
			}
			continue;
		}
		else if( *s == '\t' )
		{
			draw_len += CL_CalcTabStop( font, 0 ); // at least return max tabstop
			s++;
			continue;
		}

		if( IsColorString( s ))
		{
			s += 2;
			continue;
		}

		if( FBitSet( flags, FONT_DRAW_UTF8 ))
			number = Con_UtfProcessChar( (byte)*s );
		else number = (byte)*s;

		if( number )
		{
			if( font->ttfont )
				draw_len += FBitSet( flags, FONT_DRAW_HUD ) ? (int)( TTF_GetCharWidth( font->ttfont, number ) * clgame.scrInfo.iWidth / refState.width + 0.5f ) : TTF_GetCharWidth( font->ttfont, number );
			else
				draw_len += font->charWidths[number];

			if( width )
			{
				if( draw_len > *width )
					*width = draw_len;
			}
		}

		s++;
	}
}
