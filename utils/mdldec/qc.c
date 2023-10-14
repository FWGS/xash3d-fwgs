/*
qc.c - Quake C script writer
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

#include <stdlib.h>
#include <string.h>
#include "eiface.h"
#include "studio.h"
#include "crtlib.h"
#include "version.h"
#include "mdldec.h"
#include "utils.h"
#include "qc.h"

static char	**activity_names;
static int	  activity_count;

/*
============
LoadActivityList
============
*/
qboolean LoadActivityList( const char *appname )
{
	FILE		*fp;
	const char	*p;
	char		 path[MAX_SYSPATH];
	char		 buf[256];

	fp = fopen( ACTIVITIES_FILE, "r" );

	if( !fp )
	{
		p = getenv( "MDLDEC_ACT_PATH" );

		if( !p )
		{
			fprintf( stderr, "ERROR: Couldn't find file " ACTIVITIES_FILE ".\n" \
			    "Place " ACTIVITIES_FILE " beside %s or set MDLDEC_ACT_PATH environment variable.\n", appname );
			return false;
		}

		Q_strncpy( path, p, MAX_SYSPATH - 1 );

		COM_PathSlashFix( path );

		Q_strncat( path, ACTIVITIES_FILE, MAX_SYSPATH );

		fp = fopen( path, "r" );

		if( !fp )
		{
			fputs( "ERROR: Can't open file " ACTIVITIES_FILE ".\n", stderr );
			return false;
		}
	}

	while( fgets( buf, sizeof( buf ), fp ) )
	{
		activity_names = realloc( activity_names, sizeof( char* ) * ++activity_count );

		if( !activity_names )
		{
			fputs( "ERROR: Couldn't allocate memory for activities strings.\n", stderr );
			return false;
		}

		COM_RemoveLineFeed( buf );

		activity_names[activity_count - 1] = strdup( buf );

		if( !activity_names[activity_count - 1] )
		{
			fputs( "ERROR: Couldn't allocate memory for activities strings.\n", stderr );
			return false;
		}
	}

	fclose( fp );

	return true;
}

/*
============
FindActivityName
============
*/
static const char *FindActivityName( int type )
{
	if( type >= 0 && type < activity_count )
		return activity_names[type];

	return NULL;
}

/*
============
GetMotionTypeString
============
*/
static void GetMotionTypeString( int type, char *str, size_t size, qboolean is_composite )
{
	const char	*p = NULL;

	str[0] = '\0';

	if( is_composite )
	{
		if( type & STUDIO_X )
			Q_strncat( str, " X", size );

		if( type & STUDIO_Y )
			Q_strncat( str, " Y", size );

		if( type & STUDIO_Z )
			Q_strncat( str, " Z", size );

		if( type & STUDIO_XR )
			Q_strncat( str, " XR", size );

		if( type & STUDIO_YR )
			Q_strncat( str, " YR", size );

		if( type & STUDIO_ZR )
			Q_strncat( str, " ZR", size );

		if( type & STUDIO_LX )
			Q_strncat( str, " LX", size );

		if( type & STUDIO_LY )
			Q_strncat( str, " LY", size );

		if( type & STUDIO_LZ )
			Q_strncat( str, " LZ", size );

		if( type & STUDIO_LXR )
			Q_strncat( str, " LXR", size );

		if( type & STUDIO_LYR )
			Q_strncat( str, " LYR", size );

		if( type & STUDIO_LZR )
			Q_strncat( str, " LZR", size );

		if( type & STUDIO_LINEAR )
			Q_strncat( str, " LM", size );

		if( type & STUDIO_QUADRATIC_MOTION )
			Q_strncat( str, " LQ", size );

		return;
	}

	type &= STUDIO_TYPES;

	switch( type )
	{
	case STUDIO_X:    p = "X";    break;
	case STUDIO_Y:    p = "Y";    break;
	case STUDIO_Z:    p = "Z";    break;
	case STUDIO_XR:   p = "XR";   break;
	case STUDIO_YR:   p = "YR";   break;
	case STUDIO_ZR:   p = "ZR";   break;
	case STUDIO_LX:   p = "LX";   break;
	case STUDIO_LY:   p = "LY";   break;
	case STUDIO_LZ:   p = "LZ";   break;
	case STUDIO_LXR:  p = "LXR";  break;
	case STUDIO_LYR:  p = "LYR";  break;
	case STUDIO_LZR:  p = "LZR";  break;
	case STUDIO_LINEAR: p = "LM"; break;
	case STUDIO_QUADRATIC_MOTION: p = "LQ"; break;
	default: break;
	}

	if( p )
		Q_strncpy( str, p, size );
}

