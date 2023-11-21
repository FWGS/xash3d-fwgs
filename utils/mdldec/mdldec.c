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

#include <ctype.h>
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
IsValidName
============
*/
static qboolean IsValidName( char *name )
{
	if( !( isalpha( *name ) || isdigit( *name )))
		return false;

	while( *( ++name ))
	{
		if( isalpha( *name ) || isdigit( *name )
		    || *name == '.' || *name == '-' || *name == '_'
		    || *name == ' ' || *name == '(' || *name == ')'
		    || *name == '[' || *name == ']')
			continue;
		// Found control character Ctrl+Shift+A(SOH|^A|0x1) in the end of name in some models.
		else if( name[1] == '\0' )
		{
			*name = '\0';
			return true;
		}

		return false;
	}

	return true;
}

/*
============
TextureNameFix
============
*/
static void TextureNameFix( void )
{
	int			 i, j, len, counter, protected = 0;
	qboolean		 hasduplicates = false;
	mstudiotexture_t	*texture = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex ), *texture1;

	for( i = 0; i < texture_hdr->numtextures; ++i, ++texture )
	{
		ExtractFileName( texture->name, sizeof( texture->name ));
		if( !Q_strchr( texture->name, '.' ) )
			Q_strncat( texture->name, ".bmp", sizeof( texture->name ));
	}

	texture -= i;

	for( i = 0; i < texture_hdr->numtextures; ++i, ++texture )
	{
		if( !IsValidName( texture->name ))
		{
			Q_snprintf( texture->name, sizeof( texture->name ), "MDLDEC_Texture%i.bmp", ++protected );
			continue;
		}

		counter = 0;

		texture1 = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex );

		for( j = 0; j < texture_hdr->numtextures; ++j, ++texture1 )
		{
			if( j != i && !Q_strncmp( texture1->name, texture->name, sizeof( texture1->name)))
			{
				len = Q_snprintf( texture1->name, sizeof( texture1->name ), "%s_%i.bmp", texture1->name, ++counter );

				if( len == -1 )
					Q_snprintf( texture1->name, sizeof( texture1->name ), "MDLDEC_Texture%i_%i.bmp", j, counter );
			}
		}

		if( counter > 0 )
		{
			printf( "WARNING: Texture name \"%s\" is repeated %i times.\n", texture->name, counter );

			hasduplicates = true;
		}
	}

	if( protected )
		printf( "WARNING: Gived name to %i protected texture(s).\n", protected );

	if( hasduplicates )
		puts( "WARNING: Added numeric suffix to repeated texture name(s)." );
}

/*
============
BodypartNameFix
============
*/
static void BodypartNameFix( void )
{
	int			 i, j, k, len, counter, protected = 0, protected_models = 0;
	qboolean		 hasduplicates = false;
	mstudiobodyparts_t	*bodypart = (mstudiobodyparts_t *) ( (byte *)model_hdr + model_hdr->bodypartindex );
	mstudiomodel_t		*model, *model1;

	for( i = 0; i < model_hdr->numbodyparts; ++i, ++bodypart )
		ExtractFileName( bodypart->name, sizeof( bodypart->name ));

	bodypart -= i;

	for( i = 0; i < model_hdr->numbodyparts; ++i, ++bodypart )
	{
		if( !IsValidName( bodypart->name ))
		{
			Q_snprintf( bodypart->name, sizeof( bodypart->name ), "MDLDEC_Bodypart%i", ++protected );
			continue;
		}

		model = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex );

		for( j = 0; j < bodypart->nummodels; ++j, ++model )
			ExtractFileName( model->name, sizeof( model->name ));

		model -= j;

		for( j = 0; j < bodypart->nummodels; ++j, ++model )
		{
			if( !IsValidName( model->name ))
			{
				Q_snprintf( model->name, sizeof( model->name ), "MDLDEC_Model%i", ++protected_models );
				continue;
			}

			counter = 0;

			model1 = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex );

			for( k = 0; k < bodypart->nummodels; ++k, ++model1 )
			{
				if( k != j && !Q_strncmp( model1->name, model->name, sizeof( model1->name )))
				{
					len = Q_snprintf( model1->name, sizeof( model1->name ), "%s_%i", model1->name, ++counter );

					if( len == -1 )
						Q_snprintf( model1->name, sizeof( model1->name ), "MDLDEC_Model%i_%i", k, counter );
				}
			}

			if( counter > 0 )
			{
				printf( "WARNING: Sequence name \"%s\" is repeated %i times.\n", model->name, counter );

				hasduplicates = true;
			}
		}
	}

	if( protected )
		printf( "WARNING: Gived name to %i protected bodypart(s).\n", protected );

	if( protected_models )
		printf( "WARNING: Gived name to %i protected model(s).\n", protected_models );

	if( hasduplicates )
		puts( "WARNING: Added numeric suffix to repeated bodypart name(s)." );
}

