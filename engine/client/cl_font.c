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
#include "freetype2/include/ft2build.h"
#include FT_FREETYPE_H
#include FT_GLYPH_H

FT_Library ft;
FT_Face face;

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
	// load freetype font

	if (!ft && FT_Init_FreeType(&ft)) 
	{
		Con_DPrintf( S_ERROR "failed to init freetype\n" );
    	return false;
	}
	else
	if (!face && FT_New_Face(ft, "D:\\Program Files\\Kodi\\media\\Fonts\\arial.ttf", 0, &face)) 
	{
		Con_DPrintf( S_ERROR "failed to new font face \n" );
    	return false;
	}

	FT_Set_Pixel_Sizes(face, 16, 16);
	FT_Select_Charmap(face, FT_ENCODING_UNICODE);

	//load bitmap font texture

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

int CL_DrawCharacter( float x, float y, int number, rgba_t color, cl_font_t *font, int flags )
{
	int tex = 0;
	char texname[20];
	int width = 0;
	int height = 0;
	
	// check if printable
	if( number <= 32 )
	{
		if( number == ' ' )
			return font->charWidths[' '];
		else if( number == '\t' )
			return CL_CalcTabStop( font, x );
		return 0;
	}
	
	FT_Set_Pixel_Sizes(face, font->charHeight, font->charHeight);

    // used FT_LOAD_RENDER
	if (FT_Load_Char(face, number, FT_LOAD_RENDER))
	{
		return 0;
	}

	// may be dont need this
	// FT_Glyph sGlyph;
	// if (FT_Get_Glyph(face->glyph, &sGlyph)) return 0;
	// FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
	// FT_Glyph_To_Bitmap(&sGlyph, ft_render_mode_normal, 0, 1);
	// FT_BitmapGlyph sBitmapGlyph = (FT_BitmapGlyph)sGlyph;

	Q_snprintf( texname, sizeof( texname ), "font_%i", number);

	if( !( tex = ref.dllFuncs.GL_FindTexture( texname ) ) )
	{
		
		FT_Bitmap bitmap = face->glyph->bitmap;

		if (!bitmap.width || !bitmap.rows)
		{
			return 0;
		}

		byte *rgbData = malloc(bitmap.rows * bitmap.pitch * 4);
		for (int i = 0; i < bitmap.rows; i++)
		{
			for (int j = 0; j < bitmap.width; j++)
			{
				unsigned int index = i * bitmap.pitch + j;
				unsigned char pixel_value = bitmap.buffer[index];
				int rgbIndex = (i * bitmap.width + j) * 4;
				rgbData[rgbIndex + 0] = 255;
				rgbData[rgbIndex + 1] = 255;
				rgbData[rgbIndex + 2] = 255;
				rgbData[rgbIndex + 3] = pixel_value;
			}
		}
		
		tex = ref.dllFuncs.GL_CreateTexture( texname, bitmap.width, bitmap.rows, rgbData, TF_IMAGE|TF_HAS_ALPHA );

		free(rgbData);

		if( !tex )
		{
			Con_DPrintf( S_ERROR "cannot load char tex %s\n" , texname);
			return 0;
		}
	}

	R_GetTextureParms(&width, &height, tex);

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	if( font->type != FONT_FIXED || REF_GET_PARM( PARM_TEX_GLFORMAT, font->hFontTexture ) == 0x8045 ) // GL_LUMINANCE8_ALPHA8
		ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
	else ref.dllFuncs.Color4ub( 255, 255, 255, color[3] );

	ref.dllFuncs.R_DrawStretchPic( x + face->glyph->bitmap_left, y - face->glyph->bitmap_top, width, height, 0.0f, 0.0f, 1.0f, 1.0f, tex);

	return face->glyph->bitmap_left + width;
	
	// wrect_t *rc;
	// float w, h;
	// float s1, t1, s2, t2, half = 0.5f;
	// int texw, texh;

	// if( !font || !font->valid || y < -font->charHeight )
	// 	return 0;

	// // check if printable
	// if( number <= 32 )
	// {
	// 	if( number == ' ' )
	// 		return font->charWidths[' '];
	// 	else if( number == '\t' )
	// 		return CL_CalcTabStop( font, x );
	// 	return 0;
	// }

	// if( FBitSet( flags, FONT_DRAW_UTF8 ))
	// 	number = Con_UtfProcessChar( number & 255 );
	// else number &= 255;

	// if( !number || !font->charWidths[number])
	// 	return 0;

	// R_GetTextureParms( &texw, &texh, font->hFontTexture );
	// if( !texw || !texh )
	// 	return font->charWidths[number];

	// rc = &font->fontRc[number];

	// if( font->scale <= 1.f || !REF_GET_PARM( PARM_TEX_FILTERING, font->hFontTexture ))
	// 	half = 0;

	// s1 = ((float)rc->left + half ) / texw;
	// t1 = ((float)rc->top + half ) / texh;
	// s2 = ((float)rc->right - half ) / texw;
	// t2 = ((float)rc->bottom - half ) / texh;
	// w = ( rc->right - rc->left ) * font->scale;
	// h = ( rc->bottom - rc->top ) * font->scale;

	// if( FBitSet( flags, FONT_DRAW_HUD ))
	// 	SPR_AdjustSize( &x, &y, &w, &h );

	// if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
	// 	CL_SetFontRendermode( font );

	// // don't apply color to fixed fonts it's already colored
	// if( font->type != FONT_FIXED || REF_GET_PARM( PARM_TEX_GLFORMAT, font->hFontTexture ) == 0x8045 ) // GL_LUMINANCE8_ALPHA8
	// 	ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
	// else ref.dllFuncs.Color4ub( 255, 255, 255, color[3] );
	// ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, font->hFontTexture );

	// return font->charWidths[number];
}