/*
============
WriteTextureRenderMode
============
*/
static void WriteTextureRenderMode( FILE *fp )
{
	int		  i;
	mstudiotexture_t *texture;
	long		  pos = ftell( fp );

	for( i = 0; i < texture_hdr->numtextures; i++ )
	{
		texture = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex ) + i;

		if( texture->flags & STUDIO_NF_FLATSHADE )
			fprintf( fp,"$texrendermode \"%s\" \"flatshade\" \n", texture->name ); // sven-coop extension

		if( texture->flags & STUDIO_NF_CHROME )
			fprintf( fp, "$texrendermode \"%s\" \"chrome\" \n", texture->name ); // sven-coop extension/may be added in HLMV

		if( texture->flags & STUDIO_NF_FULLBRIGHT )
			fprintf( fp, "$texrendermode \"%s\" \"fullbright\" \n", texture->name ); // sven-coop extension/xash3d extension

		if( texture->flags & STUDIO_NF_NOMIPS )
			fprintf( fp, "$texrendermode \"%s\" \"nomips\" \n", texture->name ); // sven-coop extension

		if( texture->flags & STUDIO_NF_SMOOTH )
		{
			fprintf( fp, "$texrendermode \"%s\" \"alpha\" \n", texture->name ); // sven-coop extension
			fprintf( fp, "$texrendermode \"%s\" \"smooth\" \n", texture->name ); // xash3d extension
		}

		if( texture->flags & STUDIO_NF_ADDITIVE )
			fprintf( fp, "$texrendermode \"%s\" \"additive\" \n", texture->name );

		if( texture->flags & STUDIO_NF_MASKED )
		{
			if( texture->flags & STUDIO_NF_ALPHASOLID )
				fprintf( fp, "$texrendermode \"%s\" \"masked_solid\" \n", texture->name ); // xash3d extension
			else
				fprintf( fp, "$texrendermode \"%s\" \"masked\" \n", texture->name );
		}

		if( texture->flags & STUDIO_NF_TWOSIDE )
			fprintf( fp, "$texrendermode \"%s\" \"twoside\" \n", texture->name );
	}

	if( ftell( fp ) != pos )
		fputs( "\n", fp );
}

/*
============
WriteSkinFamilyInfo
============
*/
static void WriteSkinFamilyInfo( FILE *fp )
{
	int			 i, j, k;
	short			*skinref, index;
	mstudiotexture_t	*texture;

	if( texture_hdr->numskinfamilies < 2 )
		return;

	fprintf( fp, "// %i skin families\n", texture_hdr->numskinfamilies );

	fputs( "$texturegroup skinfamilies \n{\n", fp );

	skinref = (short *)( (byte *)texture_hdr + texture_hdr->skinindex );

	for( i = 0; i < texture_hdr->numskinfamilies; ++i )
	{
		fputs( "\t{", fp );

		for( j = 0; j < texture_hdr->numskinref; ++j )
		{
			index = *( skinref + i * texture_hdr->numskinref + j );

			for( k = 0; k < texture_hdr->numskinfamilies; ++k )
			{
				if( index == *( skinref + k * texture_hdr->numskinref + j ) )
					continue;

				texture = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex ) + index;

				fprintf( fp, " \"%s\" ", texture->name );
				break;
			}
		}

		fputs( "}\n", fp );
	}

	fputs( "}\n\n", fp );
}

/*
============
WriteAttachmentInfo
============
*/
static void WriteAttachmentInfo( FILE *fp )
{
	int			 i;
	mstudioattachment_t	*attachment;
	mstudiobone_t		*bone;

	if( !model_hdr->numattachments )
		return;

	fprintf( fp, "// %i attachment(s)\n", model_hdr->numattachments );

	for( i = 0; i < model_hdr->numattachments; ++i )
	{
		attachment = (mstudioattachment_t *)( (byte *)model_hdr + model_hdr->attachmentindex ) + i;
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + attachment->bone;

		fprintf( fp, "$attachment %i \"%s\" %f %f %f\n", i, bone->name, attachment->org[0], attachment->org[1], attachment->org[2] );
	}

	fputs( "\n", fp );
}

/*
============
WriteBodyGroupInfo
============
*/
static void WriteBodyGroupInfo( FILE *fp )
{
	int			 i, j;
	mstudiobodyparts_t	*bodypart;
	mstudiomodel_t		*model;
	char			 modelname[64];

	fputs( "// reference mesh(es)\n", fp );

	for( i = 0; i < model_hdr->numbodyparts; ++i )
	{
		bodypart = (mstudiobodyparts_t *) ( (byte *)model_hdr + model_hdr->bodypartindex ) + i;

		if( bodypart->nummodels == 1 )
		{
			model = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex );

			COM_FileBase( model->name, modelname, sizeof( modelname ));

			fprintf( fp, "$body \"%s\" \"%s\"\n", bodypart->name, modelname );
			continue;
		}

		fprintf( fp, "$bodygroup \"%s\"\n", bodypart->name );

		fputs( "{\n", fp );

		for( j = 0; j < bodypart->nummodels; ++j )
		{
			model = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex ) + j;

			if( !Q_strncmp( model->name, "blank", 5 ) )
			{
				fputs( "\tblank\n", fp );
				continue;
			}

			COM_FileBase( model->name, modelname, sizeof( modelname ));

			fprintf( fp, "\tstudio \"%s\"\n", modelname );
		}

		fputs( "}\n", fp );
	}

	fputs( "\n", fp );
}

