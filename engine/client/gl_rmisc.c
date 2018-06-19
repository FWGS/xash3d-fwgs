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

#include "common.h"
#include "client.h"
#include "gl_local.h"
#include "mod_local.h"
#include "shake.h"

typedef struct
{
	const char	*texname;
	const char	*detail;
	const char	material;
	int		lMin;
	int		lMax;
} dmaterial_t;

typedef struct
{
	char		texname[64];		// shortname
	imgfilter_t	filter;
} dfilter_t;

dfilter_t		*tex_filters[MAX_TEXTURES];
int		num_texfilters;

// default rules for apply detail textures.
// maybe move this to external script?
static const dmaterial_t detail_table[] =
{
{ "crt",		"dt_conc",	'C', 0, 0 },	// concrete
{ "rock",		"dt_rock1",	'C', 0, 0 },
{ "conc", 	"dt_conc",	'C', 0, 0 },
{ "brick", 	"dt_brick",	'C', 0, 0 },
{ "wall", 	"dt_brick",	'C', 0, 0 },
{ "city", 	"dt_conc",	'C', 0, 0 },
{ "crete",	"dt_conc",	'C', 0, 0 },
{ "generic",	"dt_brick",	'C', 0, 0 },
{ "floor", 	"dt_conc",	'C', 0, 0 },
{ "metal",	"dt_metal%i",	'M', 1, 2 },	// metal
{ "mtl",		"dt_metal%i",	'M', 1, 2 },
{ "pipe",		"dt_metal%i",	'M', 1, 2 },
{ "elev",		"dt_metal%i",	'M', 1, 2 },
{ "sign",		"dt_metal%i",	'M', 1, 2 },
{ "barrel",	"dt_metal%i",	'M', 1, 2 },
{ "bath",		"dt_ssteel1",	'M', 1, 2 },
{ "tech",		"dt_ssteel1",	'M', 1, 2 },
{ "refbridge",	"dt_metal%i",	'M', 1, 2 },
{ "panel",	"dt_ssteel1",	'M', 0, 0 },
{ "brass",	"dt_ssteel1",	'M', 0, 0 },
{ "rune",		"dt_metal%i",	'M', 1, 2 },
{ "car",		"dt_metal%i",	'M', 1, 2 },
{ "circuit",	"dt_metal%i",	'M', 1, 2 },
{ "steel",	"dt_ssteel1",	'M', 0, 0 },
{ "dirt",		"dt_ground%i",	'D', 1, 5 },	// dirt
{ "drt",		"dt_ground%i",	'D', 1, 5 },
{ "out",		"dt_ground%i",	'D', 1, 5 },
{ "grass",	"dt_grass1",	'D', 0, 0 },
{ "mud",		"dt_carpet1",	'D', 0, 0 },
{ "vent",		"dt_ssteel1",	'V', 1, 4 },	// vent
{ "duct",		"dt_ssteel1",	'V', 1, 4 },
{ "tile",		"dt_smooth%i",	'T', 1, 2 },
{ "labflr",	"dt_smooth%i",	'T', 1, 2 },
{ "bath",		"dt_smooth%i",	'T', 1, 2 },
{ "grate",	"dt_stone%i",	'G', 1, 4 },	// vent
{ "stone",	"dt_stone%i",	'G', 1, 4 },
{ "grt",		"dt_stone%i",	'G', 1, 4 },
{ "wiz",		"dt_wood%i",	'W', 1, 3 },
{ "wood",		"dt_wood%i",	'W', 1, 3 },
{ "wizwood",	"dt_wood%i",	'W', 1, 3 },
{ "wd",		"dt_wood%i",	'W', 1, 3 },
{ "table",	"dt_wood%i",	'W', 1, 3 },
{ "board",	"dt_wood%i",	'W', 1, 3 },
{ "chair",	"dt_wood%i",	'W', 1, 3 },
{ "brd",		"dt_wood%i",	'W', 1, 3 },
{ "carp",		"dt_carpet1",	'W', 1, 3 },
{ "book",		"dt_wood%i",	'W', 1, 3 },
{ "box",		"dt_wood%i",	'W', 1, 3 },
{ "cab",		"dt_wood%i",	'W', 1, 3 },
{ "couch",	"dt_wood%i",	'W', 1, 3 },
{ "crate",	"dt_wood%i",	'W', 1, 3 },
{ "poster",	"dt_plaster%i",	'W', 1, 2 },
{ "sheet",	"dt_plaster%i",	'W', 1, 2 },
{ "stucco",	"dt_plaster%i",	'W', 1, 2 },
{ "comp",		"dt_smooth1",	'P', 0, 0 },
{ "cmp",		"dt_smooth1",	'P', 0, 0 },
{ "elec",		"dt_smooth1",	'P', 0, 0 },
{ "vend",		"dt_smooth1",	'P', 0, 0 },
{ "monitor",	"dt_smooth1",	'P', 0, 0 },
{ "phone",	"dt_smooth1",	'P', 0, 0 },
{ "glass",	"dt_ssteel1",	'Y', 0, 0 },
{ "window",	"dt_ssteel1",	'Y', 0, 0 },
{ "flesh",	"dt_rough1",	'F', 0, 0 },
{ "meat",		"dt_rough1",	'F', 0, 0 },
{ "fls",		"dt_rough1",	'F', 0, 0 },
{ "ground",	"dt_ground%i",	'D', 1, 5 },
{ "gnd",		"dt_ground%i",	'D', 1, 5 },
{ "snow",		"dt_snow%i",	'O', 1, 2 },	// snow
{ "wswamp",	"dt_smooth1",	'W', 0, 0 },
{ NULL, NULL, 0, 0, 0 }
};

