/*
mdldec.c - Half-Life Studio Model Decompiler
Copyright (C) 2020 Andrey Akhmichin

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "const.h"
#include "com_model.h"
#include "crtlib.h"
#include "studio.h"
#include "qc.h"
#include "smd.h"
#include "texture.h"
#include "utils.h"
#include "version.h"

char		  destdir[MAX_SYSPATH];
char		  modelfile[MAX_SYSPATH];
studiohdr_t	 *model_hdr;
studiohdr_t	 *texture_hdr;
studiohdr_t	**anim_hdr;

/*
============
SequenceNameFix
============
*/
static void SequenceNameFix( void )
{
	int			 i, j, counter;
	qboolean		 hasduplicates = false;
	mstudioseqdesc_t	*seqdesc, *seqdesc1;

	for( i = 0; i < model_hdr->numseq; i++ )
	{
		seqdesc = (mstudioseqdesc_t *)( (byte *)model_hdr + model_hdr->seqindex ) + i;

		counter = 1;

		for( j = 0; j < model_hdr->numseq; j++ )
		{
			seqdesc1 = (mstudioseqdesc_t *)( (byte *)model_hdr + model_hdr->seqindex ) + j;

			if( j != i && !Q_strncmp( seqdesc1->label, seqdesc->label, sizeof( seqdesc1->label ) ) )
				Q_snprintf( seqdesc1->label, sizeof( seqdesc1->label ), "%s_%i", seqdesc1->label, ++counter );
		}

		if( counter > 1 )
		{
			printf( "WARNING: Sequence name \"%s\" is repeated %i times.\n", seqdesc->label, counter );

			Q_snprintf( seqdesc->label, sizeof( seqdesc->label ), "%s_1", seqdesc->label );

			hasduplicates = true;
		}
	}

	if( hasduplicates )
		puts( "WARNING: Added numeric suffix to repeated sequence name(s)." );
}

/*
============
BoneNameFix
============
*/
static void BoneNameFix( void )
{
	int		 i, counter = 0;
	mstudiobone_t	*bone;

	for( i = 0; i < model_hdr->numbones; i++ )
	{
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + i;

		if( bone->name[0] == '\0' )
			Q_sprintf( bone->name, "MDLDEC_Bone%i", ++counter );
	}

	if( counter )
		printf( "WARNING: Gived name to %i unnamed bone(s).\n", counter );
}