int CL_DrawString( float x, float y, const char *s, rgba_t color, cl_font_t *font, int flags )
{
	rgba_t current_color;
	int draw_len = 0;

	if( !font || !font->valid )
		return 0;

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // clear utf state

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	Vector4Copy( color, current_color );
	

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
				y += font->charHeight;
			}

			if( FBitSet( flags, FONT_DRAW_RESETCOLORONLF ))
				 Vector4Copy( color, current_color );
			continue;
		}

		if( IsColorString( s ))
		{
			// don't copy alpha
			if( !FBitSet( flags, FONT_DRAW_FORCECOL ))
				VectorCopy( g_color_table[ColorIndex(*( s + 1 ))], current_color );

			s += 2;
			continue;
		}       

		int ch = Con_UtfProcessChar( (byte)*s );
		
		// skip setting rendermode, it was changed for this string already
		if( ch )
			draw_len += CL_DrawCharacter( x + draw_len, y, ch, current_color, font, flags | FONT_DRAW_NORENDERMODE );
		
		s++;
	}

	return draw_len;
}

int CL_DrawStringf( cl_font_t *font, float x, float y, rgba_t color, int flags, const char *fmt, ... )
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
	if( width )
	{
		if( number == '\t' )
			*width = CL_CalcTabStop( font, 0 ); // at least return max tabstop
		else 
		{
			FT_Set_Pixel_Sizes(face, font->charHeight, font->charHeight);
			
			if (FT_Load_Char(face, number, FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_NORMAL))
				*width = font->charWidths[number & 255];
			else
				*width = face->glyph->bitmap_left + face->glyph->bitmap.width;
		}
	}
	if( height ) *height = font->charHeight;
}

void CL_DrawStringLen( cl_font_t *font, const char *s, int *width, int *height, int flags )
{
	int draw_len = 0;

	if( !font || !font->valid )
		return;

	if( height )
		*height = font->charHeight;

	if( width )
		*width = 0;

	if( !COM_CheckString( s ))
		return;

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // reset utf state

	//UTF8 Support
	FT_Set_Pixel_Sizes(face, font->charHeight, font->charHeight);

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
					*height += font->charHeight;
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
			draw_len += font->charWidths[number];

			if (FT_Load_Char(face, number, FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_NORMAL))
				draw_len += font->charWidths[number];
			else
				draw_len += face->glyph->bitmap_left + face->glyph->bitmap.width;
			if( width )
			{
				if( draw_len > *width )
					*width = draw_len;
			}
		}

		s++;
	}
}
