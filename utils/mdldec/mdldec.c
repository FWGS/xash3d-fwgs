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
#include "build.h"
#if !XASH_WIN32
	#include <unistd.h>
#else
	#include "getopt.h"
#endif
#include "const.h"
#include "com_model.h"
#include "crtlib.h"
#include "studio.h"
#include "qc.h"
#include "smd.h"
#include "texture.h"
#include "utils.h"
#include "version.h"
#include "settings.h"
#include "mdldec.h"

char		  destdir[MAX_SYSPATH];
char		  modelfile[MAX_SYSPATH];
studiohdr_t	 *model_hdr;
studiohdr_t	 *texture_hdr;
studiohdr_t	**anim_hdr;
int		  globalsettings;

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

		if( !( ( ( texture->width == 1 || texture->height == 1 )
		    && texture->name[0] == '#' ) || IsValidName( texture->name )))
			Q_snprintf( texture->name, sizeof( texture->name ), "MDLDEC_Texture%i.bmp", ++protected );
	}

	texture -= i;

	for( i = 0; i < texture_hdr->numtextures; ++i, ++texture )
	{
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
			LogPrintf( "WARNING: Texture name \"%s\" is repeated %i times.", texture->name, counter );

			hasduplicates = true;
		}
	}

	if( protected )
		LogPrintf( "WARNING: Gived name to %i protected texture%s.", protected, protected > 1 ? "s" : "" );

	if( hasduplicates )
		LogPutS( "WARNING: Added numeric suffix to repeated texture name(s)." );
}

