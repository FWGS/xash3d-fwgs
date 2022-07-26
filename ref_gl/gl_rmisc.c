/*
gl_rmisc.c - renderer misceallaneous
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

#include "gl_local.h"
#include "shake.h"
#include "screenfade.h"
#include "cdll_int.h"

static void R_ParseDetailTextures( const char *filename )
{
	byte *afile;
	char *pfile;
	string	token, texname;
	string	detail_texname;
	string	detail_path;
	float	xScale, yScale;
	texture_t	*tex;
	int	i;

	afile = gEngfuncs.fsapi->LoadFile( filename, NULL, false );
	if( !afile ) return;

	pfile = (char *)afile;

	// format: 'texturename' 'detailtexture' 'xScale' 'yScale'
	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		texname[0] = '\0';
		detail_texname[0] = '\0';

		// read texname
		if( token[0] == '{' )
		{
			// NOTE: COM_ParseFile handled some symbols seperately
			// this code will be fix it
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			Q_strncat( texname, "{", sizeof( texname ));
			Q_strncat( texname, token, sizeof( texname ));
		}
		else Q_strncpy( texname, token, sizeof( texname ));

		// read detailtexture name
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		Q_strncat( detail_texname, token, sizeof( detail_texname ));

		// trying the scales or '{'
		pfile = COM_ParseFile( pfile, token, sizeof( token ));

		// read second part of detailtexture name
		if( token[0] == '{' )
		{
			Q_strncat( detail_texname, token, sizeof( detail_texname ));
			pfile = COM_ParseFile( pfile, token, sizeof( token )); // read scales
			Q_strncat( detail_texname, token, sizeof( detail_texname ));
			pfile = COM_ParseFile( pfile, token, sizeof( token )); // parse scales
		}

		Q_snprintf( detail_path, sizeof( detail_path ), "gfx/%s", detail_texname );

		// read scales
		xScale = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		yScale = Q_atof( token );

		if( xScale <= 0.0f || yScale <= 0.0f )
			continue;

		// search for existing texture and uploading detail texture
		for( i = 0; i < WORLDMODEL->numtextures; i++ )
		{
			tex = WORLDMODEL->textures[i];

			if( Q_stricmp( tex->name, texname ))
				continue;

			tex->dt_texturenum = GL_LoadTexture( detail_path, NULL, 0, TF_FORCE_COLOR );

			// texture is loaded
			if( tex->dt_texturenum )
			{
				gl_texture_t	*glt;

				glt = R_GetTexture( tex->gl_texturenum );
				glt->xscale = xScale;
				glt->yscale = yScale;
			}
			break;
		}
	}

	Mem_Free( afile );
}

void R_NewMap( void )
{
	texture_t	*tx;
	int	i;

	R_ClearDecals(); // clear all level decals

	R_StudioResetPlayerModels();

	// upload detailtextures
	if( CVAR_TO_BOOL( r_detailtextures ))
	{
		string	mapname, filepath;

		Q_strncpy( mapname, WORLDMODEL->name, sizeof( mapname ));
		COM_StripExtension( mapname );
		Q_sprintf( filepath, "%s_detail.txt", mapname );

		R_ParseDetailTextures( filepath );
	}

	if( gEngfuncs.pfnGetCvarFloat( "v_dark" ))
	{
		screenfade_t		*sf = gEngfuncs.GetScreenFade();
		float			fadetime = 5.0f;
		client_textmessage_t	*title;

		title = gEngfuncs.pfnTextMessageGet( "GAMETITLE" );
		if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
			fadetime = 1.0f;

		if( title )
		{
			// get settings from titles.txt
			sf->fadeEnd = title->holdtime + title->fadeout;
			sf->fadeReset = title->fadeout;
		}
		else sf->fadeEnd = sf->fadeReset = fadetime;

		sf->fadeFlags = FFADE_IN;
		sf->fader = sf->fadeg = sf->fadeb = 0;
		sf->fadealpha = 255;
		sf->fadeSpeed = (float)sf->fadealpha / sf->fadeReset;
		sf->fadeReset += gpGlobals->time;
		sf->fadeEnd += sf->fadeReset;

		gEngfuncs.Cvar_SetValue( "v_dark", 0.0f );
	}

	// clear out efrags in case the level hasn't been reloaded
	for( i = 0; i < WORLDMODEL->numleafs; i++ )
		WORLDMODEL->leafs[i+1].efrags = NULL;

	glState.isFogEnabled = false;
	tr.skytexturenum = -1;
	pglDisable( GL_FOG );

	// clearing texture chains
	for( i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		if( !WORLDMODEL->textures[i] )
			continue;

		tx = WORLDMODEL->textures[i];

		if( !Q_strncmp( tx->name, "sky", 3 ) && tx->width == ( tx->height * 2 ))
			tr.skytexturenum = i;

 		tx->texturechain = NULL;
	}

	R_SetupSky( MOVEVARS->skyName );

	GL_BuildLightmaps ();
	R_GenerateVBO();

	if( gEngfuncs.drawFuncs->R_NewMap != NULL )
		gEngfuncs.drawFuncs->R_NewMap();

}
