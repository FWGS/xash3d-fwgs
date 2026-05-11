/*
in_psvita.h - psvita-specific input code
Copyright (C) 2021-2023 fgsfds

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/* 
the SDL fork that we use sometimes fails to call sceImeUpdate,
which leads to input being consumed forever without ever showing the keyboard,
so we just reimplement the whole thing here using SceCommonDialog
*/

#include "platform/platform.h"
#include <vitasdk.h>
#include <SDL.h>

extern int SDL_SendKeyboardText( const char *text );

static qboolean ime_enabled;
static SceWChar16 ime_string[SCE_IME_MAX_TEXT_LENGTH + 1];

/* this is dumb but will probably work fine enough */
static inline void utf2ascii( char *dst, const SceWChar16 *src, unsigned dstsize )
{
	if ( !src || !dst || !dstsize )
		return;
	while ( *src && ( dstsize-- > 0 ) )
		*(dst++) = (*(src++)) & 0xFF;
	*dst = 0x00;
}

static void IME_Open( void )
{
	SceInt32 res;
	SceImeDialogParam param;

	memset( ime_string, 0, sizeof( ime_string ) );

	sceImeDialogParamInit( &param );
	param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
	param.languagesForced = SCE_TRUE;
	param.type = SCE_IME_TYPE_BASIC_LATIN;
	param.title = u"Input text";
	param.maxTextLength = SCE_IME_MAX_TEXT_LENGTH;
	param.initialText = (SceWChar16 *)u"";
	param.inputTextBuffer = ime_string;

	res = sceImeDialogInit( &param );
	if ( res < 0 )
	{
		Con_Reportf( S_WARN "Could not open IME keyboard: %d\n", res );
	}
	else
	{
		ime_enabled = true;
	}
}

static void IME_Close( void )
{
	if ( ime_enabled )
	{
		ime_enabled = false;
		sceImeDialogTerm( );
	}
}

static void IME_Update( void )
{
	char ascii_string[SCE_IME_MAX_TEXT_LENGTH + 2] = { 0 };
	SceImeDialogResult result;
	SceCommonDialogStatus status = sceImeDialogGetStatus( );
	if( status == 2 )
	{
		memset( &result, 0, sizeof( SceImeDialogResult ) );
		sceImeDialogGetResult( &result );
		if( result.button == SCE_IME_DIALOG_BUTTON_ENTER )
		{
			utf2ascii( ascii_string, ime_string, SCE_IME_MAX_TEXT_LENGTH );
			SDL_SendKeyboardText( ascii_string );
		}
		IME_Close();
	}
}

void Platform_EnableTextInput( qboolean enable )
{
	if ( enable )
	{
		if ( ime_enabled )
			IME_Close();
		IME_Open();
		SDL_EventState( SDL_TEXTINPUT, SDL_ENABLE );
	}
	else
	{
		SDL_EventState( SDL_TEXTINPUT, SDL_DISABLE );
		IME_Close();
	}
}

void PSVita_InputUpdate( void )
{
	if ( ime_enabled )
		IME_Update();
}