static const char *R_DetailTextureForName( const char *name )
{
	const dmaterial_t	*table;

	if( !name || !*name ) return NULL;
	if( !Q_strnicmp( name, "sky", 3 ))
		return NULL; // never details for sky

	// never apply details for liquids
	if( !Q_strnicmp( name + 1, "!lava", 5 ))
		return NULL;
	if( !Q_strnicmp( name + 1, "!slime", 6 ))
		return NULL;
	if( !Q_strnicmp( name, "!cur_90", 7 ))
		return NULL;
	if( !Q_strnicmp( name, "!cur_0", 6 ))
		return NULL;
	if( !Q_strnicmp( name, "!cur_270", 8 ))
		return NULL;
	if( !Q_strnicmp( name, "!cur_180", 8 ))
		return NULL;
	if( !Q_strnicmp( name, "!cur_up", 7 ))
		return NULL;
	if( !Q_strnicmp( name, "!cur_dwn", 8 ))
		return NULL;
	if( name[0] == '!' )
		return NULL;

	// never apply details to the special textures
	if( !Q_strnicmp( name, "origin", 6 ))
		return NULL;
	if( !Q_strnicmp( name, "clip", 4 ))
		return NULL;
	if( !Q_strnicmp( name, "hint", 4 ))
		return NULL;
	if( !Q_strnicmp( name, "skip", 4 ))
		return NULL;
	if( !Q_strnicmp( name, "translucent", 11 ))
		return NULL;
	if( !Q_strnicmp( name, "3dsky", 5 ))	// xash-mod support :-)
		return NULL;
	if( !Q_strnicmp( name, "scroll", 6 ))
		return NULL;
	if( name[0] == '@' )
		return NULL;

	// last check ...
	if( !Q_strnicmp( name, "null", 4 ))
		return NULL;

	for( table = detail_table; table && table->texname; table++ )
	{
		if( Q_stristr( name, table->texname ))
		{
			if(( table->lMin + table->lMax ) > 0 )
				return va( table->detail, COM_RandomLong( table->lMin, table->lMax )); 
			return table->detail;
		}
	}

	return NULL;
}

void R_CreateDetailTexturesList( const char *filename )
{
	file_t		*detail_txt = NULL;
	float		xScale, yScale;
	const char	*detail_name;
	texture_t		*tex;
	rgbdata_t		*pic;
	int		i;

	for( i = 0; i < cl.worldmodel->numtextures; i++ )
	{
		tex = cl.worldmodel->textures[i];
		detail_name = R_DetailTextureForName( tex->name );
		if( !detail_name ) continue;

		// detailtexture detected
		if( detail_name )
		{
			if( !detail_txt ) detail_txt = FS_Open( filename, "w", false ); 
			if( !detail_txt )
			{
				MsgDev( D_ERROR, "Can't write %s\n", filename );
				break;
			}

			pic = FS_LoadImage( va( "gfx/detail/%s", detail_name ), NULL, 0 );

			if( pic )
			{
				xScale = (pic->width / (float)tex->width) * gl_detailscale->value;
				yScale = (pic->height / (float)tex->height) * gl_detailscale->value;
				FS_FreeImage( pic );
			}
			else xScale = yScale = 10.0f;

			// store detailtexture description
			FS_Printf( detail_txt, "%s detail/%s %.2f %.2f\n", tex->name, detail_name, xScale, yScale );
		}
	}

	if( detail_txt ) FS_Close( detail_txt );
}

