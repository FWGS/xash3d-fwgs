/*
smd.c - Studio Model Data format writer
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
#include "xash3d_mathlib.h"
#include "crtlib.h"
#include "studio.h"
#include "mdldec.h"
#include "utils.h"
#include "settings.h"
#include "smd.h"

static matrix3x4	*bonetransform;
static matrix3x4	*worldtransform;

/*
============
CreateBoneTransformMatrices
============
*/
static qboolean CreateBoneTransformMatrices( matrix3x4 **matrix )
{
	*matrix = calloc( model_hdr->numbones, sizeof( matrix3x4 ) );

	if( !*matrix )
	{
		LogPutS( "ERROR: Couldn't allocate memory for bone transformation matrices!");
		return false;
	}

	return true;
}

/*
============
FillBoneTransformMatrices
============
*/
static void FillBoneTransformMatrices( void )
{
	int		 i;
	mstudiobone_t	*bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex );
	matrix3x4	 bonematrix;
	vec4_t		 q;

	for( i = 0; i < model_hdr->numbones; ++i, ++bone )
	{
		AngleQuaternion( &bone->value[3], q, true );
		Matrix3x4_FromOriginQuat( bonematrix, q, bone->value );

		if( bone->parent == -1 )
		{
			Matrix3x4_Copy( bonetransform[i], bonematrix );
			continue;
		}

		Matrix3x4_ConcatTransforms( bonetransform[i], bonetransform[bone->parent], bonematrix );
	}
}

/*
============
FillWorldTransformMatrices
============
*/
static void FillWorldTransformMatrices( void )
{
	int			 i;
	mstudioboneinfo_t	*boneinfo = (mstudioboneinfo_t *)( (byte *)model_hdr + model_hdr->boneindex + model_hdr->numbones * sizeof( mstudiobone_t ) );

	for( i = 0; i < model_hdr->numbones; ++i, ++boneinfo )
		Matrix3x4_ConcatTransforms( worldtransform[i], bonetransform[i], boneinfo->poseToBone );
}

/*
============
RemoveBoneTransformMatrices
============
*/
static void RemoveBoneTransformMatrices( matrix3x4 **matrix )
{
	free( *matrix );
}

/*
============
ClipRotations
============
*/
static void ClipRotations( vec3_t angle )
{
	int i;

	for( i = 0; i < 3; i++ )
	{
		while( angle[i] >= M_PI_F )
			angle[i] -= M_PI2_F;

		while( angle[i] < -M_PI_F )
			angle[i] += M_PI2_F;
	}
}

/*
============
ProperBoneRotationZ
============
*/
static void ProperBoneRotationZ( vec_t *motion, float angle )
{
	float	tmp, rot;

	rot = DEG2RAD( angle );

	tmp = motion[0];
	motion[0] = motion[1];
	motion[1] = -tmp;

	motion[5] += rot;
}

/*
============
CalcBonePosition
============
*/
static void CalcBonePosition( mstudioanim_t *anim, mstudiobone_t *bone, vec_t *motion, int frame )
{
	int			 i, j;
	float			 value;
	mstudioanimvalue_t	*animvalue;

	for( i = 0; i < 6; i++ )
	{
		motion[i] = bone->value[i];

		if( !anim->offset[i] )
			continue;

		animvalue = (mstudioanimvalue_t *)( (byte *)anim + anim->offset[i] );

		j = frame;

		while( animvalue->num.total <= j )
		{
			j -= animvalue->num.total;
			animvalue += animvalue->num.valid + 1;
		}

		if( animvalue->num.valid > j )
			value = animvalue[j + 1].value;
		else
			value = animvalue[animvalue->num.valid].value;

		motion[i] += value * bone->scale[i];
	}
}

/*
============
WriteNodes
============
*/
static void WriteNodes( FILE *fp )
{
	int		 i;
	mstudiobone_t	*bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex );

	fputs( "nodes\n", fp );

	for( i = 0; i < model_hdr->numbones; ++i, ++bone )
		fprintf( fp, "%3i \"%s\" %i\n", i, bone->name, bone->parent );

	fputs( "end\n", fp );
}