/*
============
LoadMDL
============
*/
static qboolean LoadMDL( const char *modelname )
{
	int		 i;
	size_t		 len;
	char		 texturename[MAX_SYSPATH];
	char		 seqgroupname[MAX_SYSPATH];
	const char	*ext;
	const char	 id_mdlhdr[] = {'I', 'D', 'S', 'T'};
	const char	 id_seqhdr[] = {'I', 'D', 'S', 'Q'};

	printf( "MDL: %s\n", modelname );

	len = Q_strlen( modelname );

	if( len > MAX_SYSPATH - 3 )
	{
		fputs( "ERROR: Source path is too long.\n", stderr );
		return false;
	}

	ext = COM_FileExtension( modelname );

	if( !ext )
	{
		fprintf( stderr, "ERROR: Source file does not have extension.\n" );
		return false;
	}

	if( Q_strcmp( ext, "mdl" ) )
	{
		fprintf( stderr, "ERROR: Only .mdl-files is supported.\n" );
		return false;
	}

	model_hdr = (studiohdr_t *)LoadFile( modelname );

	if( !model_hdr )
	{
		fprintf( stderr, "ERROR: Can't open %s\n", modelname );
		return false;
	}

	if( memcmp( &model_hdr->ident, id_mdlhdr, sizeof( id_mdlhdr ) ) )
	{
		if( !memcmp( &model_hdr->ident, id_seqhdr, sizeof( id_seqhdr ) ) )
			fprintf( stderr, "ERROR: %s is not a main HL model file.\n", modelname );
		else
			fprintf( stderr, "ERROR: %s is not a valid HL model file.\n", modelname );

		return false;
	}

	if( model_hdr->version != STUDIO_VERSION )
	{
		fprintf( stderr, "ERROR: %s has unknown Studio MDL format version.\n", modelname );
		return false;
	}

	if( !model_hdr->numbodyparts )
	{
		fprintf( stderr, "ERROR: %s is not a main HL model file.\n", modelname );
		return false;
	}
                        
	if( destdir[0] != '\0' )
	{
		if( !IsFileExists( destdir ) )
		{
			fprintf( stderr, "ERROR: Couldn't find directory %s\n", destdir );
			return false;
		}

		COM_PathSlashFix( destdir );
	}
	else
		COM_ExtractFilePath( modelname, destdir );

	len -= 4; // path length without extension

	if( !model_hdr->numtextures )
	{
		Q_strcpy( texturename, modelname );
		Q_strcpy( &texturename[len], "t.mdl" );

		texture_hdr = (studiohdr_t *)LoadFile( texturename );

		if( !texture_hdr )
		{
#if !XASH_WIN32
			// dirty hack for casesensetive filesystems
			texturename[len] = 'T';

			texture_hdr = (studiohdr_t *)LoadFile( texturename );

			if( !texture_hdr )
#endif
			{
				fprintf( stderr, "ERROR: Can't open external textures file %s\n", texturename );
				return false;
			}
		}

		if( memcmp( &texture_hdr->ident, id_mdlhdr, sizeof( id_mdlhdr ) ) )
		{
			fprintf( stderr, "ERROR: %s is not a valid external textures file.\n", texturename );
			return false;
		}
	}
	else
		texture_hdr = model_hdr;

	anim_hdr = malloc( sizeof( studiohdr_t* ) * model_hdr->numseqgroups );

	if( !anim_hdr )
	{
		fputs( "ERROR: Couldn't allocate memory for sequences.\n", stderr );
		return false;
	}

	anim_hdr[0] = model_hdr;

	if( model_hdr->numseqgroups > 1 )
	{
		Q_strcpy( seqgroupname, modelname );

		for( i = 1; i < model_hdr->numseqgroups; i++ )
		{
			Q_sprintf( &seqgroupname[len], "%02d.mdl", i );

			anim_hdr[i] = (studiohdr_t *)LoadFile( seqgroupname );

			if( !anim_hdr[i] )
			{
				fprintf( stderr, "ERROR: Can't open sequence file %s\n", seqgroupname );
				return false;
			}

			if( memcmp( &anim_hdr[i]->ident, id_seqhdr, sizeof( id_seqhdr ) ) )
			{
				fprintf( stderr, "ERROR: %s is not a valid sequence file.\n", seqgroupname );
				return false;
			}
		}
	}

	COM_FileBase( modelname, modelfile );

	SequenceNameFix();

	BoneNameFix();

	return true;
}

/*
============
ShowHelp
============
*/
static void ShowHelp( const char *app_name )
{
	printf( "usage: %s source_file\n", app_name );
	printf( "       %s source_file target_directory\n", app_name );
}

int main( int argc, char *argv[] )
{
	puts( "\nHalf-Life Studio Model Decompiler " APP_VERSION );
	puts( "Copyright Flying With Gauss 2020 (c) " );
	puts( "--------------------------------------------------" );

	if( argc == 1 )
	{
		ShowHelp( argv[0] );
		goto end;
	}
	else if( argc == 3 )
	{
		if( Q_strlen( argv[2] ) > MAX_SYSPATH - 1 )
		{
			fputs( "ERROR: Destination path is too long.\n", stderr );
			goto end;
		}

		Q_strcpy( destdir, argv[2] );
	}

	if( !LoadActivityList( argv[0] ) || !LoadMDL( argv[1] ) )
		goto end;

	WriteQCScript();
	WriteSMD();
	WriteTextures();

	puts( "Done." );

end:
	puts( "--------------------------------------------------" );

	return 0;
}