void R_ParseDetailTextures( const char *filename )
{
	char	*afile, *pfile;
	string	token, texname;
	string	detail_texname;
	string	detail_path;
	float	xScale, yScale;
	texture_t	*tex;
	int	i;

	if( r_detailtextures->value >= 2 && !FS_FileExists( filename, false ))
	{
		// use built-in generator for detail textures
		R_CreateDetailTexturesList( filename );
	}

	afile = FS_LoadFile( filename, NULL, false );
	if( !afile ) return;

	pfile = afile;

	// format: 'texturename' 'detailtexture' 'xScale' 'yScale'
	while(( pfile = COM_ParseFile( pfile, token )) != NULL )
	{
		texname[0] = '\0';
		detail_texname[0] = '\0';

		// read texname
		if( token[0] == '{' )
		{
			// NOTE: COM_ParseFile handled some symbols seperately
			// this code will be fix it
			pfile = COM_ParseFile( pfile, token );
			Q_strncat( texname, "{", sizeof( texname ));
			Q_strncat( texname, token, sizeof( texname ));
		}
		else Q_strncpy( texname, token, sizeof( texname ));

		// read detailtexture name
		pfile = COM_ParseFile( pfile, token );
		Q_strncat( detail_texname, token, sizeof( detail_texname ));

		// trying the scales or '{'
		pfile = COM_ParseFile( pfile, token );

		// read second part of detailtexture name
		if( token[0] == '{' )
		{
			Q_strncat( detail_texname, token, sizeof( detail_texname ));
			pfile = COM_ParseFile( pfile, token ); // read scales
			Q_strncat( detail_texname, token, sizeof( detail_texname ));
			pfile = COM_ParseFile( pfile, token ); // parse scales
		}

		Q_snprintf( detail_path, sizeof( detail_path ), "gfx/%s", detail_texname );

		// read scales
		xScale = Q_atof( token );		

		pfile = COM_ParseFile( pfile, token );
		yScale = Q_atof( token );

		if( xScale <= 0.0f || yScale <= 0.0f )
			continue;

		// search for existing texture and uploading detail texture
		for( i = 0; i < cl.worldmodel->numtextures; i++ )
		{
			tex = cl.worldmodel->textures[i];

			if( Q_stricmp( tex->name, texname ))
				continue;

			tex->dt_texturenum = GL_LoadTexture( detail_path, NULL, 0, TF_FORCE_COLOR, NULL );

			// texture is loaded
			if( tex->dt_texturenum )
			{
				gltexture_t	*glt;

				glt = R_GetTexture( tex->gl_texturenum );
				glt->xscale = xScale;
				glt->yscale = yScale;
			}
			break;
		}
	}

	Mem_Free( afile );
}