/*
============
WriteSkeleton
============
*/
static void WriteSkeleton( FILE *fp )
{
	int		 i, j;
	mstudiobone_t	*bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex );

	fputs( "skeleton\n", fp );
	fputs( "time 0\n", fp );

	for( i = 0; i < model_hdr->numbones; ++i, ++bone )
	{
		fprintf( fp, "%3i", i );

		for( j = 0; j < 6; j++ )
			fprintf( fp, " %f", bone->value[j] );

		fputs( "\n", fp );
	}

	fputs( "end\n", fp );
}

/*
============
WriteTriangleInfo
============
*/
static void WriteTriangleInfo( FILE *fp, mstudiomodel_t *model, mstudiotexture_t *texture, mstudiotrivert_t **triverts, qboolean isevenstrip )
{
	int			 i, j, k, l, index;
	int			 vert_index;
	int			 norm_index;
	int			 bone_index;
	int			 valid_bones;
	int			 width, height;
	float			 u, v;
	byte			*vertbone;
	vec3_t			*studioverts;
	vec3_t			*studionorms;
	vec3_t			 vert, norm;
	float			 weights[MAXSTUDIOBONEWEIGHTS], oldweight, totalweight;
	matrix3x4		 bonematrix[MAXSTUDIOBONEWEIGHTS], skinmatrix, *pskinmatrix;
	mstudioboneweight_t	*studioboneweights;
	char			 buffer[64];

	vertbone    = ( (byte *)model_hdr + model->vertinfoindex );
	studioverts = (vec3_t *)( (byte *)model_hdr + model->vertindex );
	studionorms = (vec3_t *)( (byte *)model_hdr + model->normindex );
	studioboneweights = (mstudioboneweight_t *)( (byte *)model_hdr + model->blendvertinfoindex );

	Q_strncpy( buffer, texture->name, sizeof( buffer ));

	// Many filesystems couldn't write files if "#" is first character in the name.
	if( buffer[0] == '#' ) buffer[0] = 's';

	fprintf( fp, "%s\n", buffer );

	for( i = 0; i < 3; i++ )
	{
		index = isevenstrip ? ( i + 1 ) % 3 : i;
		vert_index = triverts[index]->vertindex;
		norm_index = triverts[index]->normindex;
		bone_index = vertbone[vert_index];

		if( model_hdr->flags & STUDIO_HAS_BONEWEIGHTS )
		{
			valid_bones = 0, totalweight = 0;
			memset( skinmatrix, 0, sizeof( matrix3x4 ) );

			for( j = 0; j < MAXSTUDIOBONEWEIGHTS; ++j )
				if( studioboneweights[vert_index].bone[j] != -1 )
					valid_bones++;

			for( j = 0; j < valid_bones; ++j )
			{
				Matrix3x4_Copy( bonematrix[j], worldtransform[studioboneweights[vert_index].bone[j]] );
				weights[j] = studioboneweights[vert_index].weight[j] / 255.0f;
				totalweight += weights[j];
			}

			oldweight = weights[0];

			if( totalweight < 1.0f )
				weights[0] += 1.0f - totalweight;

			for( j = 0; j < valid_bones; ++j )
				for( k = 0; k < 3; ++k )
					for( l = 0; l < 4; ++l )
						skinmatrix[k][l] += bonematrix[j][k][l] * weights[j];

			pskinmatrix = &skinmatrix;
		}
		else
			pskinmatrix = &bonetransform[bone_index];

		Matrix3x4_VectorTransform( *pskinmatrix, studioverts[vert_index], vert );
		Matrix3x4_VectorRotate( *pskinmatrix, studionorms[norm_index], norm );
		VectorNormalize( norm );

		if( texture->flags & STUDIO_NF_UV_COORDS )
		{
			u = HalfToFloat( triverts[index]->s );
			v = -HalfToFloat( triverts[index]->t );
		}
		else if( texture->width == 1 || texture->height == 1 )
		{
			if( texture->name[0] == '#' )
			{
				Q_strncpy( buffer, &texture->name[1], 4 );
				width = Q_atoi( buffer );

				Q_strncpy( buffer, &texture->name[4], 4 );
				height = Q_atoi( buffer );

				u = (float)triverts[index]->s / width;
				v = 1.0f - (float)triverts[index]->t / height;
			}
			else
			{
				u = (float)triverts[index]->s;
				v = 1.0f - (float)triverts[index]->t;
			}
		}
		else if( globalsettings & SETTINGS_UVSHIFTING )
		{
			u = (float)triverts[index]->s / texture->width;
			v = 1.0f - (float)triverts[index]->t / texture->height;
		}
		else
		{
			u = (float)triverts[index]->s / ( texture->width - 1 );
			v = 1.0f - (float)triverts[index]->t / ( texture->height - 1 );
		}

		fprintf( fp, "%3i %f %f %f %f %f %f %f %f",
		    bone_index,
		    vert[0], vert[1], vert[2],
		    norm[0], norm[1], norm[2],
		    u, v );

		if( model_hdr->flags & STUDIO_HAS_BONEWEIGHTS )
		{
			fprintf( fp, " %d", valid_bones );

			weights[0] = oldweight;

			for( j = 0; j < valid_bones; ++j )
				fprintf( fp, " %d %f",
				    studioboneweights[vert_index].bone[j],
				    weights[j] );
		}

		fputs( "\n", fp );
	}
}

