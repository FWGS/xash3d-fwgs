/*
cl_game.c - client dll interaction
Copyright (C) 2008 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if XASH_SDL == 2
#include <SDL.h> // SDL_GetWindowPosition
#elif XASH_SDL == 3
#include <SDL3/SDL.h> // SDL_GetWindowPosition
#endif // XASH_SDL

#include "common.h"
#include "client.h"
#include "const.h"
#include "triangleapi.h"
#include "r_efx.h"
#include "demo_api.h"
#include "ivoicetweak.h"
#include "pm_local.h"
#include "cl_tent.h"
#include "input.h"
#include "shake.h"
#include "sprite.h"
#include "library.h"
#include "vgui_draw.h"
#include "sound.h"		// SND_STOP_LOOPING
#include "platform/platform.h"

#define MAX_LINELENGTH	80
#define MAX_TEXTCHANNELS	8		// must be power of two (GoldSrc uses 4 channels)
#define TEXT_MSGNAME	"TextMessage%i"

static char cl_textbuffer[MAX_TEXTCHANNELS][2048];
static client_textmessage_t cl_textmessage[MAX_TEXTCHANNELS];

static const dllfunc_t cdll_exports[] =
{
{ "Initialize", (void **)&clgame.dllFuncs.pfnInitialize },
{ "HUD_VidInit", (void **)&clgame.dllFuncs.pfnVidInit },
{ "HUD_Init", (void **)&clgame.dllFuncs.pfnInit },
{ "HUD_Shutdown", (void **)&clgame.dllFuncs.pfnShutdown },
{ "HUD_Redraw", (void **)&clgame.dllFuncs.pfnRedraw },
{ "HUD_UpdateClientData", (void **)&clgame.dllFuncs.pfnUpdateClientData },
{ "HUD_Reset", (void **)&clgame.dllFuncs.pfnReset },
{ "HUD_PlayerMove", (void **)&clgame.dllFuncs.pfnPlayerMove },
{ "HUD_PlayerMoveInit", (void **)&clgame.dllFuncs.pfnPlayerMoveInit },
{ "HUD_PlayerMoveTexture", (void **)&clgame.dllFuncs.pfnPlayerMoveTexture },
{ "HUD_ConnectionlessPacket", (void **)&clgame.dllFuncs.pfnConnectionlessPacket },
{ "HUD_GetHullBounds", (void **)&clgame.dllFuncs.pfnGetHullBounds },
{ "HUD_Frame", (void **)&clgame.dllFuncs.pfnFrame },
{ "HUD_PostRunCmd", (void **)&clgame.dllFuncs.pfnPostRunCmd },
{ "HUD_Key_Event", (void **)&clgame.dllFuncs.pfnKey_Event },
{ "HUD_AddEntity", (void **)&clgame.dllFuncs.pfnAddEntity },
{ "HUD_CreateEntities", (void **)&clgame.dllFuncs.pfnCreateEntities },
{ "HUD_StudioEvent", (void **)&clgame.dllFuncs.pfnStudioEvent },
{ "HUD_TxferLocalOverrides", (void **)&clgame.dllFuncs.pfnTxferLocalOverrides },
{ "HUD_ProcessPlayerState", (void **)&clgame.dllFuncs.pfnProcessPlayerState },
{ "HUD_TxferPredictionData", (void **)&clgame.dllFuncs.pfnTxferPredictionData },
{ "HUD_TempEntUpdate", (void **)&clgame.dllFuncs.pfnTempEntUpdate },
{ "HUD_DrawNormalTriangles", (void **)&clgame.dllFuncs.pfnDrawNormalTriangles },
{ "HUD_DrawTransparentTriangles", (void **)&clgame.dllFuncs.pfnDrawTransparentTriangles },
{ "HUD_GetUserEntity", (void **)&clgame.dllFuncs.pfnGetUserEntity },
{ "Demo_ReadBuffer", (void **)&clgame.dllFuncs.pfnDemo_ReadBuffer },
{ "CAM_Think", (void **)&clgame.dllFuncs.CAM_Think },
{ "CL_IsThirdPerson", (void **)&clgame.dllFuncs.CL_IsThirdPerson },
{ "CL_CameraOffset", (void **)&clgame.dllFuncs.CL_CameraOffset },	// unused callback. Now camera code is completely moved to the user area
{ "CL_CreateMove", (void **)&clgame.dllFuncs.CL_CreateMove },
{ "IN_ActivateMouse", (void **)&clgame.dllFuncs.IN_ActivateMouse },
{ "IN_DeactivateMouse", (void **)&clgame.dllFuncs.IN_DeactivateMouse },
{ "IN_MouseEvent", (void **)&clgame.dllFuncs.IN_MouseEvent },
{ "IN_Accumulate", (void **)&clgame.dllFuncs.IN_Accumulate },
{ "IN_ClearStates", (void **)&clgame.dllFuncs.IN_ClearStates },
{ "V_CalcRefdef", (void **)&clgame.dllFuncs.pfnCalcRefdef },
{ "KB_Find", (void **)&clgame.dllFuncs.KB_Find },
};

// optional exports
static const dllfunc_t cdll_new_exports[] = 	// allowed only in SDK 2.3 and higher
{
{ "HUD_GetStudioModelInterface", (void **)&clgame.dllFuncs.pfnGetStudioModelInterface },
{ "HUD_DirectorMessage", (void **)&clgame.dllFuncs.pfnDirectorMessage },
{ "HUD_VoiceStatus", (void **)&clgame.dllFuncs.pfnVoiceStatus },
{ "HUD_ChatInputPosition", (void **)&clgame.dllFuncs.pfnChatInputPosition },
{ "HUD_GetRenderInterface", (void **)&clgame.dllFuncs.pfnGetRenderInterface },	// Xash3D ext
{ "HUD_ClipMoveToEntity", (void **)&clgame.dllFuncs.pfnClipMoveToEntity },	// Xash3D ext
{ "IN_ClientTouchEvent", (void **)&clgame.dllFuncs.pfnTouchEvent}, // Xash3D FWGS ext
{ "IN_ClientMoveEvent", (void **)&clgame.dllFuncs.pfnMoveEvent}, // Xash3D FWGS ext
{ "IN_ClientLookEvent", (void **)&clgame.dllFuncs.pfnLookEvent}, // Xash3D FWGS ext
};

static void pfnSPR_DrawHoles( int frame, int x, int y, const wrect_t *prc );

/*
====================
CL_CreatePlaylist

Create a default valve playlist
====================
*/
static void CL_CreatePlaylist( const char *filename )
{
	file_t	*f;

	f = FS_Open( filename, "w", false );
	if( !f )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, filename );
		return;
	}

	// make standard cdaudio playlist
	FS_Print( f, "blank\n" );		// #1
	FS_Print( f, "Half-Life01.mp3\n" );	// #2
	FS_Print( f, "Prospero01.mp3\n" );	// #3
	FS_Print( f, "Half-Life12.mp3\n" );	// #4
	FS_Print( f, "Half-Life07.mp3\n" );	// #5
	FS_Print( f, "Half-Life10.mp3\n" );	// #6
	FS_Print( f, "Suspense01.mp3\n" );	// #7
	FS_Print( f, "Suspense03.mp3\n" );	// #8
	FS_Print( f, "Half-Life09.mp3\n" );	// #9
	FS_Print( f, "Half-Life02.mp3\n" );	// #10
	FS_Print( f, "Half-Life13.mp3\n" );	// #11
	FS_Print( f, "Half-Life04.mp3\n" );	// #12
	FS_Print( f, "Half-Life15.mp3\n" );	// #13
	FS_Print( f, "Half-Life14.mp3\n" );	// #14
	FS_Print( f, "Half-Life16.mp3\n" );	// #15
	FS_Print( f, "Suspense02.mp3\n" );	// #16
	FS_Print( f, "Half-Life03.mp3\n" );	// #17
	FS_Print( f, "Half-Life08.mp3\n" );	// #18
	FS_Print( f, "Prospero02.mp3\n" );	// #19
	FS_Print( f, "Half-Life05.mp3\n" );	// #20
	FS_Print( f, "Prospero04.mp3\n" );	// #21
	FS_Print( f, "Half-Life11.mp3\n" );	// #22
	FS_Print( f, "Half-Life06.mp3\n" );	// #23
	FS_Print( f, "Prospero03.mp3\n" );	// #24
	FS_Print( f, "Half-Life17.mp3\n" );	// #25
	FS_Print( f, "Prospero05.mp3\n" );	// #26
	FS_Print( f, "Suspense05.mp3\n" );	// #27
	FS_Print( f, "Suspense07.mp3\n" );	// #28
	FS_Close( f );
}

/*
====================
CL_InitCDAudio

Initialize CD playlist
====================
*/
static void CL_InitCDAudio( const char *filename )
{
	byte *afile;
	char *pfile;
	string	token;
	int	c = 0;

	if( !FS_FileExists( filename, false ))
	{
		// create a default playlist
		CL_CreatePlaylist( filename );
	}

	afile = FS_LoadFile( filename, NULL, false );
	if( !afile ) return;

	pfile = (char *)afile;

	// format: trackname\n [num]
	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( !Q_stricmp( token, "blank" ))
			clgame.cdtracks[c][0] = '\0';
		else
		{
			Q_snprintf( clgame.cdtracks[c], sizeof( clgame.cdtracks[c] ),
				"media/%s", token );
		}

		if( ++c > MAX_CDTRACKS - 1 )
		{
			Con_Reportf( S_WARN "%s: too many tracks %i in %s\n", __func__, MAX_CDTRACKS, filename );
			break;
		}
	}

	Mem_Free( afile );
}

/*
=============
CL_AdjustXPos

adjust text by x pos
=============
*/
static int CL_AdjustXPos( float x, int width, int totalWidth )
{
	int	xPos;

	if( x == -1 )
	{
		xPos = ( clgame.scrInfo.iWidth - width ) * 0.5f;
	}
	else
	{
		if ( x < 0 )
			xPos = (1.0f + x) * clgame.scrInfo.iWidth - totalWidth;	// Alight right
		else // align left
			xPos = x * clgame.scrInfo.iWidth;
	}

	if( xPos + width > clgame.scrInfo.iWidth )
		xPos = clgame.scrInfo.iWidth - width;
	else if( xPos < 0 )
		xPos = 0;

	return xPos;
}

/*
=============
CL_AdjustYPos

adjust text by y pos
=============
*/
static int CL_AdjustYPos( float y, int height )
{
	int	yPos;

	if( y == -1 ) // centered?
	{
		yPos = ( clgame.scrInfo.iHeight - height ) * 0.5f;
	}
	else
	{
		// Alight bottom?
		if( y < 0 )
			yPos = (1.0f + y) * clgame.scrInfo.iHeight - height; // Alight bottom
		else // align top
			yPos = y * clgame.scrInfo.iHeight;
	}

	if( yPos + height > clgame.scrInfo.iHeight )
		yPos = clgame.scrInfo.iHeight - height;
	else if( yPos < 0 )
		yPos = 0;

	return yPos;
}

/*
=============
CL_CenterPrint

print centerscreen message
=============
*/
void CL_CenterPrint( const char *text, float y )
{
	cl_font_t *font = Con_GetCurFont();

	if( COM_StringEmptyOrNULL( text ) || !font || !font->valid )
		return;

	clgame.centerPrint.totalWidth = 0;
	clgame.centerPrint.time = cl.mtime[0]; // allow pause for centerprint
	Q_strncpy( clgame.centerPrint.message, text, sizeof( clgame.centerPrint.message ));

	CL_DrawStringLen( font,
		clgame.centerPrint.message,
		&clgame.centerPrint.totalWidth,
		&clgame.centerPrint.totalHeight,
		FONT_DRAW_HUD | FONT_DRAW_UTF8 );

	if( font->charHeight )
		clgame.centerPrint.lines = clgame.centerPrint.totalHeight / font->charHeight;
	else clgame.centerPrint.lines = 1;

	clgame.centerPrint.y = CL_AdjustYPos( y, clgame.centerPrint.totalHeight );
}

/*
====================
SPR_AdjustSize

draw hudsprite routine
====================
*/
void SPR_AdjustSize( float *x, float *y, float *w, float *h )
{
	float	xscale, yscale;

	if( refState.width == clgame.scrInfo.iWidth && refState.height == clgame.scrInfo.iHeight )
		return;

	// scale for screen sizes
	xscale = refState.width / (float)clgame.scrInfo.iWidth;
	yscale = refState.height / (float)clgame.scrInfo.iHeight;

	*x *= xscale;
	*y *= yscale;
	*w *= xscale;
	*h *= yscale;
}

static void SPR_AdjustTexCoords( int texnum, float width, float height, float *s1, float *t1, float *s2, float *t2 )
{
	const qboolean filtering = REF_GET_PARM( PARM_TEX_FILTERING, texnum );
	const int xremainder = refState.width % clgame.scrInfo.iWidth;
	const int yremainder = refState.height % clgame.scrInfo.iHeight;

	if(( filtering || xremainder ) && refState.width != clgame.scrInfo.iWidth )
	{
		// align to texel if scaling
		*s1 += 0.5f;
		*s2 -= 0.5f;
	}

	if(( filtering || yremainder ) && refState.height != clgame.scrInfo.iHeight )
	{
		// align to texel if scaling
		*t1 += 0.5f;
		*t2 -= 0.5f;
	}

	*s1 /= width;
	*t1 /= height;
	*s2 /= width;
	*t2 /= height;
}