/*
============
WriteControllerInfo
============
*/
static void WriteControllerInfo( FILE *fp )
{
	int			 i;
	mstudiobonecontroller_t	*bonecontroller;
	mstudiobone_t		*bone;
	char			 motion_types[64];

	if( !model_hdr->numbonecontrollers )
		return;

	fprintf( fp, "// %i bone controller(s)\n", model_hdr->numbonecontrollers );

	for( i = 0; i < model_hdr->numbonecontrollers; ++i )
	{
		bonecontroller = (mstudiobonecontroller_t *)( (byte *)model_hdr + model_hdr->bonecontrollerindex ) + i;
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + bonecontroller->bone;

		GetMotionTypeString( bonecontroller->type & ~STUDIO_RLOOP, motion_types, sizeof( motion_types ), false );

		fprintf( fp, "$controller %i \"%s\" %s %f %f\n",
		    bonecontroller->index, bone->name, motion_types,
		    bonecontroller->start, bonecontroller->end );
	}

	fputs( "\n", fp );
}

/*
============
WriteHitBoxInfo
============
*/
static void WriteHitBoxInfo( FILE *fp )
{
	int		 i;
	mstudiobbox_t	*hitbox;
	mstudiobone_t	*bone;

	if( !model_hdr->numhitboxes )
		return;

	fprintf( fp, "// %i hit box(es)\n", model_hdr->numhitboxes );

	for( i = 0; i < model_hdr->numhitboxes; i++ )
	{
		hitbox = (mstudiobbox_t *)( (byte *)model_hdr + model_hdr->hitboxindex ) + i;
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + hitbox->bone;

		fprintf( fp, "$hbox %i \"%s\" %f %f %f %f %f %f\n",
		    hitbox->group, bone->name,
		    hitbox->bbmin[0], hitbox->bbmin[1], hitbox->bbmin[2],
		    hitbox->bbmax[0], hitbox->bbmax[1], hitbox->bbmax[2] );
	}

	fputs( "\n", fp );
}

/*
============
WriteSequenceInfo
============
*/
static void WriteSequenceInfo( FILE *fp )
{
	int			 i, j;
	const char		*activity;
	char			 motion_types[256];
	mstudioevent_t		*event;
	mstudioseqdesc_t	*seqdesc;

	if( model_hdr->numseqgroups > 1 )
		fputs( "$sequencegroupsize 64\n\n", fp );

	if( model_hdr->numseq > 0 )
		fprintf( fp, "// %i animation sequence(s)\n", model_hdr->numseq );

	for( i = 0; i < model_hdr->numseq; ++i )
	{
		seqdesc = (mstudioseqdesc_t *)( (byte *)model_hdr + model_hdr->seqindex ) + i;

		fprintf( fp, "$sequence \"%s\" ", seqdesc->label );

		if( seqdesc->numblends > 1 )
		{
			if( seqdesc->numblends > 2 )
			{
				fputs( "{\n", fp );

				for( j = 0; j < seqdesc->numblends; j++ )
				{
					fputs( "          ", fp );

					fprintf( fp, "\"%s_blend%i\" ", seqdesc->label, j + 1 );

					fputs( "\n", fp );
				}

				fputs( "          ", fp );
			}
			else
			{
				fprintf( fp, "\"%s_blend1\" ", seqdesc->label );
				fprintf( fp, "\"%s_blend2\" ", seqdesc->label );
			}

			GetMotionTypeString( seqdesc->blendtype[0], motion_types, sizeof( motion_types ), false );

			fprintf( fp, "blend %s %.0f %.0f",
			    motion_types, seqdesc->blendstart[0], seqdesc->blendend[0] );
		}
		else
		{
			fprintf( fp, "\"%s\"", seqdesc->label );
		}

		if( seqdesc->motiontype )
		{
			GetMotionTypeString( seqdesc->motiontype, motion_types, sizeof( motion_types ), true );

			fprintf( fp, "%s", motion_types );
		}

		fprintf( fp, " fps %.0f ", seqdesc->fps );

		if( seqdesc->flags == 1 )
			fputs( "loop ", fp );

		if( seqdesc->activity )
		{
			activity = FindActivityName( seqdesc->activity );

			if( activity )
			{
				fprintf( fp, "%s %i ", activity, seqdesc->actweight );
			}
			else
			{
				printf( "WARNING: Sequence %s has a custom activity flag (ACT_%i %i).\n",
				    seqdesc->label, seqdesc->activity, seqdesc->actweight );

				fprintf( fp, "ACT_%i %i ", seqdesc->activity, seqdesc->actweight );
			}
		}

		if( seqdesc->entrynode && seqdesc->exitnode )
		{
			if( seqdesc->entrynode == seqdesc->exitnode )
				fprintf( fp, "node %i ", seqdesc->entrynode );
			else if( seqdesc->nodeflags )
				fprintf( fp, "rtransition %i %i ", seqdesc->entrynode, seqdesc->exitnode );
			else
				fprintf( fp, "transition %i %i ", seqdesc->entrynode, seqdesc->exitnode );
		}

		if( seqdesc->numevents > 2 )
		{
			fputs( "{\n", fp );

			for( j = 0; j < seqdesc->numevents; j++ )
			{
				event = (mstudioevent_t *)( (byte *)model_hdr + seqdesc->eventindex ) + j;

				fprintf( fp, "\t{ event %i %i", event->event, event->frame );

				if( event->options[0] != '\0' )
					fprintf( fp, " \"%s\"", event->options );

				fputs( " }\n", fp );
			}

			fputs( "}", fp );
		}
		else
		{
			for( j = 0; j < seqdesc->numevents; j++ )
			{
				event = (mstudioevent_t *)( (byte *)model_hdr + seqdesc->eventindex ) + j;

				fprintf( fp, "{ event %i %i", event->event, event->frame );

				if( event->options[0] != '\0')
					fprintf( fp, " \"%s\"", event->options );

				fputs( " } ", fp );
			}
		}

		fputs( "\n", fp );

		if( seqdesc->numblends > 2 )
			fputs( "}\n", fp );
	}

	fputs( "\n", fp );
}

