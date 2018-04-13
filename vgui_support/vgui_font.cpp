/*
vgui_font.cpp - fonts management
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

In addition, as a special exception, the author gives permission
to link the code of this program with VGUI library developed by
Valve, L.L.C ("Valve"). You must obey the GNU General Public License
in all respects for all of the code used other than VGUI library.
If you modify this file, you may extend this exception to your
version of the file, but you are not obligated to do so. If
you do not wish to do so, delete this exception statement
from your version.

*/

#include "vgui_main.h"

int FontCache::s_pFontPageSize[FONT_PAGE_SIZE_COUNT] = { 16, 32, 64, 128 };

FontCache::FontCache() : m_CharCache( 0, 256, CacheEntryLessFunc )
{
	CacheEntry_t listHead = { 0, 0 };

	m_LRUListHeadIndex = m_CharCache.Insert( listHead );
	m_CharCache[m_LRUListHeadIndex].nextEntry = m_LRUListHeadIndex;
	m_CharCache[m_LRUListHeadIndex].prevEntry = m_LRUListHeadIndex;
	for( int i = 0; i < FONT_PAGE_SIZE_COUNT; i++ )
	{
		m_pCurrPage[i] = -1;
	}
}

bool FontCache::CacheEntryLessFunc( CacheEntry_t const &lhs, CacheEntry_t const &rhs )
{
	if( lhs.font < rhs.font )
		return true;
	else if( lhs.font > rhs.font )
		return false;

	return ( lhs.ch < rhs.ch );
}

bool FontCache::GetTextureForChar( Font *font, char ch, int *textureID, float **texCoords )
{
	static CacheEntry_t cacheitem;

	cacheitem.font = font;
	cacheitem.ch = ch;

	Assert( texCoords != NULL );

	*texCoords = cacheitem.texCoords;

	HCacheEntry cacheHandle = m_CharCache.Find( cacheitem );

	if( cacheHandle != 65535 && m_CharCache.IsValidIndex( cacheHandle ))
	{
		// we have an entry already, return that
		int page = m_CharCache[cacheHandle].page;
		*textureID = m_PageList[page].textureID;
		//else return false;
		*texCoords = m_CharCache[cacheHandle].texCoords;
		return true;
	}

	// get the char details
	int fontTall = font->getTall();
	int a, b, c;
	font->getCharABCwide( (byte)ch, a, b, c );
	int fontWide = b;

	// get a texture to render into
	int page, drawX, drawY, twide, ttall;
	if( !AllocatePageForChar( fontWide, fontTall, page, drawX, drawY, twide, ttall ))
		return false;

	// create a buffer and render the character into it
	int nByteCount = s_pFontPageSize[FONT_PAGE_SIZE_COUNT-1] * s_pFontPageSize[FONT_PAGE_SIZE_COUNT-1] * 4;
	byte * rgba = (byte *)g_api->EngineMalloc(nByteCount);//(byte *)Z_Malloc( nByteCount );
	font->getCharRGBA( (byte)ch, 0, 0, fontWide, fontTall, rgba );

	// upload the new sub texture 
	g_api->BindTexture( m_PageList[page].textureID );
	g_api->UploadTextureBlock( m_PageList[page].textureID, drawX, drawY, rgba, fontWide, fontTall );

	// set the cache info
	cacheitem.page = page;

	cacheitem.texCoords[0] = (float)((double)drawX / ((double)twide));
	cacheitem.texCoords[1] = (float)((double)drawY / ((double)ttall));
	cacheitem.texCoords[2] = (float)((double)(drawX + fontWide) / (double)twide);
	cacheitem.texCoords[3] = (float)((double)(drawY + fontTall) / (double)ttall);

	m_CharCache.Insert( cacheitem );

	// return the data
	*textureID = m_PageList[page].textureID;
	// memcpy( texCoords, cacheitem.texCoords, sizeof( float ) * 4 );
	return true;
}

int FontCache::ComputePageType( int charTall ) const
{
	for( int i = 0; i < FONT_PAGE_SIZE_COUNT; i++ )
	{
		if( charTall < s_pFontPageSize[i] )
			return i;
	}
	return -1;
}

bool FontCache::AllocatePageForChar( int charWide, int charTall, int &pageIndex, int &drawX, int &drawY, int &twide, int &ttall )
{
	// see if there is room in the last page for this character
	int nPageType = ComputePageType( charTall );

	if( nPageType < 0 )
		return false;

	pageIndex = m_pCurrPage[nPageType];
	
	int nNextX = 0;
	bool bNeedsNewPage = true;

	if( pageIndex > -1 )
	{
		Page_t &page = m_PageList[pageIndex];

		nNextX = page.nextX + charWide;

		// make sure we have room on the current line of the texture page
		if( nNextX > page.wide )
		{
			// move down a line
			page.nextX = 0;
			nNextX = charWide;
			page.nextY += page.fontHeight + 1;
		}

		bNeedsNewPage = (( page.nextY + page.fontHeight + 1 ) > page.tall );
	}
	
	if( bNeedsNewPage )
	{
		// allocate a new page
		pageIndex = m_PageList.AddToTail();
		Page_t &newPage = m_PageList[pageIndex];
		m_pCurrPage[nPageType] = pageIndex;

		newPage.textureID = g_api->GenerateTexture();

		newPage.fontHeight = s_pFontPageSize[nPageType];
		newPage.wide = 256;
		newPage.tall = 256;
		newPage.nextX = 0;
		newPage.nextY = 0;

		nNextX = charWide;

		// create empty texture
		g_api->CreateTexture( newPage.textureID, 256, 256 );
	}

	// output the position
	Page_t &page = m_PageList[pageIndex];
	drawX = page.nextX;
	drawY = page.nextY;
	twide = page.wide;
	ttall = page.tall;

	// update the next position to draw in
	page.nextX = nNextX + 1;

	return true;
}