/*
====================
SPR_DrawGeneric

draw hudsprite routine
====================
*/
static void SPR_DrawGeneric( int frame, float x, float y, float width, float height, const wrect_t *prc )
{
	float	s1, s2, t1, t2;
	int	texnum;

	if( width == -1 && height == -1 )
	{
		int	w, h;

		// assume we get sizes from image
		ref.dllFuncs.R_GetSpriteParms( &w, &h, NULL, frame, clgame.ds.pSprite );

		width = w;
		height = h;
	}

	texnum = ref.dllFuncs.R_GetSpriteTexture( clgame.ds.pSprite, frame );

	if( prc )
	{
		wrect_t	rc = *prc;

		// Sigh! some stupid modmakers set wrong rectangles in hud.txt
		if( rc.left <= 0 || rc.left >= width ) rc.left = 0;
		if( rc.top <= 0 || rc.top >= height ) rc.top = 0;
		if( rc.right <= 0 || rc.right > width ) rc.right = width;
		if( rc.bottom <= 0 || rc.bottom > height ) rc.bottom = height;

		s1 = rc.left;
		t1 = rc.top;
		s2 = rc.right;
		t2 = rc.bottom;

		// calc user-defined rectangle
		SPR_AdjustTexCoords( texnum, width, height, &s1, &t1, &s2, &t2 );
		width = rc.right - rc.left;
		height = rc.bottom - rc.top;
	}
	else
	{
		s1 = t1 = 0.0f;
		s2 = t2 = 1.0f;
	}

	// pass scissor test if supposed
	if( !CL_Scissor( &clgame.ds.scissor, &x, &y, &width, &height, &s1, &t1, &s2, &t2 ))
		return;

	// scale for screen sizes
	SPR_AdjustSize( &x, &y, &width, &height );
	ref.dllFuncs.Color4ub( clgame.ds.spriteColor[0], clgame.ds.spriteColor[1], clgame.ds.spriteColor[2], clgame.ds.spriteColor[3] );
	ref.dllFuncs.R_DrawStretchPic( x, y, width, height, s1, t1, s2, t2, texnum );
}

/*
=============
CL_DrawCenterPrint

called each frame
=============
*/
void CL_DrawCenterPrint( void )
{
	cl_font_t *font = Con_GetCurFont();
	char	*pText;
	int	i, j, x, y;
	int	width, lineLength;
	byte	*colorDefault, line[MAX_LINELENGTH];
	int	charWidth, charHeight;

	if( !clgame.centerPrint.time )
		return;

	if(( cl.time - clgame.centerPrint.time ) >= scr_centertime.value )
	{
		// time expired
		clgame.centerPrint.time = 0.0f;
		return;
	}

	y = clgame.centerPrint.y; // start y
	colorDefault = g_color_table[7];
	pText = clgame.centerPrint.message;

	CL_DrawCharacterLen( font, 0, NULL, &charHeight );
	CL_SetFontRendermode( font );
	for( i = 0; i < clgame.centerPrint.lines; i++ )
	{
		lineLength = 0;
		width = 0;

		while( *pText && *pText != '\n' && lineLength < MAX_LINELENGTH )
		{
			int number = Con_UtfProcessChar(( byte ) * pText );
			pText++;
			if( number == 0 )
				continue;

			line[lineLength] = number;
			CL_DrawCharacterLen( font, number, &charWidth, NULL );
			width += charWidth;
			lineLength++;
		}

		if( lineLength == MAX_LINELENGTH )
			lineLength--;

		pText++; // Skip LineFeed
		line[lineLength] = 0;

		x = CL_AdjustXPos( -1, width, clgame.centerPrint.totalWidth );

		for( j = 0; j < lineLength; j++ )
		{
			if( x >= 0 && y >= 0 && x <= refState.width )
				x += CL_DrawCharacter( x, y, line[j], colorDefault, font, FONT_DRAW_HUD | FONT_DRAW_NORENDERMODE );
		}
		y += charHeight;
	}
}

static int V_FadeAlpha( screenfade_t *sf )
{
	int alpha;

	if( cl.time > sf->fadeReset && cl.time > sf->fadeEnd )
	{
		if( !FBitSet( sf->fadeFlags, FFADE_STAYOUT ))
			return 0;
	}

	if( FBitSet( sf->fadeFlags, FFADE_STAYOUT ))
	{
		alpha = sf->fadealpha;
		if( FBitSet( sf->fadeFlags, FFADE_OUT ) && sf->fadeTotalEnd > cl.time )
		{
			alpha += sf->fadeSpeed * ( sf->fadeTotalEnd - cl.time );
		}
		else
		{
			sf->fadeEnd = cl.time + 0.1;
		}
	}
	else
	{
		alpha = sf->fadeSpeed * ( sf->fadeEnd - cl.time );
		if( FBitSet( sf->fadeFlags, FFADE_OUT ))
		{
			alpha += sf->fadealpha;
		}
	}
	alpha = bound( 0, alpha, sf->fadealpha );

	return alpha;
}

/*
=============
CL_DrawScreenFade

fill screen with specfied color
can be modulated
=============
*/
static void CL_DrawScreenFade( void )
{
	screenfade_t	*sf = &clgame.fade;
	int		alpha;

	alpha = V_FadeAlpha( sf );

	if( !alpha )
		return;

	if( !FBitSet( sf->fadeFlags, FFADE_MODULATE ))
	{
		ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
		ref.dllFuncs.Color4ub( sf->fader, sf->fadeg, sf->fadeb, alpha );
	}
	else if( Host_IsQuakeCompatible( ))
	{
		// Quake Wrapper and Quake Remake use FFADE_MODULATE for item pickups
		// so hack the check here
		ref.dllFuncs.GL_SetRenderMode( kRenderTransAdd );
		ref.dllFuncs.Color4ub( sf->fader, sf->fadeg, sf->fadeb, alpha );
	}
	else
	{
		ref.dllFuncs.GL_SetRenderMode( kRenderScreenFadeModulate );

		ref.dllFuncs.Color4ub(
			(uint16_t)( sf->fader * alpha + ( 255 - alpha ) * 255 ) >> 8,
			(uint16_t)( sf->fadeg * alpha + ( 255 - alpha ) * 255 ) >> 8,
			(uint16_t)( sf->fadeb * alpha + ( 255 - alpha ) * 255 ) >> 8,
			255 );
	}

	ref.dllFuncs.R_DrawStretchPic( 0, 0, refState.width, refState.height, 0, 0, 1, 1,
		R_GetBuiltinTexture( REF_WHITE_TEXTURE ));
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
}

/*
====================
CL_InitTitles

parse all messages that declared in titles.txt
and hold them into permament memory pool
====================
*/
static void CL_InitTitles( const char *filename )
{
	fs_offset_t	fileSize;
	byte	*pMemFile;
	int	i;

	// initialize text messages (game_text)
	for( i = 0; i < MAX_TEXTCHANNELS; i++ )
	{
		char name[MAX_VA_STRING];

		Q_snprintf( name, sizeof( name ), TEXT_MSGNAME, i );

		cl_textmessage[i].pName = copystringpool( clgame.mempool, name );
		cl_textmessage[i].pMessage = cl_textbuffer[i];
	}

	// clear out any old data that's sitting around.
	if( clgame.titles ) Mem_Free( clgame.titles );

	clgame.titles = NULL;
	clgame.numTitles = 0;

	pMemFile = FS_LoadFile( filename, &fileSize, false );
	if( !pMemFile ) return;

	CL_TextMessageParse( pMemFile, fileSize );
	Mem_Free( pMemFile );
}

/*
====================
CL_HudMessage

Template to show hud messages
====================
*/
void CL_HudMessage( const char *pMessage )
{
	if( COM_StringEmptyOrNULL( pMessage ))
		return;

	CL_DispatchUserMessage( "HudText", Q_strlen( pMessage ) + 1, (void *)pMessage );
}

/*
====================
CL_ParseTextMessage

Parse TE_TEXTMESSAGE
====================
*/
void CL_ParseTextMessage( sizebuf_t *msg )
{
	static int		msgindex = 0;
	client_textmessage_t	*text;
	int			channel;

	// read channel ( 0 - auto)
	channel = MSG_ReadByte( msg );

	if( channel <= 0 || channel > ( MAX_TEXTCHANNELS - 1 ))
	{
		channel = msgindex;
		msgindex = (msgindex + 1) & (MAX_TEXTCHANNELS - 1);
	}

	// grab message channel
	text = &cl_textmessage[channel];

	text->x = (float)(MSG_ReadShort( msg ) / 8192.0f);
	text->y = (float)(MSG_ReadShort( msg ) / 8192.0f);
	text->effect = MSG_ReadByte( msg );
	text->r1 = MSG_ReadByte( msg );
	text->g1 = MSG_ReadByte( msg );
	text->b1 = MSG_ReadByte( msg );
	text->a1 = MSG_ReadByte( msg );
	text->r2 = MSG_ReadByte( msg );
	text->g2 = MSG_ReadByte( msg );
	text->b2 = MSG_ReadByte( msg );
	text->a2 = MSG_ReadByte( msg );
	text->fadein = (float)(MSG_ReadWord( msg ) / 256.0f );
	text->fadeout = (float)(MSG_ReadWord( msg ) / 256.0f );
	text->holdtime = (float)(MSG_ReadWord( msg ) / 256.0f );

	if( text->effect == 2 )
		text->fxtime = (float)(MSG_ReadWord( msg ) / 256.0f );
	else text->fxtime = 0.0f;

	// to prevent grab too long messages
	Q_strncpy( (char *)text->pMessage, MSG_ReadString( msg ), 2048 );

	CL_HudMessage( text->pName );
}

/*
================
CL_ParseFinaleCutscene

show display finale or cutscene message
================
*/
void CL_ParseFinaleCutscene( sizebuf_t *msg, int level )
{
	static int		msgindex = 0;
	client_textmessage_t	*text;
	int			channel;

	cl.intermission = level;

	channel = msgindex;
	msgindex = (msgindex + 1) & (MAX_TEXTCHANNELS - 1);

	// grab message channel
	text = &cl_textmessage[channel];

	// NOTE: svc_finale and svc_cutscene has a
	// predefined settings like Quake-style
	text->x = -1.0f;
	text->y = 0.15f;
	text->effect = 2;	// scan out effect
	text->r1 = 245;
	text->g1 = 245;
	text->b1 = 245;
	text->a1 = 0;	// unused
	text->r2 = 0;
	text->g2 = 0;
	text->b2 = 0;
	text->a2 = 0;
	text->fadein = 0.15f;
	text->fadeout = 0.0f;
	text->holdtime = 99999.0f;
	text->fxtime = 0.0f;

	// to prevent grab too long messages
	Q_strncpy( (char *)text->pMessage, MSG_ReadString( msg ), 2048 );

	if( *text->pMessage == '\0' )
		return; // no real text

	CL_HudMessage( text->pName );
}

/*
====================
CL_GetMaxlients

Render callback for studio models
====================
*/
int GAME_EXPORT CL_GetMaxClients( void )
{
	return cl.maxclients;
}

/*
====================
CL_SoundFromIndex

return soundname from index
====================
*/
static const char *CL_SoundFromIndex( int index )
{
	sfx_t	*sfx = NULL;
	int	hSound;

	// make sure what we in-bounds
	index = bound( 0, index, MAX_SOUNDS );
	hSound = cl.sound_index[index];

	if( !hSound )
	{
		Con_DPrintf( S_ERROR "%s: invalid sound index %i\n", __func__, index );
		return NULL;
	}

	sfx = S_GetSfxByHandle( hSound );
	if( !sfx )
	{
		Con_DPrintf( S_ERROR "%s: bad sfx for index %i\n", __func__, index );
		return NULL;
	}

	return sfx->name;
}

/*
================
CL_EnableScissor

enable scissor test
================
*/
void CL_EnableScissor( scissor_state_t *scissor, int x, int y, int width, int height )
{
	scissor->x = x;
	scissor->y = y;
	scissor->width = width;
	scissor->height = height;
	scissor->test = true;
}

/*
================
CL_DisableScissor

disable scissor test
================
*/
void CL_DisableScissor( scissor_state_t *scissor )
{
	scissor->test = false;
}

/*
================
CL_Scissor

perform common scissor test
================
*/
qboolean CL_Scissor( const scissor_state_t *scissor, float *x, float *y, float *width, float *height, float *u0, float *v0, float *u1, float *v1 )
{
	float dudx, dvdy;

	if( !scissor->test )
		return true;

	// clip sub rect to sprite
	if( *width == 0 || *height == 0 )
		return false;

	if( *x + *width <= scissor->x )
		return false;
	if( *x >= scissor->x + scissor->width )
		return false;
	if( *y + *height <= scissor->y )
		return false;
	if( *y >= scissor->y + scissor->height )
		return false;

	dudx = (*u1 - *u0) / *width;
	dvdy = (*v1 - *v0) / *height;

	if( *x < scissor->x )
	{
		*u0 += (scissor->x - *x) * dudx;
		*width -= scissor->x - *x;
		*x = scissor->x;
	}

	if( *x + *width > scissor->x + scissor->width )
	{
		*u1 -= (*x + *width - (scissor->x + scissor->width)) * dudx;
		*width = scissor->x + scissor->width - *x;
	}

	if( *y < scissor->y )
	{
		*v0 += (scissor->y - *y) * dvdy;
		*height -= scissor->y - *y;
		*y = scissor->y;
	}

	if( *y + *height > scissor->y + scissor->height )
	{
		*v1 -= (*y + *height - (scissor->y + scissor->height)) * dvdy;
		*height = scissor->y + scissor->height - *y;
	}
	return true;
}

/*
=========
SPR_EnableScissor

=========
*/
static void GAME_EXPORT SPR_EnableScissor( int x, int y, int width, int height )
{
	// check bounds
	x = bound( 0, x, clgame.scrInfo.iWidth );
	y = bound( 0, y, clgame.scrInfo.iHeight );
	width = bound( 0, width, clgame.scrInfo.iWidth - x );
	height = bound( 0, height, clgame.scrInfo.iHeight - y );

	CL_EnableScissor( &clgame.ds.scissor, x, y, width, height );
}

/*
=========
SPR_DisableScissor

=========
*/
static void GAME_EXPORT SPR_DisableScissor( void )
{
	CL_DisableScissor( &clgame.ds.scissor );
}

/*
====================
CL_DrawCrosshair

Render crosshair
====================
*/
static void CL_DrawCrosshair( void )
{
	int	x, y, width, height;
	float xscale, yscale;

	if( !clgame.ds.pCrosshair || !cl_crosshair.value )
		return;

	// any camera on or client is died
	if( cl.local.health <= 0 || cl.viewentity != ( cl.playernum + 1 ))
		return;

	// get crosshair dimension
	width = clgame.ds.rcCrosshair.right - clgame.ds.rcCrosshair.left;
	height = clgame.ds.rcCrosshair.bottom - clgame.ds.rcCrosshair.top;

	x = clgame.viewport[0] + ( clgame.viewport[2] >> 1 );
	y = clgame.viewport[1] + ( clgame.viewport[3] >> 1 );

	// g-cont - cl.crosshairangle is the autoaim angle.
	// if we're not using autoaim, just draw in the middle of the screen
	if( !VectorIsNull( cl.crosshairangle ))
	{
		vec3_t	angles;
		vec3_t	forward;
		vec3_t	point, screen;

		VectorAdd( refState.viewangles, cl.crosshairangle, angles );
		AngleVectors( angles, forward, NULL, NULL );
		VectorAdd( refState.vieworg, forward, point );
		ref.dllFuncs.WorldToScreen( point, screen );

		x += ( clgame.viewport[2] >> 1 ) * screen[0] + 0.5f;
		y += ( clgame.viewport[3] >> 1 ) * screen[1] + 0.5f;
	}

	// back to logical sizes
	xscale = (float)clgame.scrInfo.iWidth / refState.width;
	yscale = (float)clgame.scrInfo.iHeight / refState.height;

	x *= xscale;
	y *= yscale;

	// move at center the screen
	x -= 0.5f * width;
	y -= 0.5f * height;

	clgame.ds.pSprite = clgame.ds.pCrosshair;
	Vector4Copy( clgame.ds.rgbaCrosshair, clgame.ds.spriteColor );

	pfnSPR_DrawHoles( 0, x, y, &clgame.ds.rcCrosshair );
}