/*
============
WriteTriangles
============
*/
static void WriteTriangles( FILE *fp, mstudiomodel_t *model )
{
	int			 i, j, k;
	mstudiomesh_t		*mesh = (mstudiomesh_t *)( (byte *)model_hdr + model->meshindex );
	mstudiotexture_t	*texture;
	mstudiotrivert_t	*triverts[3];
	short			*tricmds;

	fputs( "triangles\n", fp );

	for( i = 0; i < model->nummesh; ++i, ++mesh )
	{
		tricmds = (short *)( (byte *)model_hdr + mesh->triindex );
		texture = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex ) + mesh->skinref;

		while( ( j = *( tricmds++ ) ) )
		{
			if( j >= 0 )
			{
				// triangle strip
				for( k = 0; j > 0; j--, k++, tricmds += 4 )
				{
					if( k == 0 )
					{
						triverts[0] = (mstudiotrivert_t *)tricmds;
					}
					else if( k == 1 )
					{
						triverts[2] = (mstudiotrivert_t *)tricmds;
					}
					else if( k == 2 )
					{
						triverts[1] = (mstudiotrivert_t *)tricmds;

						WriteTriangleInfo( fp, model, texture, triverts, true );
					}
					else if( k % 2 )
					{
						triverts[0] = triverts[2];
						triverts[2] = (mstudiotrivert_t *)tricmds;

						WriteTriangleInfo( fp, model, texture, triverts, false );
					}
					else
					{
						triverts[0] = triverts[1];
						triverts[1] = (mstudiotrivert_t *)tricmds;

						WriteTriangleInfo( fp, model, texture, triverts, true );
					}
				}
			}
			else
			{
				// triangle fan
				j = abs( j );

				for( k = 0; j > 0; j--, k++, tricmds += 4 )
				{
					if( k == 0 )
					{
						triverts[0] = (mstudiotrivert_t *)tricmds;
					}
					else if( k == 1 )
					{
						triverts[2] = (mstudiotrivert_t *)tricmds;
					}
					else if( k == 2 )
					{
						triverts[1] = (mstudiotrivert_t *)tricmds;

						WriteTriangleInfo( fp, model, texture, triverts, false );
					}
					else
					{
						triverts[2] = triverts[1];
						triverts[1] = (mstudiotrivert_t *)tricmds;

						WriteTriangleInfo( fp, model, texture, triverts, false );
					}
				}
			}
		}
	}

	fputs( "end\n", fp );
}

/*
============
WriteFrameInfo
============
*/
static void WriteFrameInfo( FILE *fp, mstudioanim_t *anim, mstudioseqdesc_t *seqdesc, int frame )
{
	int			 i, j;
	float			 scale;
	vec_t			 motion[6]; // x, y, z, xr, yr, zr
	mstudiobone_t		*bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex );

	fprintf( fp, "time %i\n", frame );

	for( i = 0; i < model_hdr->numbones; ++i, ++anim, ++bone )
	{
		CalcBonePosition( anim, bone, motion, frame );

		if( bone->parent == -1 )
		{
			if( seqdesc->numframes > 1 && frame > 0 )
			{
				scale = frame / (float)( seqdesc->numframes - 1 );

				VectorMA( motion, scale, seqdesc->linearmovement, motion );
			}

			ProperBoneRotationZ( motion, 270.0f );
		}

		ClipRotations( &motion[3] );

		fprintf( fp, "%3i  ", i );

		for( j = 0; j < 6; j++ )
			fprintf( fp, " %f", motion[j] );

		fputs( "\n", fp );
	}
}