/*
============
WriteQCScript
============
*/
void WriteQCScript( void )
{
	FILE	*fp;
	char	 filename[MAX_SYSPATH];
	int	 len;

	len = Q_snprintf( filename, MAX_SYSPATH, "%s%s.qc", destdir, modelfile );

	if( len == -1 )
	{
		fprintf( stderr, "ERROR: Destination path is too long. Can't write %s.qc\n", modelfile );
		return;
	}

	fp = fopen( filename, "w" );

	if( !fp )
	{
		fprintf( stderr, "ERROR: Can't write %s\n", filename );
		return;
	}

	fputs( "/*\n", fp );
	fputs( "==============================================================================\n\n", fp );
	fputs( "QC script generated by Half-Life Studio Model Decompiler " APP_VERSION "\n", fp );

	fprintf( fp, "Copyright Flying With Gauss %s (c) \n\n", Q_timestamp( TIME_YEAR_ONLY ) );
	fprintf( fp, "%s.mdl\n\n", modelfile );

	fputs( "Original internal name:\n", fp );

	fprintf( fp, "\"%s\"\n\n", model_hdr->name );

	fputs( "==============================================================================\n", fp );
	fputs( "*/\n\n", fp );

	fprintf( fp, "$modelname \"%s.mdl\"\n", modelfile );

	fputs( "$cd \".\"\n", fp );
	fputs( "$cdtexture \".\"\n", fp );
	fputs( "$cliptotextures\n", fp );
	fputs( "$scale 1.0\n", fp );
	fputs( "\n", fp );

	WriteBodyGroupInfo( fp );

	fprintf( fp, "$flags %i\n\n", model_hdr->flags );
	fprintf( fp, "$eyeposition %f %f %f\n\n", model_hdr->eyeposition[0], model_hdr->eyeposition[1], model_hdr->eyeposition[2] );

	if( !model_hdr->numtextures )
		fputs( "$externaltextures\n\n", fp );

	WriteSkinFamilyInfo( fp );
	WriteTextureRenderMode( fp );
	WriteAttachmentInfo( fp );

	fprintf( fp, "$bbox %f %f %f", model_hdr->min[0], model_hdr->min[1], model_hdr->min[2] );
	fprintf( fp, " %f %f %f\n\n", model_hdr->max[0], model_hdr->max[1], model_hdr->max[2] );
	fprintf( fp, "$cbox %f %f %f", model_hdr->bbmin[0], model_hdr->bbmin[1], model_hdr->bbmin[2] );
	fprintf( fp, " %f %f %f\n\n", model_hdr->bbmax[0], model_hdr->bbmax[1], model_hdr->bbmax[2] );

	WriteHitBoxInfo( fp );
	WriteControllerInfo( fp );
	WriteSequenceInfo( fp );

	fputs( "// End of QC script.\n", fp );
	fclose( fp );

	printf( "QC Script: %s\n", filename );
}