/*
=============
CL_DrawLoading

draw loading progress bar
=============
*/
static void CL_DrawLoadingOrPaused( int tex )
{
	float	x, y, width, height;
	int iWidth, iHeight;

	R_GetTextureParms( &iWidth, &iHeight, tex );
	x = ( clgame.scrInfo.iWidth - iWidth ) / 2.0f;
	y = ( clgame.scrInfo.iHeight - iHeight ) / 2.0f;
	width = iWidth;
	height = iHeight;

	SPR_AdjustSize( &x, &y, &width, &height );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
	ref.dllFuncs.R_DrawStretchPic( x, y, width, height, 0, 0, 1, 1, tex );
}

void CL_DrawHUD( int state )
{
	if( state == CL_ACTIVE && !cl.video_prepped )
		state = CL_LOADING;

	if( state == CL_ACTIVE && cl.paused )
		state = CL_PAUSED;

	switch( state )
	{
	case CL_ACTIVE:
		if( !cl.intermission )
			CL_DrawScreenFade ();
		CL_DrawCrosshair ();
		CL_DrawCenterPrint ();
		clgame.dllFuncs.pfnRedraw( cl.time, cl.intermission );
		if( cl.intermission ) CL_DrawScreenFade ();
		break;
	case CL_PAUSED:
		CL_DrawScreenFade ();
		CL_DrawCrosshair ();
		CL_DrawCenterPrint ();
		clgame.dllFuncs.pfnRedraw( cl.time, cl.intermission );
		if( showpause.value )
		{
			if( !cls.pauseIcon )
				cls.pauseIcon = SCR_LoadPauseIcon();
			CL_DrawLoadingOrPaused( Q_max( 0, cls.pauseIcon ));
		}
		break;
	case CL_LOADING:
		CL_DrawLoadingOrPaused( cls.loadingBar );
		break;
	case CL_CHANGELEVEL:
		if( cls.draw_changelevel )
		{
			CL_DrawLoadingOrPaused( cls.loadingBar );
			cls.draw_changelevel = false;
		}
		break;
	}
}

static void CL_ClearUserMessage( char *pszName, int svc_num )
{
	int i;

	for( i = 0; i < MAX_USER_MESSAGES && clgame.msg[i].name[0]; i++ )
		if( ( clgame.msg[i].number == svc_num ) && Q_stricmp( clgame.msg[i].name, pszName ) )
			clgame.msg[i].number = 0;
}

void CL_LinkUserMessage( char *pszName, const int svc_num, int iSize )
{
	int	i;

	if( !pszName || !*pszName )
		Host_Error( "%s: bad message name\n", __func__ );

	if( svc_num <= svc_lastmsg )
		Host_Error( "%s: tried to hook a system message \"%s\"\n", __func__, svc_strings[svc_num] );

	// see if already hooked
	for( i = 0; i < MAX_USER_MESSAGES && clgame.msg[i].name[0]; i++ )
	{
		// NOTE: no check for DispatchFunc, check only name
		if( !Q_stricmp( clgame.msg[i].name, pszName ))
		{
			clgame.msg[i].number = svc_num;
			clgame.msg[i].size = iSize;
			CL_ClearUserMessage( pszName, svc_num );
			return;
		}
	}

	if( i == MAX_USER_MESSAGES )
	{
		Host_Error( "%s: MAX_USER_MESSAGES hit!\n", __func__ );
		return;
	}

	// register new message without DispatchFunc, so we should parse it properly
	Q_strncpy( clgame.msg[i].name, pszName, sizeof( clgame.msg[i].name ));
	clgame.msg[i].number = svc_num;
	clgame.msg[i].size = iSize;
	CL_ClearUserMessage( pszName, svc_num );
}

void CL_ClearWorld( void )
{
	if( clgame.entities ) // check if we have entities, legacy protocol support kinda breaks this logic
	{
		cl_entity_t *worldmodel = clgame.entities;

		worldmodel->curstate.modelindex = 1;	// world model
		worldmodel->curstate.solid = SOLID_BSP;
		worldmodel->curstate.movetype = MOVETYPE_PUSH;
		worldmodel->model = cl.worldmodel;
		worldmodel->index = 0;
	}

	world.max_recursion = 0;

	clgame.ds.cullMode = TRI_FRONT;
	clgame.numStatics = 0;
}

void CL_InitEdicts( int maxclients )
{
	Assert( clgame.entities == NULL );

	if( !clgame.mempool ) return; // Host_Error without client
#if XASH_LOW_MEMORY != 2
	CL_UPDATE_BACKUP = ( maxclients <= 1 ) ? SINGLEPLAYER_BACKUP : MULTIPLAYER_BACKUP;
#endif
	cls.num_client_entities = CL_UPDATE_BACKUP * NUM_PACKET_ENTITIES;
	cls.packet_entities = Mem_Realloc( clgame.mempool, cls.packet_entities, sizeof( entity_state_t ) * cls.num_client_entities );
	clgame.entities = Mem_Calloc( clgame.mempool, sizeof( cl_entity_t ) * clgame.maxEntities );
	clgame.static_entities = NULL; // will be initialized later
	clgame.numStatics = 0;

	if(( clgame.maxRemapInfos - 1 ) != clgame.maxEntities )
	{
		CL_ClearAllRemaps (); // purge old remap info
		clgame.maxRemapInfos = clgame.maxEntities + 1;
		clgame.remap_info = (remap_info_t **)Mem_Calloc( clgame.mempool, sizeof( remap_info_t* ) * clgame.maxRemapInfos );
	}

	ref.dllFuncs.R_ProcessEntData( true, clgame.entities, clgame.maxEntities );
}

void CL_FreeEdicts( void )
{
	ref.dllFuncs.R_ProcessEntData( false, NULL, 0 );

	if( clgame.entities )
		Mem_Free( clgame.entities );
	clgame.entities = NULL;

	if( clgame.static_entities )
		Mem_Free( clgame.static_entities );
	clgame.static_entities = NULL;

	if( cls.packet_entities )
		Z_Free( cls.packet_entities );

	cls.packet_entities = NULL;
	cls.num_client_entities = 0;
	cls.next_client_entities = 0;
	clgame.numStatics = 0;
}

void CL_ClearEdicts( void )
{
	if( clgame.entities != NULL )
		return;

	// in case we stopped with error
	clgame.maxEntities = 2;
	CL_InitEdicts( cl.maxclients );
}

/*
==================
CL_ClearSpriteTextures

free studio cache on change level
==================
*/
void CL_ClearSpriteTextures( void )
{
	int	i;

	for( i = 1; i < MAX_CLIENT_SPRITES; i++ )
	{
		if( clgame.sprites[i].needload == NL_UNREFERENCED )
			continue;

		clgame.sprites[i].needload = NL_FREE_UNUSED;
	}
}

// it's a Valve default value for LoadMapSprite (probably must be power of two)
#define MAPSPRITE_SIZE	128

/*
====================
Mod_LoadMapSprite

Loading a bitmap image as sprite with multiple frames
as pieces of input image
====================
*/
static void Mod_LoadMapSprite( model_t *mod, const void *buffer, size_t size, qboolean *loaded )
{
	rgbdata_t *pix, temp = { 0 };
	char texname[128];
	int i, w, h;
	int xl, yl;
	int numframes;
	msprite_t *psprite;
	char poolname[MAX_VA_STRING];

	if( loaded ) *loaded = false;
	Q_snprintf( texname, sizeof( texname ), "#%s", mod->name );
	Image_SetForceFlags( IL_OVERVIEW );
	pix = FS_LoadImage( texname, buffer, size );
	Image_ClearForceFlags();
	if( !pix ) return; // bad image or something else

	mod->type = mod_sprite;

	if( pix->width % MAPSPRITE_SIZE )
		w = pix->width - ( pix->width % MAPSPRITE_SIZE );
	else w = pix->width;

	if( pix->height % MAPSPRITE_SIZE )
		h = pix->height - ( pix->height % MAPSPRITE_SIZE );
	else h = pix->height;

	if( w < MAPSPRITE_SIZE ) w = MAPSPRITE_SIZE;
	if( h < MAPSPRITE_SIZE ) h = MAPSPRITE_SIZE;

	// resample image if needed
	Image_Process( &pix, w, h, IMAGE_FORCE_RGBA|IMAGE_RESAMPLE, 0.0f );

	w = h = MAPSPRITE_SIZE;

	// check range
	if( w > pix->width ) w = pix->width;
	if( h > pix->height ) h = pix->height;

	// determine how many frames we needs
	numframes = (pix->width * pix->height) / (w * h);
	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );
	mod->mempool = Mem_AllocPool( poolname );
	psprite = Mem_Calloc( mod->mempool, sizeof( msprite_t ) + ( numframes - 1 ) * sizeof( psprite->frames ));
	mod->cache.data = psprite;	// make link to extradata

	psprite->type = SPR_FWD_PARALLEL_ORIENTED;
	psprite->texFormat = SPR_ALPHTEST;
	psprite->numframes = mod->numframes = numframes;
	psprite->radius = sqrt(((w >> 1) * (w >> 1)) + ((h >> 1) * (h >> 1)));

	mod->mins[0] = mod->mins[1] = -w / 2;
	mod->maxs[0] = mod->maxs[1] = w / 2;
	mod->mins[2] = -h / 2;
	mod->maxs[2] = h / 2;

	// create a temporary pic
	temp.width = w;
	temp.height = h;
	temp.type = pix->type;
	temp.flags = pix->flags;
	temp.size = w * h * PFDesc[temp.type].bpp;
	temp.buffer = Mem_Malloc( mod->mempool, temp.size );
	temp.palette = NULL;

	// chop the image and upload into video memory
	for( i = xl = yl = 0; i < numframes; i++ )
	{
		mspriteframe_t *pspriteframe;
		int xh = xl + w, yh = yl + h, x, y, j;
		int linedelta = ( pix->width - w ) * 4;
		byte *src = pix->buffer + ( yl * pix->width + xl ) * 4;
		byte *dst = temp.buffer;

		// cut block from source
		for( y = yl; y < yh; y++ )
		{
			for( x = xl; x < xh; x++ )
				for( j = 0; j < 4; j++ )
					*dst++ = *src++;
			src += linedelta;
		}

		// build uinque frame name
		Q_snprintf( texname, sizeof( texname ), "#MAP/%s_%i%i.spr", mod->name, i / 10, i % 10 );

		psprite->frames[i].frameptr = Mem_Calloc( mod->mempool, sizeof( mspriteframe_t ));
		pspriteframe = psprite->frames[i].frameptr;
		pspriteframe->width = w;
		pspriteframe->height = h;
		pspriteframe->up = ( h >> 1 );
		pspriteframe->left = -( w >> 1 );
		pspriteframe->down = ( h >> 1 ) - h;
		pspriteframe->right = w + -( w >> 1 );
		pspriteframe->gl_texturenum = GL_LoadTextureInternal( texname, &temp, TF_IMAGE );

		xl += w;
		if( xl >= pix->width )
		{
			xl = 0;
			yl += h;
		}
	}

	FS_FreeImage( pix );
	Mem_Free( temp.buffer );
	if( loaded ) *loaded = true;
}

/*
=============
CL_LoadHudSprite

upload sprite frames
=============
*/
static qboolean CL_LoadHudSprite( const char *szSpriteName, model_t *m_pSprite, uint type, uint texFlags )
{
	byte	*buf;
	fs_offset_t	size;
	qboolean	loaded;

	Assert( m_pSprite != NULL );

	Q_strncpy( m_pSprite->name, szSpriteName, sizeof( m_pSprite->name ));

	// it's hud sprite, make difference names to prevent free shared textures
	if( type == SPR_CLIENT || type == SPR_HUDSPRITE )
		SetBits( m_pSprite->flags, MODEL_CLIENT );

	m_pSprite->numtexinfo = texFlags; // store texFlags for renderer into numtexinfo

	if( !FS_FileExists( szSpriteName, false ) )
	{
		if( cls.state != ca_active && cl.maxclients > 1 )
		{
			// trying to download sprite from server
			CL_AddClientResource( szSpriteName, t_model );
			m_pSprite->needload = NL_NEEDS_LOADED;
			return true;
		}
		else
		{
			Con_Reportf( S_ERROR "Could not load HUD sprite %s\n", szSpriteName );
			Mod_FreeModel( m_pSprite );
			return false;
		}
	}

	buf = FS_LoadFile( szSpriteName, &size, false );
	if( buf == NULL )
		return false;

	if( type == SPR_MAPSPRITE )
		Mod_LoadMapSprite( m_pSprite, buf, size, &loaded );
	else
	{
		Mod_LoadSpriteModel( m_pSprite, buf, size, &loaded );
		ref.dllFuncs.Mod_ProcessRenderData( m_pSprite, true, buf, size );
	}

	Mem_Free( buf );

	if( !loaded )
	{
		Mod_FreeModel( m_pSprite );
		return false;
	}

	m_pSprite->needload = NL_PRESENT;

	return true;
}