/*
============
WriteAnimations
============
*/
static void WriteAnimations( FILE *fp, mstudioseqdesc_t *seqdesc, int blend )
{
	int		 i;
	mstudioanim_t	*anim;

	fputs( "skeleton\n", fp );

	anim = (mstudioanim_t *)( (byte *)anim_hdr[seqdesc->seqgroup] + seqdesc->animindex );
	anim += blend * model_hdr->numbones;

	for( i = 0; i < seqdesc->numframes; i++ )
		WriteFrameInfo( fp, anim, seqdesc, i );

	fputs( "end\n", fp );
}

/*
============
WriteReferences
============
*/
static void WriteReferences( void )
{
	int			 i, j;
	int			 len;
	FILE			*fp;
	mstudiomodel_t		*model;
	mstudiobodyparts_t	*bodypart;
	char			 filename[MAX_SYSPATH];

	if( !CreateBoneTransformMatrices( &bonetransform ) )
		return;

	FillBoneTransformMatrices();

	if( model_hdr->flags & STUDIO_HAS_BONEINFO )
	{
		if( !CreateBoneTransformMatrices( &worldtransform ) )
			return;

		FillWorldTransformMatrices();
	}

	bodypart = (mstudiobodyparts_t *)( (byte *)model_hdr + model_hdr->bodypartindex );

	for( i = 0; i < model_hdr->numbodyparts; ++i, ++bodypart )
	{
		model = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex );

		for( j = 0; j < bodypart->nummodels; ++j, ++model )
		{
			if( !Q_strncmp( model->name, "blank", 5 ) )
				continue;

			len = Q_snprintf( filename, MAX_SYSPATH, "%s%s.smd", destdir, model->name );

			if( len == -1 )
			{
				LogPrintf( "ERROR: Destination path is too long. Couldn't write %s.smd.", model->name );
				goto _fail;
			}

			fp = fopen( filename, "w" );

			if( !fp )
			{
				LogPrintf( "ERROR: Couldn't write %s.", filename );
				goto _fail;
			}

			fputs( "version 1\n", fp );

			WriteNodes( fp );
			WriteSkeleton( fp );
			WriteTriangles( fp, model );

			fclose( fp );

			LogPrintf( "Reference: %s.", filename );
		}
	}

_fail:
	RemoveBoneTransformMatrices( &bonetransform );

	if( model_hdr->flags & STUDIO_HAS_BONEINFO )
		RemoveBoneTransformMatrices( &worldtransform );
}

/*
============
WriteSequences
============
*/
static void WriteSequences( void )
{
	int			 i, j;
	int			 len, namelen, emptyplace;
	FILE			*fp;
	char			 path[MAX_SYSPATH];
	mstudioseqdesc_t	*seqdesc = (mstudioseqdesc_t *)( (byte *)model_hdr + model_hdr->seqindex );

	len = Q_snprintf( path, MAX_SYSPATH, ( globalsettings & SETTINGS_SEPARATEANIMSFOLDER ) ? "%s" SEQUENCEPATH : "%s", destdir );

	if( len == -1 || !MakeDirectory( path ))
	{
		LogPutS( "ERROR: Destination path is too long or write permission denied. Couldn't create directory for sequences." );
		return;
	}

	emptyplace = MAX_SYSPATH - len;

	for( i = 0; i < model_hdr->numseq; ++i, ++seqdesc )
	{
		for( j = 0; j < seqdesc->numblends; j++ )
		{
			if( seqdesc->numblends == 1 )
				namelen = Q_snprintf( &path[len], emptyplace, "%s.smd", seqdesc->label );
			else
				namelen = Q_snprintf( &path[len], emptyplace, "%s_blend%02i.smd", seqdesc->label, j + 1 );

			if( namelen == -1 )
			{
				LogPrintf( "ERROR: Destination path is too long. Couldn't write %s.smd.", seqdesc->label );
				return;
			}

			fp = fopen( path, "w" );

			if( !fp )
			{
				LogPrintf( "ERROR: Couldn't write %s.", path );
				return;
			}

			fputs( "version 1\n", fp );

			WriteNodes( fp );
			WriteAnimations( fp, seqdesc, j );

			fclose( fp );

			LogPrintf( "Sequence: %s.", path );
		}
	}
}

void WriteSMD( void )
{
	WriteReferences();
	WriteSequences();
}