/*
============
BodypartNameFix
============
*/
static void BodypartNameFix( void )
{
	int			 i, j, k, l, len, counter, protected = 0, protected_models = 0;
	qboolean		 hasduplicates = false;
	mstudiobodyparts_t	*bodypart = (mstudiobodyparts_t *)( (byte *)model_hdr + model_hdr->bodypartindex );
	mstudiobodyparts_t	*bodypart1;
	mstudiomodel_t		*model, *model1;

	for( i = 0; i < model_hdr->numbodyparts; ++i, ++bodypart )
	{
		ExtractFileName( bodypart->name, sizeof( bodypart->name ));
		if( !IsValidName( bodypart->name ))
			Q_snprintf( bodypart->name, sizeof( bodypart->name ), "MDLDEC_Bodypart%i", ++protected );

		model = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex );

		for( j = 0; j < bodypart->nummodels; ++j, ++model )
		{
			ExtractFileName( model->name, sizeof( model->name ));
			COM_StripExtension( model->name );
			if( !IsValidName( model->name ))
				Q_snprintf( model->name, sizeof( model->name ), "MDLDEC_Model%i", ++protected_models );
		}
	}

	bodypart -= i;

	for( i = 0; i < model_hdr->numbodyparts; ++i, ++bodypart )
	{
		model = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex );

		for( j = 0; j < bodypart->nummodels; ++j, ++model )
		{
			counter = 0;

			bodypart1 = (mstudiobodyparts_t *)( (byte *)model_hdr + model_hdr->bodypartindex );

			for( k = 0; k < model_hdr->numbodyparts; ++k, ++bodypart1 )
			{
				model1 = (mstudiomodel_t *)( (byte *)model_hdr + bodypart1->modelindex );

				for( l = 0; l < bodypart1->nummodels; ++l, ++model1 )
				{
					if( !( i==k && j==l ) && !Q_strncmp( model1->name, model->name, sizeof( model1->name )))
					{
						len = Q_snprintf( model1->name, sizeof( model1->name ), "%s_%i", model1->name, ++counter );

						if( len == -1 )
							Q_snprintf( model1->name, sizeof( model1->name ), "MDLDEC_Model%i_%i", l, counter );
					}
				}

				if( counter > 0 )
				{
					LogPrintf( "WARNING: Model name \"%s\" is repeated %i times.", model->name, counter );

					hasduplicates = true;
				}
			}
		}
	}

	if( protected )
		LogPrintf( "WARNING: Gived name to %i protected bodypart%s.", protected, protected > 1 ? "s" : "" );

	if( protected_models )
		LogPrintf( "WARNING: Gived name to %i protected model%s.", protected_models, protected_models > 1 ? "s" : "" );

	if( hasduplicates )
		LogPutS( "WARNING: Added numeric suffix to repeated bodypart name(s)." );
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
	{
		ExtractFileName( seqdesc->label, sizeof( seqdesc->label ));
		COM_StripExtension( seqdesc->label );
		if( !IsValidName( seqdesc->label ))
			Q_snprintf( seqdesc->label, sizeof( seqdesc->label ), "MDLDEC_Sequence%i", ++protected );
	}

	seqdesc -= i;

	for( i = 0; i < model_hdr->numseq; ++i, ++seqdesc )
	{
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
			LogPrintf( "WARNING: Sequence name \"%s\" is repeated %i times.", seqdesc->label, counter );

			hasduplicates = true;
		}
	}

	if( protected )
		LogPrintf( "WARNING: Gived name to %i protected sequence%s.", protected, protected > 1 ? "s" : "" );

	if( hasduplicates )
		LogPutS( "WARNING: Added numeric suffix to repeated sequence name(s)." );
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
		LogPrintf( "WARNING: Gived name to %i protected bone%s.", protected, protected > 1 ? "s" : "" );
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

	LogPrintf( "MDL: %s.", modelname );

	len = Q_strlen( modelname );

	if( len > MAX_SYSPATH - 3 )
	{
		LogPutS( "ERROR: Source path is too long.");
		return false;
	}

	ext = COM_FileExtension( modelname );

	if( !ext )
	{
		LogPutS( "ERROR: Source file does not have extension." );
		return false;
	}

	if( Q_stricmp( ext, "mdl" ) )
	{
		LogPutS( "ERROR: Only .mdl-files is supported." );
		return false;
	}

	model_hdr = (studiohdr_t *)LoadFile( modelname, &filesize );

	if( !model_hdr )
	{
		LogPrintf( "ERROR: Can't open %s.", modelname );
		return false;
	}

	if( filesize < sizeof( studiohdr_t ))
	{
		LogPrintf( "ERROR: Wrong file size! File %s may be corrupted!", modelname );
		return false;
	}

	if( filesize != model_hdr->length )
	{
		// Some bad studio model compiler don't write file length
		if( globalsettings & SETTINGS_NOVALIDATION )
			LogPrintf( "WARNING: Wrong file size! File %s may be corrupted!", modelname );
		else
		{
			LogPrintf( "ERROR: Wrong file size! File %s may be corrupted!", modelname );
			return false;
		}
	}

	if( memcmp( &model_hdr->ident, id_mdlhdr, sizeof( id_mdlhdr ) ) )
	{
		if( !memcmp( &model_hdr->ident, id_seqhdr, sizeof( id_seqhdr ) ) )
			LogPrintf( "ERROR: %s is not a main HL model file.", modelname );
		else
			LogPrintf( "ERROR: %s is not a valid HL model file.", modelname );

		return false;
	}

	if( model_hdr->version != STUDIO_VERSION )
	{
		LogPrintf( "ERROR: %s has unknown Studio MDL format version %d.", modelname, model_hdr->version );
		return false;
	}

	if( !model_hdr->numbodyparts )
	{
		LogPrintf( "ERROR: %s is not a main HL model file.", modelname );
		return false;
	}

	if( destdir[0] != '\0' )
	{
		if( !MakeFullPath( destdir ))
			return false;
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
				LogPrintf( "ERROR: Can't open external textures file %s.", texturename );
				return false;
			}
		}

		if( filesize < sizeof( studiohdr_t ) )
		{
			LogPrintf( "ERROR: Wrong file size! File %s may be corrupted!", texturename );
			return false;
		}

		if( filesize != texture_hdr->length )
		{
			// Some bad studio model compiler don't write file length
			if( globalsettings & SETTINGS_NOVALIDATION )
				LogPrintf( "WARNING: Wrong file size! File %s may be corrupted!", texturename );
			else
			{
				LogPrintf( "ERROR: Wrong file size! File %s may be corrupted!", texturename );
				return false;
			}
		}

		if( memcmp( &texture_hdr->ident, id_mdlhdr, sizeof( id_mdlhdr ) )
		    || !texture_hdr->numtextures )
		{
			LogPrintf( "ERROR: %s is not a valid external textures file.", texturename );
			return false;
		}
	}
	else
		texture_hdr = model_hdr;

	anim_hdr = malloc( sizeof( studiohdr_t* ) * model_hdr->numseqgroups );

	if( !anim_hdr )
	{
		LogPutS( "ERROR: Couldn't allocate memory for sequences." );
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
				LogPrintf( "ERROR: Can't open sequence file %s.", seqgroupname );
				return false;
			}

			if( filesize < sizeof( studiohdr_t ))
			{
				LogPrintf( "ERROR: Wrong file size! File %s may be corrupted!", seqgroupname );
				return false;
			}

			if( filesize != anim_hdr[i]->length )
			{
				// Some bad studio model compiler don't write file length
				if( globalsettings & SETTINGS_NOVALIDATION )
					LogPrintf( "WARNING: Wrong file size! File %s may be corrupted!", seqgroupname );
				else
				{
					LogPrintf( "ERROR: Wrong file size! File %s may be corrupted!", seqgroupname );
					return false;
				}
			}

			if( memcmp( &anim_hdr[i]->ident, id_seqhdr, sizeof( id_seqhdr ) ) )
			{
				LogPrintf( "ERROR: %s is not a valid sequence file.", seqgroupname );
				return false;
			}
		}
	}

	COM_FileBase( modelname, modelfile, sizeof( modelfile ));

	// Some validation checks was found in mdldec-golang by Psycrow101
	if( model_hdr->numhitboxes > model_hdr->numbones * ( MAXSTUDIOSRCBONES / MAXSTUDIOBONES ))
	{
		LogPrintf( "WARNING: Invalid hitboxes number %d.", model_hdr->numhitboxes );
		model_hdr->numhitboxes = 0;
	}
	else if( model_hdr->hitboxindex + model_hdr->numhitboxes * ( sizeof( mstudiobbox_t ) + sizeof( mstudiohitboxset_t )) > model_hdr->length )
	{
		LogPrintf( "WARNING: Invalid hitboxes offset %d.", model_hdr->hitboxindex );
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
ShowVersion
============
*/
static void ShowVersion( void )
{
	LogPutS( "\nHalf-Life Studio Model Decompiler " APP_VERSION );
	LogPutS( "Copyright Flying With Gauss 2020-2025 (c) " );
	LogPutS( "--------------------------------------------------" );
}

/*
============
ShowHelp
============
*/
static void ShowHelp( const char *app_name )
{
	LogPrintf( "usage: %s [-ahlmtuVv] <source_file>", app_name );
	LogPrintf( "       %s [-ahlmtuVv] <source_file> <target_directory>", app_name );
	LogPutS( "\nnote:" );
	LogPutS( "\tby default this decompiler aimed to support extended MDL10 format from XashXT/PrimeXT/Paranoia2." );
	LogPutS( "\tif you use an old GoldSource studio model compiler you may be need to edit .qc-file after decompilation." );
	LogPutS( "\noptions:" );
	LogPutS( "\t-a\tplace files with animations to separate directory." );
	LogPutS( "\t-l\tdo not output logs." );
	LogPutS( "\t-m\tuse GoldSource-compatible motion types." );
	LogPutS( "\t-t\tplace texture files to separate directory." );
	LogPutS( "\t-u\tenable UV coords shifting for DoomMusic's and Sven-Coop's studio model compilers." );
	LogPutS( "\t-V\tignore some validation checks for broken models." );
	LogPutS( "\t-v\tshow version." );
	LogPutS( "\t-h\tthis message." );
}

int main( int argc, char *argv[] )
{
	int opt, ret = 0 ;

	while( ( opt = getopt( argc, argv, "ahlmtuVv" )) != -1 )
	{
		switch( opt )
		{
		case 'a': globalsettings |= SETTINGS_SEPARATEANIMSFOLDER; break;
		case 't': globalsettings |= SETTINGS_SEPARATETEXTURESFOLDER; break;
		case 'l': globalsettings |= SETTINGS_NOLOGS; break;
		case 'm': globalsettings |= SETTINGS_LEGACYMOTION; break;
		case 'u': globalsettings |= SETTINGS_UVSHIFTING; break;
		case 'V': globalsettings |= SETTINGS_NOVALIDATION; break;
		case 'h':
		case '?':
			globalsettings = 0;
			ShowVersion();
			ShowHelp( argv[0] );
			ret = 2;
			goto end;
		case 'v':
			globalsettings = 0;
			ShowVersion();
			ret = 2;
			goto end;
		}
	}

	ShowVersion();

	argc -= (optind - 1);

	if( argc == 1 )
	{
		ShowHelp( argv[0] );
		ret = 2;
		goto end;
	}
	else if( argc == 3 )
	{
		if( Q_strlen( argv[optind + 1] ) > MAX_SYSPATH - 2 )
		{
			LogPutS( "ERROR: Destination path is too long.");
			ret = 1;
			goto end;
		}

		Q_strncpy( destdir, argv[optind + 1], sizeof( destdir ));
	}

	if( !(LoadActivityList( argv[0] ) && LoadMDL( argv[optind] )))
	{
		ret = 1;
		goto end;
	}

	WriteQCScript();
	WriteSMD();
	WriteTextures();

	LogPutS( "Done." );

end:
	LogPutS( "--------------------------------------------------" );

	return ret;
}