/*
=============
CL_LoadSpriteModel

some sprite models is exist only at client: HUD sprites,
tent sprites or overview images
=============
*/
static model_t *CL_LoadSpriteModel( const char *filename, uint type, uint texFlags )
{
	char	name[MAX_QPATH];
	model_t	*mod;
	int	i, start;

	if( COM_StringEmptyOrNULL( filename ))
	{
		Con_Reportf( S_ERROR "%s: bad name!\n", __func__ );
		return NULL;
	}

	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	for( i = 0, mod = clgame.sprites; i < MAX_CLIENT_SPRITES; i++, mod++ )
	{
		if( !Q_stricmp( mod->name, name ))
		{
			if( mod->needload == NL_NEEDS_LOADED )
			{
				if( CL_LoadHudSprite( name, mod, type, texFlags ))
					return mod;
			}

			// prolonge registration
			mod->needload = NL_PRESENT;
			return mod;
		}
	}

	// find a free model slot spot
	// use low indices only for HUD sprites
	// for GoldSrc bug compatibility
	start = type == SPR_HUDSPRITE ? 0 : MAX_CLIENT_SPRITES / 2;

	for( i = 0, mod = &clgame.sprites[start]; i < MAX_CLIENT_SPRITES / 2; i++, mod++ )
	{
		if( mod->needload == NL_UNREFERENCED )
			break; // this is a valid spot
	}

	if( i == MAX_CLIENT_SPRITES / 2 )
	{
		Con_Printf( S_ERROR "MAX_CLIENT_SPRITES limit exceeded (%d)\n", MAX_CLIENT_SPRITES / 2 );
		return NULL;
	}

	// load new map sprite
	if( CL_LoadHudSprite( name, mod, type, texFlags ))
		return mod;
	return NULL;
}

/*
=============
CL_LoadClientSprite

load sprites for temp ents
=============
*/
model_t *CL_LoadClientSprite( const char *filename )
{
	return CL_LoadSpriteModel( filename, SPR_CLIENT, 0 );
}

/*
===============================================================================
	CGame Builtin Functions

===============================================================================
*/
/*
=========
pfnSPR_LoadExt

=========
*/
HSPRITE pfnSPR_LoadExt( const char *szPicName, uint texFlags )
{
	model_t	*spr;

	if(( spr = CL_LoadSpriteModel( szPicName, SPR_CLIENT, texFlags )) == NULL )
		return 0;

	return (spr - clgame.sprites) + 1; // return index
}

/*
=========
pfnSPR_Load

function exported for support GoldSrc Monitor utility
=========
*/
HSPRITE EXPORT pfnSPR_Load( const char *szPicName );
HSPRITE EXPORT pfnSPR_Load( const char *szPicName )
{
	model_t	*spr;

	if(( spr = CL_LoadSpriteModel( szPicName, SPR_HUDSPRITE, 0 )) == NULL )
		return 0;

	return (spr - clgame.sprites) + 1; // return index
}

/*
=============
CL_GetSpritePointer

=============
*/
static const model_t *CL_GetSpritePointer( HSPRITE hSprite )
{
	model_t	*mod;
	int index = hSprite - 1;

	if( index < 0 || index >= MAX_CLIENT_SPRITES )
		return NULL; // bad image
	mod = &clgame.sprites[index];

	if( mod->needload == NL_NEEDS_LOADED )
	{
		int	type = FBitSet( mod->flags, MODEL_CLIENT ) ? SPR_HUDSPRITE : SPR_MAPSPRITE;

		if( CL_LoadHudSprite( mod->name, mod, type, mod->numtexinfo ))
			return mod;
	}

	if( mod->mempool )
	{
		mod->needload = NL_PRESENT;
		return mod;
	}

	return NULL;
}

/*
=========
pfnSPR_Frames

function exported for support GoldSrc Monitor utility
=========
*/
int EXPORT pfnSPR_Frames( HSPRITE hPic );
int EXPORT pfnSPR_Frames( HSPRITE hPic )
{
	int	numFrames = 0;

	ref.dllFuncs.R_GetSpriteParms( NULL, NULL, &numFrames, 0, CL_GetSpritePointer( hPic ));

	return numFrames;
}

/*
=========
pfnSPR_Height

=========
*/
static int GAME_EXPORT pfnSPR_Height( HSPRITE hPic, int frame )
{
	int	sprHeight = 0;

	ref.dllFuncs.R_GetSpriteParms( NULL, &sprHeight, NULL, frame, CL_GetSpritePointer( hPic ));

	return sprHeight;
}

/*
=========
pfnSPR_Width

=========
*/
static int GAME_EXPORT pfnSPR_Width( HSPRITE hPic, int frame )
{
	int	sprWidth = 0;

	ref.dllFuncs.R_GetSpriteParms( &sprWidth, NULL, NULL, frame, CL_GetSpritePointer( hPic ));

	return sprWidth;
}

/*
=========
pfnSPR_Set

=========
*/
static void GAME_EXPORT pfnSPR_Set( HSPRITE hPic, int r, int g, int b )
{
	const model_t *sprite = CL_GetSpritePointer( hPic );

	// a1ba: do not alter the state if invalid HSPRITE was passed
	if( !sprite )
		return;

	clgame.ds.pSprite = sprite;
	clgame.ds.spriteColor[0] = bound( 0, r, 255 );
	clgame.ds.spriteColor[1] = bound( 0, g, 255 );
	clgame.ds.spriteColor[2] = bound( 0, b, 255 );
	clgame.ds.spriteColor[3] = 255;
}

/*
=========
pfnSPR_Draw

=========
*/
static void GAME_EXPORT pfnSPR_Draw( int frame, int x, int y, const wrect_t *prc )
{
	ref.dllFuncs.GL_SetRenderMode( kRenderTransAlpha );
	SPR_DrawGeneric( frame, x, y, -1, -1, prc );
}

/*
=========
pfnSPR_DrawHoles

=========
*/
static void GAME_EXPORT pfnSPR_DrawHoles( int frame, int x, int y, const wrect_t *prc )
{
#if 1 // REFTODO
	ref.dllFuncs.GL_SetRenderMode( kRenderTransColor );
#else
	pglEnable( GL_ALPHA_TEST );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglEnable( GL_BLEND );
#endif
	SPR_DrawGeneric( frame, x, y, -1, -1, prc );

#if 1
	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
#else
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_BLEND );
#endif
}

/*
=========
pfnSPR_DrawAdditive

=========
*/
static void GAME_EXPORT pfnSPR_DrawAdditive( int frame, int x, int y, const wrect_t *prc )
{
#if 1 // REFTODO
	ref.dllFuncs.GL_SetRenderMode( kRenderTransAdd );
#else
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_ONE, GL_ONE );
#endif

	SPR_DrawGeneric( frame, x, y, -1, -1, prc );

#if 1 // REFTODO
	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
#else
	pglDisable( GL_BLEND );
#endif
}