/*
============
SequenceNameFix
============
*/
static void SequenceNameFix( void )
{
	int			 i, j, len, counter, protected = 0;
	qboolean		 hasduplicates = false;
	mstudioseqdesc_t	*seqdesc = (mstudioseqdesc_t *)( (byte *)model_hdr + model_hdr->seqindex ), *seqdesc1;

	for( i = 0; i < model_hdr->numseq; ++i, ++seqdesc )
		ExtractFileName( seqdesc->label, sizeof( seqdesc->label ));

	seqdesc -= i;

	for( i = 0; i < model_hdr->numseq; ++i, ++seqdesc )
	{
		if( !IsValidName( seqdesc->label ))
		{
			Q_snprintf( seqdesc->label, sizeof( seqdesc->label ), "MDLDEC_Sequence%i", ++protected );
			continue;
		}

		counter = 0;

		seqdesc1 = (mstudioseqdesc_t *)( (byte *)model_hdr + model_hdr->seqindex );

		for( j = 0; j < model_hdr->numseq; ++j, ++seqdesc1 )
		{
			if( j != i && !Q_strncmp( seqdesc1->label, seqdesc->label, sizeof( seqdesc1->label )))
			{
				len = Q_snprintf( seqdesc1->label, sizeof( seqdesc1->label ), "%s_%i", seqdesc1->label, ++counter );

				if( len == -1 )
					Q_snprintf( seqdesc1->label, sizeof( seqdesc1->label ), "MDLDEC_Sequence%i_%i", j, counter );
			}
		}

		if( counter > 0 )
		{
			printf( "WARNING: Sequence name \"%s\" is repeated %i times.\n", seqdesc->label, counter );

			hasduplicates = true;
		}
	}

	if( protected )
		printf( "WARNING: Gived name to %i protected sequence(s).\n", protected );

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
	int		 i, protected = 0;
	mstudiobone_t	*bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex );

	for( i = 0; i < model_hdr->numbones; ++i, ++bone )
	{
		bone->name[sizeof( bone->name ) - 1] = '\0';

		if( !IsValidName( bone->name ) )
			Q_snprintf( bone->name, sizeof( bone->name ), "MDLDEC_Bone%i", ++protected );
	}

	if( protected )
		printf( "WARNING: Gived name to %i protected bone(s).\n", protected );
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
	off_t		 filesize;
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

	if( Q_stricmp( ext, "mdl" ) )
	{
		fprintf( stderr, "ERROR: Only .mdl-files is supported.\n" );
		return false;
	}

	model_hdr = (studiohdr_t *)LoadFile( modelname, &filesize );

	if( filesize < sizeof( studiohdr_t ) || filesize != model_hdr->length )
	{
		fprintf( stderr, "ERROR: Wrong file size! File %s may be corrupted!\n", modelname );
		return false;
	}

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
		fprintf( stderr, "ERROR: %s has unknown Studio MDL format version %d.\n", modelname, model_hdr->version );
		return false;
	}

	if( !model_hdr->numbodyparts )
	{
		fprintf( stderr, "ERROR: %s is not a main HL model file.\n", modelname );
		return false;
	}

	if( destdir[0] != '\0' )
	{
		if( !MakeDirectory( destdir ))
		{
			fprintf( stderr, "ERROR: Couldn't create directory %s\n", destdir );
			return false;
		}
	}
	else
		COM_ExtractFilePath( modelname, destdir );

	if( destdir[0] != '\0' )
		COM_PathSlashFix( destdir );

	len -= ( sizeof( ".mdl" ) - 1 ); // path length without extension

	if( !model_hdr->numtextures )
	{
		Q_strncpy( texturename, modelname, sizeof( texturename ));
		Q_strncpy( &texturename[len], "t.mdl", sizeof( texturename ) - len );

		texture_hdr = (studiohdr_t *)LoadFile( texturename, &filesize );

		if( !texture_hdr )
		{
#if !XASH_WIN32
			// dirty hack for casesensetive filesystems
			texturename[len] = 'T';

			texture_hdr = (studiohdr_t *)LoadFile( texturename, &filesize );

			if( !texture_hdr )
#endif
			{
				fprintf( stderr, "ERROR: Can't open external textures file %s\n", texturename );
				return false;
			}
		}

		if( filesize < sizeof( studiohdr_t ) || filesize != model_hdr->length )
		{
			fprintf( stderr, "ERROR: Wrong file size! File %s may be corrupted!\n", texturename );
			return false;
		}

		if( memcmp( &texture_hdr->ident, id_mdlhdr, sizeof( id_mdlhdr ) )
		    || !texture_hdr->numtextures )
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
		Q_strncpy( seqgroupname, modelname, sizeof( seqgroupname ));

		for( i = 1; i < model_hdr->numseqgroups; i++ )
		{
			Q_snprintf( &seqgroupname[len], sizeof( seqgroupname ) - len, "%02d.mdl", i );

			anim_hdr[i] = (studiohdr_t *)LoadFile( seqgroupname, &filesize );

			if( !anim_hdr[i] )
			{
				fprintf( stderr, "ERROR: Can't open sequence file %s\n", seqgroupname );
				return false;
			}

			if( filesize < sizeof( studiohdr_t ) || filesize != model_hdr->length )
			{
				fprintf( stderr, "ERROR: Wrong file size! File %s may be corrupted!\n", seqgroupname );
				return false;
			}

			if( memcmp( &anim_hdr[i]->ident, id_seqhdr, sizeof( id_seqhdr ) ) )
			{
				fprintf( stderr, "ERROR: %s is not a valid sequence file.\n", seqgroupname );
				return false;
			}
		}
	}

	COM_FileBase( modelname, modelfile, sizeof( modelfile ));

	// Some validation checks was found in mdldec-golang by Psycrow101
	if( model_hdr->numhitboxes > model_hdr->numbones * ( MAXSTUDIOSRCBONES / MAXSTUDIOBONES ))
	{
		printf( "WARNING: Invalid hitboxes number %d.\n", model_hdr->numhitboxes );
		model_hdr->numhitboxes = 0;
	}
	else if( model_hdr->hitboxindex + model_hdr->numhitboxes * ( sizeof( mstudiobbox_t ) + sizeof( mstudiohitboxset_t )) > model_hdr->length )
	{
		printf( "WARNING: Invalid hitboxes offset %d.\n", model_hdr->hitboxindex );
		model_hdr->numhitboxes = 0;
	}

	TextureNameFix();

	BodypartNameFix();

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
	int ret = 0;

	puts( "\nHalf-Life Studio Model Decompiler " APP_VERSION );
	puts( "Copyright Flying With Gauss 2020 (c) " );
	puts( "--------------------------------------------------" );

	if( argc == 1 )
	{
		ShowHelp( argv[0] );
		ret = 2;
		goto end;
	}
	else if( argc == 3 )
	{
		if( Q_strlen( argv[2] ) > MAX_SYSPATH - 2 )
		{
			fputs( "ERROR: Destination path is too long.\n", stderr );
			ret = 1;
			goto end;
		}

		Q_strncpy( destdir, argv[2], sizeof( destdir ));
	}

	if( !LoadActivityList( argv[0] ) || !LoadMDL( argv[1] ) )
	{
		ret = 1;
		goto end;
	}

	WriteQCScript();
	WriteSMD();
	WriteTextures();

	puts( "Done." );

end:
	puts( "--------------------------------------------------" );

	return ret;
}