void R_ParseTexFilters( const char *filename )
{
	char	*afile, *pfile;
	string	token, texname;
	dfilter_t	*tf;
	int	i;

	afile = FS_LoadFile( filename, NULL, false );
	if( !afile ) return;

	pfile = afile;

	// format: 'texturename' 'filtername' 'factor' 'bias' 'blendmode' 'grayscale'
	while(( pfile = COM_ParseFile( pfile, token )) != NULL )
	{
		imgfilter_t	filter;

		memset( &filter, 0, sizeof( filter ));
		Q_strncpy( texname, token, sizeof( texname ));

		// parse filter
		pfile = COM_ParseFile( pfile, token );
		if( !Q_stricmp( token, "blur" ))
			filter.filter = BLUR_FILTER;
		else if( !Q_stricmp( token, "blur2" ))
			filter.filter = BLUR_FILTER2;
		else if( !Q_stricmp( token, "edge" ))
			filter.filter = EDGE_FILTER;
		else if( !Q_stricmp( token, "emboss" ))
			filter.filter = EMBOSS_FILTER;

		// reading factor
		pfile = COM_ParseFile( pfile, token );
		filter.factor = Q_atof( token );

		// reading bias
		pfile = COM_ParseFile( pfile, token );
		filter.bias = Q_atof( token );

		// reading blendFunc
		pfile = COM_ParseFile( pfile, token );
		if( !Q_stricmp( token, "modulate" ) || !Q_stricmp( token, "GL_MODULATE" ))
			filter.blendFunc = GL_MODULATE;
		else if( !Q_stricmp( token, "replace" ) || !Q_stricmp( token, "GL_REPLACE" ))
			filter.blendFunc = GL_REPLACE;
		else if( !Q_stricmp( token, "add" ) || !Q_stricmp( token, "GL_ADD" ))
			filter.blendFunc = GL_ADD;
		else if( !Q_stricmp( token, "decal" ) || !Q_stricmp( token, "GL_DECAL" ))
			filter.blendFunc = GL_DECAL;
		else if( !Q_stricmp( token, "blend" ) || !Q_stricmp( token, "GL_BLEND" ))
			filter.blendFunc = GL_BLEND;
		else if( !Q_stricmp( token, "add_signed" ) || !Q_stricmp( token, "GL_ADD_SIGNED" ))
			filter.blendFunc = GL_ADD_SIGNED;
		else MsgDev( D_WARN, "unknown blendFunc '%s' specified for texture '%s'\n", texname, token );

		// reading flags
		pfile = COM_ParseFile( pfile, token );
		filter.flags = Q_atoi( token );

		// make sure what factor is not zeroed
		if( filter.factor == 0.0f )
		{
			MsgDev( D_WARN, "texfilter for texture %s has factor 0! Ignored\n", texname );
			continue;
		}

		// check if already existed
		for( i = 0; i < num_texfilters; i++ )
		{
			tf = tex_filters[i];

			if( !Q_stricmp( tf->texname, texname ))
			{
				MsgDev( D_WARN, "texture %s has specified multiple filters! Ignored\n", texname );
				break;
			}
		}

		if( i != num_texfilters )
			continue;	// already specified

		// allocate new texfilter
		tf = Z_Malloc( sizeof( dfilter_t ));
		tex_filters[num_texfilters++] = tf;

		Q_strncpy( tf->texname, texname, sizeof( tf->texname ));
		tf->filter = filter;
	}

	MsgDev( D_INFO, "%i texture filters parsed\n", num_texfilters );

	Mem_Free( afile );
}

imgfilter_t *R_FindTexFilter( const char *texname )
{
	dfilter_t	*tf;
	int	i;

	for( i = 0; i < num_texfilters; i++ )
	{
		tf = tex_filters[i];

		if( !Q_stricmp( tf->texname, texname ))
			return &tf->filter;
	}

	return NULL;
}

/*
=======================
R_ClearStaticEntities

e.g. by demo request
=======================
*/
void R_ClearStaticEntities( void )
{
	int	i;

	if( host.type == HOST_DEDICATED )
		return;

	// clear out efrags in case the level hasn't been reloaded
	for( i = 0; i < cl.worldmodel->numleafs; i++ )
		cl.worldmodel->leafs[i+1].efrags = NULL;

	clgame.numStatics = 0;

	CL_ClearEfrags ();
}

void R_NewMap( void )
{
	texture_t	*tx;
	int	i;

	R_ClearDecals(); // clear all level decals

	// upload detailtextures
	if( r_detailtextures->value )
	{
		string	mapname, filepath;

		Q_strncpy( mapname, cl.worldmodel->name, sizeof( mapname ));
		COM_StripExtension( mapname );
		Q_sprintf( filepath, "%s_detail.txt", mapname );

		R_ParseDetailTextures( filepath );
	}

	if( v_dark->value )
	{
		screenfade_t		*sf = &clgame.fade;
		float			fadetime = 5.0f;
		client_textmessage_t	*title;

		title = CL_TextMessageGet( "GAMETITLE" );
		if( CL_IsQuakeCompatible( ))
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
		sf->fadeReset += cl.time;
		sf->fadeEnd += sf->fadeReset;

		Cvar_SetValue( "v_dark", 0.0f );
	}

	// clear out efrags in case the level hasn't been reloaded
	for( i = 0; i < cl.worldmodel->numleafs; i++ )
		cl.worldmodel->leafs[i+1].efrags = NULL;

	tr.skytexturenum = -1;
	pglDisable( GL_FOG );

	// clearing texture chains
	for( i = 0; i < cl.worldmodel->numtextures; i++ )
	{
		if( !cl.worldmodel->textures[i] )
			continue;

		tx = cl.worldmodel->textures[i];

		if( !Q_strncmp( tx->name, "sky", 3 ) && tx->width == 256 && tx->height == 128 )
			tr.skytexturenum = i;

 		tx->texturechain = NULL;
	}

	R_SetupSky( clgame.movevars.skyName );

	GL_BuildLightmaps ();
}