/*
=========
SPR_GetList

for parsing half-life scripts - hud.txt etc
=========
*/
static client_sprite_t *SPR_GetList( char *psz, int *piCount )
{
	cached_spritelist_t	*pEntry = &clgame.sprlist[0];
	int		slot, index, numSprites = 0;
	byte *afile;
	char *pfile;
	string		token;

	if( piCount ) *piCount = 0;

	// see if already in list
	// NOTE: client.dll is cache hud.txt but reparse weapon lists again and again
	// obviously there a memory leak by-design. Cache the sprite lists to prevent it
	for( slot = 0; slot < MAX_CLIENT_SPRITES && pEntry->szListName[0]; slot++ )
	{
		pEntry = &clgame.sprlist[slot];

		if( !Q_stricmp( pEntry->szListName, psz ))
		{
			if( piCount ) *piCount = pEntry->count;
			return pEntry->pList;
		}
	}

	if( slot == MAX_CLIENT_SPRITES )
	{
		Con_Printf( S_ERROR "%s: overflow cache!\n", __func__ );
		return NULL;
	}

	if( !clgame.itemspath[0] )	// typically it's sprites\*.txt
		COM_ExtractFilePath( psz, clgame.itemspath );

	afile = FS_LoadFile( psz, NULL, false );
	if( !afile ) return NULL;

	pfile = (char *)afile;
	pfile = COM_ParseFile( pfile, token, sizeof( token ));
	numSprites = Q_atoi( token );

	Q_strncpy( pEntry->szListName, psz, sizeof( pEntry->szListName ));

	// name, res, pic, x, y, w, h
	pEntry->pList = Mem_Calloc( cls.mempool, sizeof( client_sprite_t ) * numSprites );

	for( index = 0; index < numSprites; index++ )
	{
		if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
			break;

		Q_strncpy( pEntry->pList[index].szName, token, sizeof( pEntry->pList[0].szName ));

		// read resolution
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		pEntry->pList[index].iRes = Q_atoi( token );

		// read spritename
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		Q_strncpy( pEntry->pList[index].szSprite, token, sizeof( pEntry->pList[0].szSprite ));

		// parse rectangle
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		pEntry->pList[index].rc.left = Q_atoi( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		pEntry->pList[index].rc.top = Q_atoi( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		pEntry->pList[index].rc.right = pEntry->pList[index].rc.left + Q_atoi( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		pEntry->pList[index].rc.bottom = pEntry->pList[index].rc.top + Q_atoi( token );

		pEntry->count++;
	}

	if( index < numSprites )
		Con_DPrintf( S_WARN "unexpected end of %s (%i should be %i)\n", psz, numSprites, index );
	if( piCount ) *piCount = pEntry->count;
	Mem_Free( afile );

	return pEntry->pList;
}

/*
=============
CL_FillRGBA

=============
*/
static void GAME_EXPORT CL_FillRGBA( int x, int y, int w, int h, int r, int g, int b, int a )
{
	float x_ = x, y_ = y, w_ = w, h_ = h;

	r = bound( 0, r, 255 );
	g = bound( 0, g, 255 );
	b = bound( 0, b, 255 );
	a = bound( 0, a, 255 );

	SPR_AdjustSize( &x_, &y_, &w_, &h_ );

	ref.dllFuncs.FillRGBA( kRenderTransAdd, x_, y_, w_, h_, r, g, b, a );
}

/*
=============
pfnGetScreenInfo

get actual screen info
=============
*/
int GAME_EXPORT CL_GetScreenInfo( SCREENINFO *pscrinfo )
{
	qboolean apply_scale_factor = false; // we don't want floating point inaccuracies
	float scale_factor = hud_scale.value;

	if( FBitSet( hud_fontscale.flags, FCVAR_CHANGED ))
	{
		CL_FreeFont( &cls.creditsFont );
		SCR_LoadCreditsFont();

		ClearBits( hud_fontscale.flags, FCVAR_CHANGED );
	}

	// setup screen info
	clgame.scrInfo.iSize = sizeof( clgame.scrInfo );
	clgame.scrInfo.iFlags = SCRINFO_SCREENFLASH;

	if( hud_scale.value >= 320.0f && hud_scale.value >= hud_scale_minimal_width.value )
	{
		scale_factor = refState.width / hud_scale.value;
		apply_scale_factor = scale_factor > 1.0f;
	}
	else if( scale_factor && scale_factor != 1.0f )
	{
		float scaled_width = (float)refState.width / scale_factor;
		if( scaled_width >= hud_scale_minimal_width.value )
			apply_scale_factor = true;
	}

	if( apply_scale_factor )
	{
		clgame.scrInfo.iWidth = (float)refState.width / scale_factor;
		clgame.scrInfo.iHeight = (float)refState.height / scale_factor;
		SetBits( clgame.scrInfo.iFlags, SCRINFO_STRETCHED );
	}
	else
	{
		clgame.scrInfo.iWidth = refState.width;
		clgame.scrInfo.iHeight = refState.height;
		ClearBits( clgame.scrInfo.iFlags, SCRINFO_STRETCHED );
	}

	if( !pscrinfo ) return 0;

	if( pscrinfo->iSize != clgame.scrInfo.iSize )
		clgame.scrInfo.iSize = pscrinfo->iSize;

	// copy screeninfo out
	memcpy( pscrinfo, &clgame.scrInfo, clgame.scrInfo.iSize );

	return 1;
}

/*
=============
pfnSetCrosshair

setup crosshair
=============
*/
static void GAME_EXPORT pfnSetCrosshair( HSPRITE hspr, wrect_t rc, int r, int g, int b )
{
	clgame.ds.rgbaCrosshair[0] = (byte)r;
	clgame.ds.rgbaCrosshair[1] = (byte)g;
	clgame.ds.rgbaCrosshair[2] = (byte)b;
	clgame.ds.rgbaCrosshair[3] = (byte)0xFF;
	clgame.ds.pCrosshair = CL_GetSpritePointer( hspr );
	clgame.ds.rcCrosshair = rc;
}


/*
=============
pfnCvar_RegisterVariable

=============
*/
static cvar_t *GAME_EXPORT pfnCvar_RegisterClientVariable( const char *szName, const char *szValue, int flags )
{
	// a1ba: try to mitigate outdated client.dll vulnerabilities
	if( !Q_stricmp( szName, "motdfile" )
		|| !Q_stricmp( szName, "sensitivity" ))
		flags |= FCVAR_PRIVILEGED;

	return (cvar_t *)Cvar_Get( szName, szValue, flags|FCVAR_CLIENTDLL, Cvar_BuildAutoDescription( szName, flags|FCVAR_CLIENTDLL ));
}

static int GAME_EXPORT Cmd_AddClientCommand( const char *cmd_name, xcommand_t function )
{
	int flags = CMD_CLIENTDLL;

	// a1ba: try to mitigate outdated client.dll vulnerabilities
	if( !Q_stricmp( cmd_name, "motd_write" ))
		flags |= CMD_PRIVILEGED;

	return Cmd_AddCommandEx( cmd_name, function, "client command", flags, __func__ );
}

/*
=============
pfnHookUserMsg

=============
*/
static int GAME_EXPORT pfnHookUserMsg( const char *pszName, pfnUserMsgHook pfn )
{
	int	i;

	// ignore blank names or invalid callbacks
	if( !pszName || !*pszName || !pfn )
		return 0;

	for( i = 0; i < MAX_USER_MESSAGES && clgame.msg[i].name[0]; i++ )
	{
		// see if already hooked
		if( !Q_stricmp( clgame.msg[i].name, pszName ))
			return 1;
	}

	if( i == MAX_USER_MESSAGES )
	{
		Host_Error( "%s: MAX_USER_MESSAGES hit!\n", __func__ );
		return 0;
	}

	// hook new message
	Q_strncpy( clgame.msg[i].name, pszName, sizeof( clgame.msg[i].name ));
	clgame.msg[i].func = pfn;

	return 1;
}

/*
=============
pfnServerCmd

=============
*/
static int GAME_EXPORT pfnServerCmd( const char *szCmdString )
{
	if( COM_StringEmptyOrNULL( szCmdString ))
		return 0;

	// just like the client typed "cmd xxxxx" at the console
	MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
	MSG_WriteString( &cls.netchan.message, szCmdString );

	return 1;
}

/*
=============
pfnClientCmd

=============
*/
static int GAME_EXPORT pfnClientCmd( const char *szCmdString )
{
	if( COM_StringEmptyOrNULL( szCmdString ))
		return 0;

	if( cls.initialized )
	{
		Cbuf_AddText( szCmdString );
		Cbuf_AddText( "\n" );
	}
	else
	{
		// will exec later
		Q_strncat( host.deferred_cmd, szCmdString, sizeof( host.deferred_cmd ));
		Q_strncat( host.deferred_cmd, "\n", sizeof( host.deferred_cmd ));
	}

	return 1;
}

/*
=============
pfnFilteredClientCmd
=============
*/
static int GAME_EXPORT pfnFilteredClientCmd( const char *szCmdString )
{
	if( COM_StringEmptyOrNULL( szCmdString ))
		return 0;

	// a1ba:
	// there should be stufftext validator, that checks
	// hardcoded commands and disallows them before passing to
	// filtered buffer, returning 0
	// I've replaced it by hooking potentially exploitable
	// commands and variables(motd_write, motdfile, etc) in client interfaces

	Cbuf_AddFilteredText( szCmdString );
	Cbuf_AddFilteredText( "\n" );

	return 1;
}

/*
=============
pfnGetPlayerInfo

=============
*/
static void GAME_EXPORT pfnGetPlayerInfo( int ent_num, hud_player_info_t *pinfo )
{
	player_info_t	*player;

	ent_num -= 1; // player list if offset by 1 from ents

	if( ent_num >= cl.maxclients || ent_num < 0 || !cl.players[ent_num].name[0] )
	{
		pinfo->name = NULL;
		pinfo->thisplayer = false;
		return;
	}

	player = &cl.players[ent_num];
	pinfo->thisplayer = ( ent_num == cl.playernum ) ? true : false;
	pinfo->name = player->name;
	pinfo->model = player->model;
	pinfo->spectator = player->spectator;
	pinfo->ping = player->ping;
	pinfo->packetloss = player->packet_loss;
	pinfo->topcolor = player->topcolor;
	pinfo->bottomcolor = player->bottomcolor;
}

/*
=============
pfnPlaySoundByName

=============
*/
static void GAME_EXPORT pfnPlaySoundByName( const char *szSound, float volume )
{
	int hSound = S_RegisterSound( szSound );
	S_StartSound( NULL, cl.viewentity, CHAN_ITEM, hSound, volume, ATTN_NORM, PITCH_NORM, SND_STOP_LOOPING );
}

/*
=============
pfnPlaySoundByIndex

=============
*/
static void GAME_EXPORT pfnPlaySoundByIndex( int iSound, float volume )
{
	int hSound;

	// make sure what we in-bounds
	iSound = bound( 0, iSound, MAX_SOUNDS );
	hSound = cl.sound_index[iSound];
	if( !hSound ) return;

	S_StartSound( NULL, cl.viewentity, CHAN_ITEM, hSound, volume, ATTN_NORM, PITCH_NORM, SND_STOP_LOOPING );
}

/*
=============
pfnTextMessageGet

returns specified message from titles.txt
=============
*/
client_textmessage_t *CL_TextMessageGet( const char *pName )
{
	int	i;

	// first check internal messages
	for( i = 0; i < MAX_TEXTCHANNELS; i++ )
	{
		char name[MAX_VA_STRING];

		Q_snprintf( name, sizeof( name ), TEXT_MSGNAME, i );

		if( !Q_strcmp( pName, name ))
			return cl_textmessage + i;
	}

	// find desired message
	for( i = 0; i < clgame.numTitles; i++ )
	{
		if( !Q_stricmp( pName, clgame.titles[i].pName ))
			return clgame.titles + i;
	}
	return NULL; // found nothing
}

/*
=============
pfnDrawCharacter

returns drawed chachter width (in real screen pixels)
=============
*/
static int GAME_EXPORT pfnDrawCharacter( int x, int y, int number, int r, int g, int b )
{
	rgba_t color = { r, g, b, 255 };
	int flags = FONT_DRAW_HUD;

	if( hud_utf8.value )
		flags |= FONT_DRAW_UTF8;

	return CL_DrawCharacter( x, y, number, color, &cls.creditsFont, flags );
}

/*
=============
pfnDrawConsoleString

drawing string like a console string
=============
*/
int GAME_EXPORT pfnDrawConsoleString( int x, int y, char *string )
{
	cl_font_t *font = Con_GetFont( con_fontsize.value );
	rgba_t color;
	Vector4Copy( clgame.ds.textColor, color );
	Vector4Set( clgame.ds.textColor, 255, 255, 255, 255 );

	return x + CL_DrawString( x, y, string, color, font, FONT_DRAW_UTF8 | FONT_DRAW_HUD );
}

/*
=============
pfnDrawSetTextColor

set color for anything
=============
*/
void GAME_EXPORT pfnDrawSetTextColor( float r, float g, float b )
{
	// bound color and convert to byte
	clgame.ds.textColor[0] = (byte)bound( 0, r * 255, 255 );
	clgame.ds.textColor[1] = (byte)bound( 0, g * 255, 255 );
	clgame.ds.textColor[2] = (byte)bound( 0, b * 255, 255 );
	clgame.ds.textColor[3] = (byte)0xFF;
}

/*
=============
pfnDrawConsoleStringLen

compute string length in screen pixels
=============
*/
void GAME_EXPORT pfnDrawConsoleStringLen( const char *pText, int *length, int *height )
{
	cl_font_t *font = Con_GetFont( con_fontsize.value );

	if( height ) *height = font->charHeight;
	CL_DrawStringLen( font, pText, length, NULL, FONT_DRAW_UTF8 | FONT_DRAW_HUD );
}

/*
=============
pfnConsolePrint

prints directly into console (can skip notify)
=============
*/
static void GAME_EXPORT pfnConsolePrint( const char *string )
{
	if( COM_StringEmptyOrNULL( string ))
		return;

	// WON GoldSrc behavior
	if( string[0] != 1 )
		Con_Printf( "%s", string );
	else
		Con_NPrintf( 0, "%s", string + 1 );
}

/*
=============
pfnCenterPrint

holds and fade message at center of screen
like trigger_multiple message in q1
=============
*/
static void GAME_EXPORT pfnCenterPrint( const char *string )
{
	CL_CenterPrint( string, 0.25f );
}

/*
=========
GetWindowCenterX

=========
*/
static int GAME_EXPORT pfnGetWindowCenterX( void )
{
	int x = 0;

#if XASH_WIN32
	if( m_ignore.value )
	{
		POINT pos;
		GetCursorPos( &pos );
		return pos.x;
	}
#endif

#if XASH_SDL >= 2
	SDL_GetWindowPosition( host.hWnd, &x, NULL );
#endif

	return host.window_center_x + x;
}

/*
=========
GetWindowCenterY

=========
*/
static int GAME_EXPORT pfnGetWindowCenterY( void )
{
	int y = 0;

#if XASH_WIN32
	if( m_ignore.value )
	{
		POINT pos;
		GetCursorPos( &pos );
		return pos.y;
	}
#endif

#if XASH_SDL >= 2
	SDL_GetWindowPosition( host.hWnd, NULL, &y );
#endif

	return host.window_center_y + y;
}

/*
=============
pfnGetViewAngles

return interpolated angles from previous frame
=============
*/
static void GAME_EXPORT pfnGetViewAngles( float *angles )
{
	if( angles ) VectorCopy( cl.viewangles, angles );
}

/*
=============
pfnSetViewAngles

return interpolated angles from previous frame
=============
*/
static void GAME_EXPORT pfnSetViewAngles( float *angles )
{
	if( angles ) VectorCopy( angles, cl.viewangles );
}

/*
=============
pfnPhysInfo_ValueForKey

=============
*/
static const char* GAME_EXPORT pfnPhysInfo_ValueForKey( const char *key )
{
	return Info_ValueForKey( cls.physinfo, key );
}

/*
=============
pfnServerInfo_ValueForKey

=============
*/
static const char* GAME_EXPORT pfnServerInfo_ValueForKey( const char *key )
{
	return Info_ValueForKey( cl.serverinfo, key );
}

/*
=============
pfnGetClientMaxspeed

value that come from server
=============
*/
static float GAME_EXPORT pfnGetClientMaxspeed( void )
{
	return cl.local.maxspeed;
}

/*
=============
pfnIsNoClipping

=============
*/
static int GAME_EXPORT pfnIsNoClipping( void )
{
	return ( cl.frames[cl.parsecountmod].playerstate[cl.playernum].movetype == MOVETYPE_NOCLIP );
}

/*
=============
pfnGetViewModel

=============
*/
static cl_entity_t* GAME_EXPORT CL_GetViewModel( void )
{
	return &clgame.viewent;
}

/*
=============
pfnGetClientTime

=============
*/
static float GAME_EXPORT pfnGetClientTime( void )
{
	return cl.time;
}

/*
=============
pfnCalcShake

=============
*/
static void GAME_EXPORT pfnCalcShake( void )
{
	screen_shake_t *const shake = &clgame.shake;
	float frametime, fraction, freq;
	int i;

	if( cl.time > shake->time || shake->amplitude <= 0 || shake->frequency <= 0 || shake->duration <= 0 )
	{
		// reset shake
		if( shake->time != 0 )
		{
			shake->time = 0;
			shake->applied_angle = 0;
			VectorClear( shake->applied_offset );
		}

		return;
	}

	frametime = cl_clientframetime();

	if( cl.time > shake->next_shake )
	{
		// get next shake time based on frequency over duration
		shake->next_shake = (float)cl.time + shake->frequency / shake->duration;

		// randomize each shake
		for( i = 0; i < 3; i++ )
			shake->offset[i] = COM_RandomFloat( -shake->amplitude, shake->amplitude );
		shake->angle = COM_RandomFloat( -shake->amplitude * 0.25f, shake->amplitude * 0.25f );
	}

	// get initial fraction and frequency values over the duration
	fraction = ((float)cl.time - shake->time ) / shake->duration;
	freq = fraction != 0.0f ? ( shake->frequency / fraction ) * shake->frequency : 0.0f;

	// quickly approach zero but apply time over sine wave
	fraction *= fraction * sin( cl.time * freq );

	// apply shake offset
	for( i = 0; i < 3; i++ )
		shake->applied_offset[i] = shake->offset[i] * fraction;

	// apply roll angle
	shake->applied_angle = shake->angle * fraction;

	// decrease amplitude, but slower on longer shakes or higher frequency
	shake->amplitude -= shake->amplitude * ( frametime / ( shake->frequency * shake->duration ));
}

/*
=============
pfnApplyShake

=============
*/
static void GAME_EXPORT pfnApplyShake( float *origin, float *angles, float factor )
{
	if( origin )
		VectorMA( origin, factor, clgame.shake.applied_offset, origin );

	if( angles )
		angles[ROLL] += clgame.shake.applied_angle * factor;
}

/*
=============
pfnIsSpectateOnly

=============
*/
static int GAME_EXPORT pfnIsSpectateOnly( void )
{
	return (cls.spectator != 0);
}

/*
=============
pfnPointContents

=============
*/
int GAME_EXPORT PM_CL_PointContents( const float *p, int *truecontents )
{
	return PM_PointContentsPmove( clgame.pmove, p, truecontents );
}

pmtrace_t *PM_CL_TraceLine( float *start, float *end, int flags, int usehull, int ignore_pe )
{
	return PM_TraceLine( clgame.pmove, start, end, flags, usehull, ignore_pe );
}

static void GAME_EXPORT pfnPlaySoundByNameAtLocation( char *szSound, float volume, float *origin )
{
	int hSound = S_RegisterSound( szSound );
	S_StartSound( origin, cl.viewentity, CHAN_AUTO, hSound, volume, ATTN_NORM, PITCH_NORM, 0 );
}

/*
=============
pfnPrecacheEvent

=============
*/
static word GAME_EXPORT pfnPrecacheEvent( int type, const char* psz )
{
	return CL_EventIndex( psz );
}

/*
=============
pfnHookEvent

=============
*/
static void GAME_EXPORT pfnHookEvent( const char *filename, pfnEventHook pfn )
{
	char		name[64];
	cl_user_event_t	*ev;
	int		i;

	// ignore blank names
	if( !filename || !*filename )
		return;

	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	// find an empty slot
	for( i = 0; i < MAX_EVENTS; i++ )
	{
		ev = clgame.events[i];
		if( !ev ) break;

		if( !Q_stricmp( name, ev->name ) && ev->func != NULL )
		{
			Con_Reportf( S_WARN "%s: %s already hooked!\n", __func__, name );
			return;
		}
	}

	CL_RegisterEvent( i, name, pfn );
}

/*
=============
pfnKillEvent

=============
*/
static void GAME_EXPORT pfnKillEvents( int entnum, const char *eventname )
{
	int		i;
	event_state_t	*es;
	event_info_t	*ei;
	word		eventIndex = CL_EventIndex( eventname );

	if( eventIndex >= MAX_EVENTS )
		return;

	if( entnum < 0 || entnum >= clgame.maxEntities )
		return;

	es = &cl.events;

	// find all events with specified index and kill it
	for( i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		ei = &es->ei[i];

		if( ei->index == eventIndex && ei->entity_index == entnum )
		{
			CL_ResetEvent( ei );
			break;
		}
	}
}

/*
=============
pfnPlaySound

=============
*/
static void GAME_EXPORT pfnPlaySound( int ent, float *org, int chan, const char *samp, float vol, float attn, int flags, int pitch )
{
	S_StartSound( org, ent, chan, S_RegisterSound( samp ), vol, attn, pitch, flags );
}

/*
=============
CL_FindModelIndex

=============
*/
static int GAME_EXPORT CL_FindModelIndex( const char *m )
{
	char filepath[MAX_QPATH];
	int  i;

	if( COM_StringEmptyOrNULL( m ))
		return 0;

	Q_strncpy( filepath, m, sizeof( filepath ));
	COM_FixSlashes( filepath );

	for( i = 0; i < cl.nummodels; i++ )
	{
		if( !cl.models[i+1] )
			continue;

		if( !Q_stricmp( cl.models[i+1]->name, filepath ))
			return i+1;
	}

	return 0;
}

/*
=============
pfnIsLocal

=============
*/
static int GAME_EXPORT pfnIsLocal( int playernum )
{
	if( playernum == cl.playernum )
		return true;
	return false;
}

/*
=============
pfnLocalPlayerDucking

=============
*/
static int GAME_EXPORT pfnLocalPlayerDucking( void )
{
	return (cl.local.usehull == 1) ? true : false;
}

/*
=============
pfnLocalPlayerViewheight

=============
*/
static void GAME_EXPORT pfnLocalPlayerViewheight( float *view_ofs )
{
	if( view_ofs ) VectorCopy( cl.viewheight, view_ofs );
}

/*
=============
pfnLocalPlayerBounds

=============
*/
static void GAME_EXPORT pfnLocalPlayerBounds( int hull, float *mins, float *maxs )
{
	if( hull >= 0 && hull < 4 )
	{
		if( mins ) VectorCopy( host.player_mins[hull], mins );
		if( maxs ) VectorCopy( host.player_maxs[hull], maxs );
	}
}

/*
=============
pfnIndexFromTrace

=============
*/
static int GAME_EXPORT pfnIndexFromTrace( struct pmtrace_s *pTrace )
{
#if 0 // Velaron: breaks compatibility with mods that call the function after CL_PopPMStates
	if( pTrace->ent >= 0 && pTrace->ent < clgame.pmove->numphysent )
	{
		// return cl.entities number
		return clgame.pmove->physents[pTrace->ent].info;
	}
	return -1;
#endif
	return clgame.pmove->physents[pTrace->ent].info;
}

/*
=============
pfnGetPhysent

=============
*/
physent_t *pfnGetPhysent( int idx )
{
	if( idx >= 0 && idx < clgame.pmove->numphysent )
	{
		// return physent
		return &clgame.pmove->physents[idx];
	}
	return NULL;
}

/*
=============
pfnGetVisent

=============
*/
static physent_t *pfnGetVisent( int idx )
{
	if( idx >= 0 && idx < clgame.pmove->numvisent )
	{
		// return physent
		return &clgame.pmove->visents[idx];
	}
	return NULL;
}

static int GAME_EXPORT CL_TestLine( const vec3_t start, const vec3_t end, int flags )
{
	return PM_TestLineExt( clgame.pmove, clgame.pmove->physents, clgame.pmove->numphysent, start, end, flags );
}

/*
=============
CL_PushTraceBounds

=============
*/
static void GAME_EXPORT CL_PushTraceBounds( int hullnum, const float *mins, const float *maxs )
{
	if( !host.trace_bounds_pushed )
	{
		memcpy( host.player_mins_backup, host.player_mins, sizeof( host.player_mins_backup ));
		memcpy( host.player_maxs_backup, host.player_maxs, sizeof( host.player_maxs_backup ));

		host.trace_bounds_pushed = true;
	}

	hullnum = bound( 0, hullnum, 3 );
	VectorCopy( mins, host.player_mins[hullnum] );
	VectorCopy( maxs, host.player_maxs[hullnum] );
}

/*
=============
CL_PopTraceBounds

=============
*/
static void GAME_EXPORT CL_PopTraceBounds( void )
{
	if( !host.trace_bounds_pushed )
	{
		Con_Reportf( S_ERROR "%s called without push!\n", __func__ );
		return;
	}

	host.trace_bounds_pushed = false;
	memcpy( host.player_mins, host.player_mins_backup, sizeof( host.player_mins ));
	memcpy( host.player_maxs, host.player_maxs_backup, sizeof( host.player_maxs ));
}

/*
=============
pfnSetTraceHull

=============
*/
static void GAME_EXPORT CL_SetTraceHull( int hull )
{
	clgame.pmove->usehull = bound( 0, hull, 3 );
}

/*
=============
pfnPlayerTrace

=============
*/
static void GAME_EXPORT CL_PlayerTrace( float *start, float *end, int traceFlags, int ignore_pe, pmtrace_t *tr )
{
	if( !tr ) return;
	*tr = PM_PlayerTraceExt( clgame.pmove, start, end, traceFlags, clgame.pmove->numphysent, clgame.pmove->physents, ignore_pe, NULL );
}

/*
=============
pfnPlayerTraceExt

=============
*/
static void GAME_EXPORT CL_PlayerTraceExt( float *start, float *end, int traceFlags, int (*pfnIgnore)( physent_t *pe ), pmtrace_t *tr )
{
	if( !tr ) return;
	*tr = PM_PlayerTraceExt( clgame.pmove, start, end, traceFlags, clgame.pmove->numphysent, clgame.pmove->physents, -1, pfnIgnore );
}

/*
=============
CL_TraceTexture

=============
*/
const char * GAME_EXPORT PM_CL_TraceTexture( int ground, float *vstart, float *vend )
{
	return PM_TraceTexture( clgame.pmove, ground, vstart, vend );
}

/*
=============
pfnTraceSurface

=============
*/
struct msurface_s *pfnTraceSurface( int ground, float *vstart, float *vend )
{
	return PM_TraceSurfacePmove( clgame.pmove, ground, vstart, vend );
}

/*
=============
pfnGetMovevars

=============
*/
static movevars_t *pfnGetMoveVars( void )
{
	return &clgame.movevars;
}

/*
=============
pfnStopAllSounds

=============
*/
static void GAME_EXPORT pfnStopAllSounds( int ent, int entchannel )
{
	S_StopSound( ent, entchannel, NULL );
}

/*
=============
CL_LoadModel

=============
*/
model_t *CL_LoadModel( const char *modelname, int *index )
{
	int	i;

	if( index ) *index = -1;

	if(( i = CL_FindModelIndex( modelname )) == 0 )
		return NULL;

	if( index ) *index = i;

	return CL_ModelHandle( i );
}

static int GAME_EXPORT CL_AddEntity( int entityType, cl_entity_t *pEnt )
{
	if( !pEnt ) return false;

	// clear effects for all temp entities
	if( !pEnt->index ) pEnt->curstate.effects = 0;

	// let the render reject entity without model
	return CL_AddVisibleEntity( pEnt, entityType );
}

/*
=============
pfnGetGameDirectory

=============
*/
static const char *pfnGetGameDirectory( void )
{
	static char	szGetGameDir[MAX_SYSPATH];

	Q_strncpy( szGetGameDir, GI->gamefolder, sizeof( szGetGameDir ));
	return szGetGameDir;
}

/*
=============
pfnGetLevelName

=============
*/
static const char *pfnGetLevelName( void )
{
	static char	mapname[64];

	// a1ba: don't return maps/.bsp if no map is loaded yet
	// in GoldSrc this is handled by cl.levelname field but we don't have it
	// so emulate this behavior here
	if( cls.state >= ca_connected && !COM_StringEmpty( clgame.mapname ))
		Q_snprintf( mapname, sizeof( mapname ), "maps/%s.bsp", clgame.mapname );
	else mapname[0] = '\0'; // not in game

	return mapname;
}

/*
=============
pfnGetScreenFade

=============
*/
static void GAME_EXPORT pfnGetScreenFade( struct screenfade_s *fade )
{
	if( fade ) *fade = clgame.fade;
}

/*
=============
pfnSetScreenFade

=============
*/
static void GAME_EXPORT pfnSetScreenFade( struct screenfade_s *fade )
{
	if( fade ) clgame.fade = *fade;
}

/*
=============
pfnLoadMapSprite

=============
*/
static model_t *pfnLoadMapSprite( const char *filename )
{
	model_t *mod;

	mod = Mod_FindName( filename, false );

	if( CL_LoadHudSprite( filename, mod, SPR_MAPSPRITE, 0 ))
		return mod;

	return NULL;
}

/*
=============
COM_AddAppDirectoryToSearchPath

=============
*/
static void GAME_EXPORT COM_AddAppDirectoryToSearchPath( const char *pszBaseDir, const char *appName )
{
	FS_AddGameHierarchy( pszBaseDir, FS_NOWRITE_PATH );
}


/*
===========
COM_ExpandFilename

Finds the file in the search path, copies over the name with the full path name.
This doesn't search in the pak file.
===========
*/
static int GAME_EXPORT COM_ExpandFilename( const char *fileName, char *nameOutBuffer, int nameOutBufferSize )
{
	char		result[MAX_SYSPATH];

	if( COM_StringEmptyOrNULL( fileName ) || !nameOutBuffer || nameOutBufferSize <= 0 )
		return 0;

	// filename examples:
	// media\sierra.avi - D:\Xash3D\valve\media\sierra.avi
	// models\barney.mdl - D:\Xash3D\bshift\models\barney.mdl
	if( g_fsapi.GetFullDiskPath( result, sizeof( result ), fileName, false ))
	{
		// check for enough room
		if( Q_strlen( result ) > nameOutBufferSize )
			return 0;

		Q_strncpy( nameOutBuffer, result, nameOutBufferSize );
		return 1;
	}
	return 0;
}

/*
=============
PlayerInfo_ValueForKey

=============
*/
static const char *PlayerInfo_ValueForKey( int playerNum, const char *key )
{
	// find the player
	if(( playerNum > cl.maxclients ) || ( playerNum < 1 ))
		return NULL;

	if( !cl.players[playerNum-1].name[0] )
		return NULL;

	return Info_ValueForKey( cl.players[playerNum-1].userinfo, key );
}

/*
=============
PlayerInfo_SetValueForKey

=============
*/
static void GAME_EXPORT PlayerInfo_SetValueForKey( const char *key, const char *value )
{
	convar_t	*var;

	if( !Q_strcmp( Info_ValueForKey( cls.userinfo, key ), value ))
		return; // no changes ?

	var = Cvar_FindVar( key );

	if( var && FBitSet( var->flags, FCVAR_USERINFO ))
	{
		Cvar_DirectSet( var, value );
	}
	else if( Info_SetValueForStarKey( cls.userinfo, key, value, sizeof( cls.userinfo )))
	{
		// time to update server copy of userinfo
		CL_UpdateInfo( key, value );
	}
}

/*
=============
pfnGetPlayerUniqueID

=============
*/
static qboolean GAME_EXPORT pfnGetPlayerUniqueID( int iPlayer, char playerID[16] )
{
	if( iPlayer < 1 || iPlayer > cl.maxclients )
		return false;

	// make sure there is a player here..
	if( !cl.players[iPlayer-1].userinfo[0] || !cl.players[iPlayer-1].name[0] )
		return false;

	memcpy( playerID, cl.players[iPlayer-1].hashedcdkey, 16 );
	return true;
}

/*
=============
pfnGetTrackerIDForPlayer

obsolete, unused
=============
*/
static int GAME_EXPORT pfnGetTrackerIDForPlayer( int playerSlot )
{
	return 0;
}

/*
=============
pfnGetPlayerForTrackerID

obsolete, unused
=============
*/
static int GAME_EXPORT pfnGetPlayerForTrackerID( int trackerID )
{
	return 0;
}

/*
=============
pfnServerCmdUnreliable

=============
*/
static int GAME_EXPORT pfnServerCmdUnreliable( char *szCmdString )
{
	if( COM_StringEmptyOrNULL( szCmdString ))
		return 0;

	MSG_BeginClientCmd( &cls.datagram, clc_stringcmd );
	MSG_WriteString( &cls.datagram, szCmdString );

	return 1;
}

/*
=============
pfnGetMousePos

=============
*/
static void GAME_EXPORT pfnGetMousePos( struct tagPOINT *ppt )
{
	int x, y;

	if( !ppt )
		return;

	Platform_GetMousePos( &x, &y );

	ppt->x = x;
	ppt->y = y;
}

/*
=============
pfnSetMouseEnable

legacy of dinput code
=============
*/
static void GAME_EXPORT pfnSetMouseEnable( qboolean fEnable )
{
}


/*
=============
pfnGetServerTime

=============
*/
static float GAME_EXPORT pfnGetClientOldTime( void )
{
	return cl.oldtime;
}

/*
=============
pfnGetGravity

=============
*/
static float GAME_EXPORT pfnGetGravity( void )
{
	return clgame.movevars.gravity;
}

/*
=============
pfnEnableTexSort

TODO: implement
=============
*/
static void GAME_EXPORT pfnEnableTexSort( int enable )
{
}

/*
=============
pfnSetLightmapColor

TODO: implement
=============
*/
static void GAME_EXPORT pfnSetLightmapColor( float red, float green, float blue )
{
}

/*
=============
pfnSetLightmapScale

TODO: implement
=============
*/
static void GAME_EXPORT pfnSetLightmapScale( float scale )
{
}

/*
=============
pfnSPR_DrawGeneric

=============
*/
static void GAME_EXPORT pfnSPR_DrawGeneric( int frame, int x, int y, const wrect_t *prc, int blendsrc, int blenddst, int width, int height )
{
#if 0 // REFTODO:
	pglEnable( GL_BLEND );
	pglBlendFunc( blendsrc, blenddst ); // g-cont. are params is valid?
#endif
	SPR_DrawGeneric( frame, x, y, width, height, prc );
}

/*
=============
LocalPlayerInfo_ValueForKey

=============
*/
static const char *GAME_EXPORT LocalPlayerInfo_ValueForKey( const char* key )
{
	return Info_ValueForKey( cls.userinfo, key );
}

/*
=============
pfnVGUI2DrawCharacter

=============
*/
static int GAME_EXPORT pfnVGUI2DrawCharacter( int x, int y, int number, unsigned int font )
{
	return pfnDrawCharacter( x, y, number, 255, 255, 255 );
}

/*
=============
pfnVGUI2DrawCharacterAdditive

=============
*/
static int GAME_EXPORT pfnVGUI2DrawCharacterAdditive( int x, int y, int ch, int r, int g, int b, unsigned int font )
{
	return pfnDrawCharacter( x, y, ch, r, g, b );
}

/*
=============
pfnDrawString

=============
*/
static int GAME_EXPORT pfnDrawString( int x, int y, const char *str, int r, int g, int b )
{
	rgba_t color = { r, g, b, 255 };
	int flags = FONT_DRAW_HUD | FONT_DRAW_NOLF;

	if( hud_utf8.value )
		SetBits( flags, FONT_DRAW_UTF8 );

	return CL_DrawString( x, y, str, color, &cls.creditsFont, flags );
}

/*
=============
pfnDrawStringReverse

=============
*/
static int GAME_EXPORT pfnDrawStringReverse( int x, int y, const char *str, int r, int g, int b )
{
	rgba_t color = { r, g, b, 255 };
	int flags = FONT_DRAW_HUD | FONT_DRAW_NOLF;
	int width;

	if( hud_utf8.value )
		SetBits( flags, FONT_DRAW_UTF8 );

	CL_DrawStringLen( &cls.creditsFont, str, &width, NULL, flags );

	x -= width;

	return CL_DrawString( x, y, str, color, &cls.creditsFont, flags );
}

/*
=============
GetCareerGameInterface

=============
*/
static void *GAME_EXPORT GetCareerGameInterface( void )
{
	Msg( "^1Career GameInterface called!\n" );
	return NULL;
}

/*
=============
pfnPlaySoundVoiceByName

=============
*/
static void GAME_EXPORT pfnPlaySoundVoiceByName( char *filename, float volume, int pitch )
{
	int hSound = S_RegisterSound( filename );

	S_StartSound( NULL, cl.viewentity, CHAN_NETWORKVOICE_END + 1, hSound, volume, 1.0, pitch, SND_STOP_LOOPING );
}

/*
=============
pfnMP3_InitStream

=============
*/
static void GAME_EXPORT pfnMP3_InitStream( char *filename, int looping )
{
	if( !filename )
	{
		S_StopBackgroundTrack();
		return;
	}

	if( looping )
	{
		S_StartBackgroundTrack( filename, filename, 0, false );
	}
	else
	{
		S_StartBackgroundTrack( filename, NULL, 0, false );
	}
}

/*
=============
pfnPlaySoundByNameAtPitch

=============
*/
static void GAME_EXPORT pfnPlaySoundByNameAtPitch( char *filename, float volume, int pitch )
{
	int hSound = S_RegisterSound( filename );
	S_StartSound( NULL, cl.viewentity, CHAN_ITEM, hSound, volume, 1.0, pitch, SND_STOP_LOOPING );
}

/*
=============
pfnFillRGBABlend

=============
*/
static void GAME_EXPORT CL_FillRGBABlend( int x, int y, int w, int h, int r, int g, int b, int a )
{
	float x_ = x, y_ = y, w_ = w, h_ = h;

	r = bound( 0, r, 255 );
	g = bound( 0, g, 255 );
	b = bound( 0, b, 255 );
	a = bound( 0, a, 255 );

	SPR_AdjustSize( &x_, &y_, &w_, &h_ );

	ref.dllFuncs.FillRGBA( kRenderTransTexture, x_, y_, w_, h_, r, g, b, a );
}

/*
=============
pfnGetAppID

=============
*/
static int GAME_EXPORT pfnGetAppID( void )
{
	return 70; // Half-Life AppID
}

/*
=============
pfnVguiWrap2_GetMouseDelta

TODO: implement
=============
*/
static void GAME_EXPORT pfnVguiWrap2_GetMouseDelta( int *x, int *y )
{
}

/*
=============
pfnParseFile

handle colon separately
=============
*/
static char *pfnParseFile( char *data, char *token )
{
	return COM_ParseFileSafe( data, token, PFILE_TOKEN_MAX_LENGTH, PFILE_HANDLECOLON, NULL, NULL );
}

/*
=================
TriAPI implementation

=================
*/
/*
=================
TriRenderMode
=================
*/
void TriRenderMode( int mode )
{
	clgame.ds.renderMode = mode;
	ref.dllFuncs.TriRenderMode( mode );
}

/*
=================
TriColor4f
=================
*/
void TriColor4f( float r, float g, float b, float a )
{
	if( clgame.ds.renderMode == kRenderTransAlpha )
		ref.dllFuncs.Color4ub( r * 255.9f, g * 255.9f, b * 255.9f, a * 255.0f );
	else ref.dllFuncs.Color4f( r * a, g * a, b * a, 1.0 );

	clgame.ds.triRGBA[0] = r;
	clgame.ds.triRGBA[1] = g;
	clgame.ds.triRGBA[2] = b;
	clgame.ds.triRGBA[3] = a;
}

/*
=============
TriColor4ub
=============
*/
void TriColor4ub( byte r, byte g, byte b, byte a )
{
	clgame.ds.triRGBA[0] = r * (1.0f / 255.0f);
	clgame.ds.triRGBA[1] = g * (1.0f / 255.0f);
	clgame.ds.triRGBA[2] = b * (1.0f / 255.0f);
	clgame.ds.triRGBA[3] = a * (1.0f / 255.0f);

	ref.dllFuncs.Color4f( clgame.ds.triRGBA[0], clgame.ds.triRGBA[1], clgame.ds.triRGBA[2], 1.0f );
}

/*
=============
TriBrightness
=============
*/
void TriBrightness( float brightness )
{
	float	r, g, b;

	r = clgame.ds.triRGBA[0] * clgame.ds.triRGBA[3] * brightness;
	g = clgame.ds.triRGBA[1] * clgame.ds.triRGBA[3] * brightness;
	b = clgame.ds.triRGBA[2] * clgame.ds.triRGBA[3] * brightness;

	ref.dllFuncs.Color4f( r, g, b, 1.0f );
}

/*
=============
TriCullFace
=============
*/
void TriCullFace( TRICULLSTYLE style )
{
	clgame.ds.cullMode = style;
	ref.dllFuncs.CullFace( style );
}

/*
=============
TriWorldToScreen
convert world coordinates (x,y,z) into screen (x, y)
=============
*/
int TriWorldToScreen( const float *world, float *screen )
{
	return ref.dllFuncs.WorldToScreen( world, screen );
}

/*
=============
TriBoxInPVS

check box in pvs (absmin, absmax)
=============
*/
int TriBoxInPVS( float *mins, float *maxs )
{
	return Mod_BoxVisible( mins, maxs, ref.dllFuncs.Mod_GetCurrentVis( ));
}

/*
=============
TriLightAtPoint
NOTE: dlights are ignored
=============
*/
void TriLightAtPoint( float *pos, float *value )
{
	colorVec	vLightColor;

	if( !pos || !value ) return;

	vLightColor = ref.dllFuncs.R_LightPoint( pos );

	value[0] = vLightColor.r;
	value[1] = vLightColor.g;
	value[2] = vLightColor.b;
}

/*
=============
TriColor4fRendermode
Heavy legacy of Quake...
=============
*/
void TriColor4fRendermode( float r, float g, float b, float a, int rendermode )
{
	if( rendermode == kRenderTransAlpha )
	{
		clgame.ds.triRGBA[3] = a / 255.0f;
		ref.dllFuncs.Color4f( r, g, b, a );
	}
	else ref.dllFuncs.Color4f( r * a, g * a, b * a, 1.0f );
}


/*
=============
TriSpriteTexture

bind current texture
=============
*/
int TriSpriteTexture( model_t *pSpriteModel, int frame )
{
	int	gl_texturenum;

	if(( gl_texturenum = ref.dllFuncs.R_GetSpriteTexture( pSpriteModel, frame )) <= 0 )
		return 0;

	ref.dllFuncs.GL_Bind( XASH_TEXTURE0, gl_texturenum );

	return 1;
}

/*
=================
DemoApi implementation

=================
*/
/*
=================
Demo_IsTimeDemo

=================
*/
static int GAME_EXPORT Demo_IsTimeDemo( void )
{
	return cls.timedemo;
}

/*
=================
NetworkApi implementation

=================
*/
/*
=================
NetAPI_InitNetworking

=================
*/
static void GAME_EXPORT NetAPI_InitNetworking( void )
{
	NET_Config( true, false ); // allow remote
}

/*
=================
NetAPI_InitNetworking

=================
*/
static void GAME_EXPORT NetAPI_Status( net_status_t *status )
{
	qboolean	connected = false;
	int	packet_loss = 0;

	Assert( status != NULL );

	if( cls.state > ca_disconnected && cls.state != ca_cinematic )
		connected = true;

	if( cls.state == ca_active )
		packet_loss = bound( 0, (int)cls.packet_loss, 100 );

	status->connected = connected;
	status->connection_time = (connected) ? (host.realtime - cls.netchan.connect_time) : 0.0;
	status->latency = (connected) ? cl.frames[cl.parsecountmod].latency : 0.0;
	status->remote_address = cls.netchan.remote_address;
	status->packet_loss = packet_loss;
	NET_GetLocalAddress( &status->local_address, NULL ); // NetAPI doesn't know about IPv6
	status->rate = rate.value;
}

/*
=================
NetAPI_SendRequest

=================
*/
static void GAME_EXPORT NetAPI_SendRequest( int context, int request, int flags, double timeout, netadr_t *remote_address, net_api_response_func_t response )
{
	net_request_t	*nr = NULL;
	int		i;

	if( !response )
	{
		Con_DPrintf( S_ERROR "%s: no callbcak specified for request with context %i!\n", __func__, context );
		return;
	}

	if( NET_NetadrType( remote_address ) == NA_IPX || NET_NetadrType( remote_address ) == NA_BROADCAST_IPX )
		return; // IPX no longer support

	if( request == NETAPI_REQUEST_SERVERLIST )
		return; // no support for server list requests

	// find a free request
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		nr = &clgame.net_requests[i];
		if( !nr->pfnFunc ) break;
	}

	if( i == MAX_REQUESTS )
	{
		double	max_timeout = 0;

		// no free requests? use oldest
		for( i = 0, nr = NULL; i < MAX_REQUESTS; i++ )
		{
			if(( host.realtime - clgame.net_requests[i].timesend ) > max_timeout )
			{
				max_timeout = host.realtime - clgame.net_requests[i].timesend;
				nr = &clgame.net_requests[i];
			}
		}
	}

	Assert( nr != NULL );

	// clear slot
	memset( nr, 0, sizeof( *nr ));

	// create a new request
	nr->timesend = host.realtime;
	nr->timeout = nr->timesend + timeout;
	nr->pfnFunc = response;
	nr->resp.context = context;
	nr->resp.type = request;
	nr->resp.remote_address = *remote_address;
	nr->flags = flags;

	// local servers request
	Netchan_OutOfBandPrint( NS_CLIENT, nr->resp.remote_address, A2A_NETINFO" %i %i %i", PROTOCOL_VERSION, context, request );
}

/*
=================
NetAPI_CancelRequest

=================
*/
static void GAME_EXPORT NetAPI_CancelRequest( int context )
{
	net_request_t	*nr;
	int		i;
;
	// find a specified request
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		nr = &clgame.net_requests[i];

		if( clgame.net_requests[i].resp.context == context )
		{
			if( nr->pfnFunc )
			{
				SetBits( nr->resp.error, NET_ERROR_TIMEOUT );
				nr->resp.ping = host.realtime - nr->timesend;
				nr->pfnFunc( &nr->resp );
			}

			memset( &clgame.net_requests[i], 0, sizeof( net_request_t ));
			break;
		}
	}
}

/*
=================
NetAPI_CancelAllRequests

=================
*/
void GAME_EXPORT NetAPI_CancelAllRequests( void )
{
	net_request_t	*nr;
	int		i;

	// tell the user about cancel
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		nr = &clgame.net_requests[i];
		if( !nr->pfnFunc ) continue;	// not used
		SetBits( nr->resp.error, NET_ERROR_TIMEOUT );
		nr->resp.ping = host.realtime - nr->timesend;
		nr->pfnFunc( &nr->resp );
	}

	memset( clgame.net_requests, 0, sizeof( clgame.net_requests ));
}

