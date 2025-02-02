/**
 * cl_font.h
 * 
 * This file is part of the Xash3D FWGS project.
 * 
 * Xash3D FWGS is a fork of the Xash3D Engine by Unkle Mike.
 * 
 * This header file contains the declarations for font handling in the client.
 * 
 * Author: [Your Name]
 * Date: [Current Date]
 */

#ifndef CL_FONT_H
#define CL_FONT_H

#include "stb_truetype.h"

#define FONTS_MAX_BUFFER	1000
#define FONT_PAGE_MAX	8

typedef struct {
	int m_iTexture;
	int m_iWidth,m_iHeight;
	int m_iXOff,m_iYOff;
	byte *pTexture;
	int m_iCharWidth;
	int Char;
} CHARINFO;

typedef struct {
	byte *m_pFontData;
	stbtt_fontinfo m_fontInfo;
	
	double scale;
	
	int m_iAscent, m_iMaxCharWidth;
    int m_iCharCount;
    int m_iWidth, m_iHeight;
    int m_iBuffer[FONTS_MAX_BUFFER];
    CHARINFO m_tFontTexture[FONTS_MAX_BUFFER];
} Font;

int Font_Init(Font* self, char* name, int tall);
int Font_CheckCharExists(Font* self, int ch);
int Font_LoadChar(Font* self, int ch);
CHARINFO* Font_GetChar(Font* self, int ch);
int Font_DrawChar(cl_font_t *font, rgba_t color, int x, int y, int number, int flags);
void Font_SetWidth(Font* self, int iWidth);
#endif // CL_FONT_H