/*
=================
NetAPI_AdrToString

=================
*/
static const char *NetAPI_AdrToString( netadr_t *a )
{
	return NET_AdrToString( *a );
}

/*
=================
NetAPI_CompareAdr

=================
*/
static int GAME_EXPORT NetAPI_CompareAdr( netadr_t *a, netadr_t *b )
{
	return NET_CompareAdr( *a, *b );
}

/*
=================
NetAPI_RemoveKey

=================
*/
static void GAME_EXPORT NetAPI_RemoveKey( char *s, const char *key )
{
	Info_RemoveKey( s, key );
}

/*
=================
NetAPI_SetValueForKey

=================
*/
static void GAME_EXPORT NetAPI_SetValueForKey( char *s, const char *key, const char *value, int maxsize )
{
	if( key[0] == '*' ) return;
	Info_SetValueForStarKey( s, key, value, maxsize );
}


/*
=================
IVoiceTweak implementation

TODO: implement
=================
*/
/*
=================
Voice_StartVoiceTweakMode

=================
*/
static int GAME_EXPORT Voice_StartVoiceTweakMode( void )
{
	return 0;
}

/*
=================
Voice_EndVoiceTweakMode

=================
*/
static void GAME_EXPORT Voice_EndVoiceTweakMode( void )
{
}

/*
=================
Voice_SetControlFloat

=================
*/
static void GAME_EXPORT Voice_SetControlFloat( VoiceTweakControl iControl, float value )
{
}

/*
=================
Voice_GetControlFloat

=================
*/
static float GAME_EXPORT Voice_GetControlFloat( VoiceTweakControl iControl )
{
	return 1.0f;
}

static void GAME_EXPORT VGui_ViewportPaintBackground( int extents[4] )
{
	// stub
}

// shared between client and server
triangleapi_t gTriApi;

static efx_api_t gEfxApi =
{
	R_AllocParticle,
	R_BlobExplosion,
	R_Blood,
	R_BloodSprite,
	R_BloodStream,
	R_BreakModel,
	R_Bubbles,
	R_BubbleTrail,
	R_BulletImpactParticles,
	R_EntityParticles,
	R_Explosion,
	R_FizzEffect,
	R_FireField,
	R_FlickerParticles,
	R_FunnelSprite,
	R_Implosion,
	R_LargeFunnel,
	R_LavaSplash,
	R_MultiGunshot,
	R_MuzzleFlash,
	R_ParticleBox,
	R_ParticleBurst,
	R_ParticleExplosion,
	R_ParticleExplosion2,
	R_ParticleLine,
	R_PlayerSprites,
	R_Projectile,
	R_RicochetSound,
	R_RicochetSprite,
	R_RocketFlare,
	R_RocketTrail,
	R_RunParticleEffect,
	R_ShowLine,
	R_SparkEffect,
	R_SparkShower,
	R_SparkStreaks,
	R_Spray,
	R_Sprite_Explode,
	R_Sprite_Smoke,
	R_Sprite_Spray,
	R_Sprite_Trail,
	R_Sprite_WallPuff,
	R_StreakSplash,
	R_TracerEffect,
	R_UserTracerParticle,
	R_TracerParticles,
	R_TeleportSplash,
	R_TempSphereModel,
	R_TempModel,
	R_DefaultSprite,
	R_TempSprite,
	CL_DecalIndex,
	CL_DecalIndexFromName,
	CL_DecalShoot,
	R_AttachTentToPlayer,
	R_KillAttachedTents,
	R_BeamCirclePoints,
	R_BeamEntPoint,
	R_BeamEnts,
	R_BeamFollow,
	R_BeamKill,
	R_BeamLightning,
	R_BeamPoints,
	R_BeamRing,
	CL_AllocDlight,
	CL_AllocElight,
	CL_TempEntAlloc,
	CL_TempEntAllocNoModel,
	CL_TempEntAllocHigh,
	CL_TempEntAllocCustom,
	R_GetPackedColor,
	R_LookupColor,
	CL_DecalRemoveAll,
	CL_FireCustomDecal,
};

static event_api_t gEventApi =
{
	EVENT_API_VERSION,
	pfnPlaySound,
	S_StopSound,
	CL_FindModelIndex,
	pfnIsLocal,
	pfnLocalPlayerDucking,
	pfnLocalPlayerViewheight,
	pfnLocalPlayerBounds,
	pfnIndexFromTrace,
	pfnGetPhysent,
	CL_SetUpPlayerPrediction,
	CL_PushPMStates,
	CL_PopPMStates,
	CL_SetSolidPlayers,
	CL_SetTraceHull,
	CL_PlayerTrace,
	CL_WeaponAnim,
	pfnPrecacheEvent,
	CL_PlaybackEvent,
	PM_CL_TraceTexture,
	pfnStopAllSounds,
	pfnKillEvents,
	CL_PlayerTraceExt,		// Xash3D added
	CL_SoundFromIndex,
	pfnTraceSurface,
	pfnGetMoveVars,
	CL_VisTraceLine,
	pfnGetVisent,
	CL_TestLine,
	CL_PushTraceBounds,
	CL_PopTraceBounds,
};

static demo_api_t gDemoApi =
{
	(void *)CL_IsRecordDemo,
	(void *)CL_IsPlaybackDemo,
	Demo_IsTimeDemo,
	CL_WriteDemoUserMessage,
};

net_api_t gNetApi =
{
	NetAPI_InitNetworking,
	NetAPI_Status,
	NetAPI_SendRequest,
	NetAPI_CancelRequest,
	NetAPI_CancelAllRequests,
	NetAPI_AdrToString,
	NetAPI_CompareAdr,
	(void *)NET_StringToAdr,
	Info_ValueForKey,
	NetAPI_RemoveKey,
	NetAPI_SetValueForKey,
};

static IVoiceTweak gVoiceApi =
{
	Voice_StartVoiceTweakMode,
	Voice_EndVoiceTweakMode,
	Voice_SetControlFloat,
	Voice_GetControlFloat,
};

// engine callbacks
static cl_enginefunc_t gEngfuncs =
{
	pfnSPR_Load,
	pfnSPR_Frames,
	pfnSPR_Height,
	pfnSPR_Width,
	pfnSPR_Set,
	pfnSPR_Draw,
	pfnSPR_DrawHoles,
	pfnSPR_DrawAdditive,
	SPR_EnableScissor,
	SPR_DisableScissor,
	SPR_GetList,
	CL_FillRGBA,
	CL_GetScreenInfo,
	pfnSetCrosshair,
	pfnCvar_RegisterClientVariable,
	Cvar_VariableValue,
	Cvar_VariableString,
	Cmd_AddClientCommand,
	pfnHookUserMsg,
	pfnServerCmd,
	pfnClientCmd,
	pfnGetPlayerInfo,
	pfnPlaySoundByName,
	pfnPlaySoundByIndex,
	AngleVectors,
	CL_TextMessageGet,
	pfnDrawCharacter,
	pfnDrawConsoleString,
	pfnDrawSetTextColor,
	pfnDrawConsoleStringLen,
	pfnConsolePrint,
	pfnCenterPrint,
	pfnGetWindowCenterX,
	pfnGetWindowCenterY,
	pfnGetViewAngles,
	pfnSetViewAngles,
	CL_GetMaxClients,
	Cvar_SetValue,
	Cmd_Argc,
	Cmd_Argv,
	Con_Printf,
	Con_DPrintf,
	Con_NPrintf,
	Con_NXPrintf,
	pfnPhysInfo_ValueForKey,
	pfnServerInfo_ValueForKey,
	pfnGetClientMaxspeed,
	COM_CheckParm,
	Key_Event,
	Platform_GetMousePos,
	pfnIsNoClipping,
	CL_GetLocalPlayer,
	CL_GetViewModel,
	CL_GetEntityByIndex,
	pfnGetClientTime,
	pfnCalcShake,
	pfnApplyShake,
	PM_CL_PointContents,
	CL_WaterEntity,
	PM_CL_TraceLine,
	CL_LoadModel,
	CL_AddEntity,
	CL_GetSpritePointer,
	pfnPlaySoundByNameAtLocation,
	pfnPrecacheEvent,
	CL_PlaybackEvent,
	CL_WeaponAnim,
	COM_RandomFloat,
	COM_RandomLong,
	pfnHookEvent,
	Con_Visible,
	pfnGetGameDirectory,
	pfnCVarGetPointer,
	Key_LookupBinding,
	pfnGetLevelName,
	pfnGetScreenFade,
	pfnSetScreenFade,
	VGui_GetPanel,
	VGui_ViewportPaintBackground,
	COM_LoadFile,
	pfnParseFile,
	COM_FreeFile,
	&gTriApi,
	&gEfxApi,
	&gEventApi,
	&gDemoApi,
	&gNetApi,
	&gVoiceApi,
	pfnIsSpectateOnly,
	pfnLoadMapSprite,
	COM_AddAppDirectoryToSearchPath,
	COM_ExpandFilename,
	PlayerInfo_ValueForKey,
	PlayerInfo_SetValueForKey,
	pfnGetPlayerUniqueID,
	pfnGetTrackerIDForPlayer,
	pfnGetPlayerForTrackerID,
	pfnServerCmdUnreliable,
	pfnGetMousePos,
	Platform_SetMousePos,
	pfnSetMouseEnable,
	Cvar_GetList,
	(void*)Cmd_GetFirstFunctionHandle,
	(void*)Cmd_GetNextFunctionHandle,
	(void*)Cmd_GetName,
	pfnGetClientOldTime,
	pfnGetGravity,
	CL_ModelHandle,
	pfnEnableTexSort,
	pfnSetLightmapColor,
	pfnSetLightmapScale,
	pfnSequenceGet,
	pfnSPR_DrawGeneric,
	pfnSequencePickSentence,
	pfnDrawString,
	pfnDrawStringReverse,
	LocalPlayerInfo_ValueForKey,
	pfnVGUI2DrawCharacter,
	pfnVGUI2DrawCharacterAdditive,
	Sound_GetApproxWavePlayLen,
	GetCareerGameInterface,
	Cvar_Set,
	pfnIsCareerMatch,
	pfnPlaySoundVoiceByName,
	pfnMP3_InitStream,
	Sys_DoubleTime,
	pfnProcessTutorMessageDecayBuffer,
	pfnConstructTutorMessageDecayBuffer,
	pfnResetTutorMessageDecayData,
	pfnPlaySoundByNameAtPitch,
	CL_FillRGBABlend,
	pfnGetAppID,
	Cmd_AliasGetList,
	pfnVguiWrap2_GetMouseDelta,
	pfnFilteredClientCmd
};

void CL_UnloadProgs( void )
{
	if( !clgame.hInstance ) return;

	CL_FreeEdicts();
	CL_FreeTempEnts();
	CL_FreeViewBeams();
	CL_FreeParticles();
	CL_ClearAllRemaps();
	Mod_ClearUserData();

	// NOTE: HLFX 0.5 has strange bug: hanging on exit if no map was loaded
	if( Q_stricmp( GI->gamefolder, "hlfx" ) || GI->version != 0.5f )
		clgame.dllFuncs.pfnShutdown();

	if( GI->internal_vgui_support )
		VGui_Shutdown();

	Cvar_DirectFullSet( &cl_background, "0", FCVAR_READ_ONLY );
	Cvar_FullSet( "host_clientloaded", "0", FCVAR_READ_ONLY );

	Cvar_Unlink( FCVAR_CLIENTDLL );
	Cmd_Unlink( CMD_CLIENTDLL );

	COM_FreeLibrary( clgame.hInstance );
	Mem_FreePool( &cls.mempool );
	Mem_FreePool( &clgame.mempool );
	memset( &clgame, 0, sizeof( clgame ));
}

qboolean CL_LoadProgs( const char *name )
{
	static playermove_t		gpMove;
	CL_EXPORT_FUNCS	GetClientAPI; // single export
	qboolean valid_single_export = false;
	qboolean missed_exports = false;
	qboolean try_internal_vgui_support = GI->internal_vgui_support;
	int i;

	if( clgame.hInstance ) CL_UnloadProgs();

	// initialize PlayerMove
	clgame.pmove = &gpMove;

	cls.mempool = Mem_AllocPool( "Client Static Pool" );
	clgame.mempool = Mem_AllocPool( "Client Edicts Zone" );
	clgame.entities = NULL;

	// a1ba: we need to check if client.dll has direct dependency on SDL2
	// and if so, disable relative mouse mode
#if XASH_WIN32 && !XASH_64BIT
	clgame.client_dll_uses_sdl = COM_CheckLibraryDirectDependency( name, OS_LIB_PREFIX "SDL2." OS_LIB_EXT, false );
	Con_Printf( S_NOTE "%s uses %s for mouse input\n", name, clgame.client_dll_uses_sdl ? "SDL2" : "Windows API" );
#endif

	// NOTE: important stuff!
	// vgui must startup BEFORE loading client.dll to avoid get error ERROR_NOACESS during LoadLibrary
	if( !try_internal_vgui_support && VGui_LoadProgs( NULL ))
		VGui_Startup( refState.width, refState.height );
	else
		try_internal_vgui_support = true; // we failed to load vgui_support, but let's probe client.dll for support anyway

	clgame.hInstance = COM_LoadLibrary( name, false, false );

	if( !clgame.hInstance )
		return false;

	// delayed vgui initialization for internal support
	if( try_internal_vgui_support && VGui_LoadProgs( clgame.hInstance ))
		VGui_Startup( refState.width, refState.height );

	// clear exports
	ClearExports( cdll_exports, ARRAYSIZE( cdll_exports ));

	// trying to get single export
	if(( GetClientAPI = COM_GetProcAddress( clgame.hInstance, "GetClientAPI" )) != NULL )
	{
		Con_Reportf( "%s: found single callback export\n", __func__ );

		// trying to fill interface now
		GetClientAPI( &clgame.dllFuncs );
	}
	else if(( GetClientAPI = COM_GetProcAddress( clgame.hInstance, "F" )) != NULL )
	{
		Con_Reportf( "%s: found single callback export (secured client dlls)\n", __func__ );

		// trying to fill interface now
		CL_GetSecuredClientAPI( GetClientAPI );
	}

	if( GetClientAPI != NULL ) // check critical functions again
		valid_single_export = ValidateExports( cdll_exports, ARRAYSIZE( cdll_exports ));

	for( i = 0; i < ARRAYSIZE( cdll_exports ); i++ )
	{
		if( *(cdll_exports[i].func) != NULL )
			continue; // already got through 'F' or 'GetClientAPI'

		// functions are cleared before all the extensions are evaluated
		if(( *(cdll_exports[i].func) = (void *)COM_GetProcAddress( clgame.hInstance, cdll_exports[i].name )) == NULL )
		{
			Con_Reportf( S_ERROR "%s: failed to get address of %s proc\n", __func__, cdll_exports[i].name );

			// print all not found exports at once, for debug
			missed_exports = true;
		}
	}

	if( missed_exports )
	{
		if( clgame.dllFuncs.pfnInit && clgame.dllFuncs.pfnRedraw && clgame.dllFuncs.pfnReset && clgame.dllFuncs.pfnUpdateClientData && clgame.dllFuncs.pfnVidInit && clgame.dllFuncs.pfnInitialize )
			COM_PushLibraryError( "missing essential exports; outdated DLL!!!" );
		else
			COM_PushLibraryError( "missing essential exports" );

		COM_FreeLibrary( clgame.hInstance );
		clgame.hInstance = NULL;
		return false;
	}

	// it may be loaded through 'GetClientAPI' so we don't need to clear them
	if( !valid_single_export )
		ClearExports( cdll_new_exports, ARRAYSIZE( cdll_new_exports ));

	for( i = 0; i < ARRAYSIZE( cdll_new_exports ); i++ )
	{
		if( *(cdll_new_exports[i].func) != NULL )
			continue; // already gott through 'F' or 'GetClientAPI'

		// functions are cleared before all the extensions are evaluated
		// NOTE: new exports can be missed without stop the engine
		if(( *(cdll_new_exports[i].func) = (void *)COM_GetProcAddress( clgame.hInstance, cdll_new_exports[i].name )) == NULL )
			Con_Reportf( S_WARN "%s: failed to get address of %s proc\n", __func__, cdll_new_exports[i].name );
	}

	if( !clgame.dllFuncs.pfnInitialize( &gEngfuncs, CLDLL_INTERFACE_VERSION ))
	{
		COM_PushLibraryError( "can't init client API" );
		COM_FreeLibrary( clgame.hInstance );
		Con_Reportf( "%s: can't init client API\n", __func__ );
		clgame.hInstance = NULL;
		return false;
	}

	Cvar_FullSet( "host_clientloaded", "1", FCVAR_READ_ONLY );

	clgame.maxRemapInfos = 0; // will be alloc on first call CL_InitEdicts();
	clgame.maxEntities = 2; // world + localclient (have valid entities not in game)

	CL_InitCDAudio( "media/cdaudio.txt" );
	CL_InitTitles( "titles.txt" );
	CL_InitParticles ();
	CL_InitViewBeams ();
	CL_InitTempEnts ();

	if( !R_InitRenderAPI())	// Xash3D extension
		Con_Reportf( S_WARN "%s: couldn't get render API\n", __func__ );

	if( !Mobile_Init() ) // Xash3D FWGS extension: mobile interface
		Con_Reportf( S_WARN "%s: couldn't get mobility API\n", __func__ );

	CL_InitEdicts( cl.maxclients );		// initailize local player and world
	CL_InitClientMove();	// initialize pm_shared

	// initialize game
	clgame.dllFuncs.pfnInit();

	ref.dllFuncs.CL_InitStudioAPI();

	return true;
}
