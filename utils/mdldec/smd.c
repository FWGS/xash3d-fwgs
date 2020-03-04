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
#include "smd.h"

static matrix3x4	*bonetransform;

/*
============
CreateBoneTransformMatrices        
============
*/
static qboolean CreateBoneTransformMatrices( void )
{
	bonetransform = calloc( model_hdr->numbones, sizeof( matrix3x4 ) );

	if( !bonetransform )
	{
		fputs( "ERROR: Couldn't allocate memory for bone transformation matrices!\n", stderr );
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
	mstudiobone_t	*bone;
	matrix3x4	 bonematrix;
	vec4_t		 q;

	for( i = 0; i < model_hdr->numbones; i++ )
	{
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + i;

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
RemoveBoneTransformMatrices
============
*/
static void RemoveBoneTransformMatrices( void )
{
	free( bonetransform );
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
static void ProperBoneRotationZ( mstudioseqdesc_t *seqdesc, vec_t *motion, int frame, float angle )
{
	int	i;
	float	c, s, x, y;
	float	rot;

	for( i = 0; i < 3; i++ )
		 motion[i] += frame * 1.0f / seqdesc->numframes * seqdesc->linearmovement[i];

	rot = DEG2RAD( angle );

	s = sin( rot );
	c = cos( rot );

	x = motion[0];
	y = motion[1];

	motion[0] = c * x - s * y;
	motion[1] = s * x + c * y;

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
	mstudiobone_t	*bone;

	fputs( "nodes\n", fp );

	for( i = 0; i < model_hdr->numbones; i++ )
	{
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + i;

		fprintf( fp, "%3i \"%s\" %i\n", i, bone->name, bone->parent );
	}

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
	mstudiobone_t	*bone;

	fputs( "skeleton\n", fp );
	fputs( "time 0\n", fp );

	for( i = 0; i < model_hdr->numbones; i++ )
	{
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + i;

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
	int	 i, indices[3];
	int	 vert_index;
	int	 norm_index;
	int	 bone_index; 
	float	 s, t, u, v;
	byte	*vertbone;
	vec3_t	*studioverts;
	vec3_t	*studionorms;
	vec3_t	 vert, norm;

	if( isevenstrip )
	{
		indices[0] = 1;
		indices[1] = 2;
		indices[2] = 0;
	}
	else
	{
		indices[0] = 0;
		indices[1] = 1;
		indices[2] = 2;
	}

	vertbone    = ( (byte *)model_hdr + model->vertinfoindex );
	studioverts = (vec3_t *)( (byte *)model_hdr + model->vertindex );
	studionorms = (vec3_t *)( (byte *)model_hdr + model->normindex );

	s = 1.0f / texture->width;
	t = 1.0f / texture->height;

	fprintf( fp, "%s\n", texture->name );

	for( i = 0; i < 3; i++ )
	{
		vert_index = triverts[indices[i]]->vertindex;
		norm_index = triverts[indices[i]]->normindex;
		bone_index = vertbone[vert_index];

		Matrix3x4_VectorTransform( bonetransform[bone_index], studioverts[vert_index], vert );
		Matrix3x4_VectorRotate( bonetransform[bone_index], studionorms[norm_index], norm );
		VectorNormalize( norm );		

		u = ( triverts[indices[i]]->s + 1.0f ) * s;
		v = 1.0f - triverts[indices[i]]->t * t;

		fprintf( fp, "%3i %f %f %f %f %f %f %f %f\n",
		    bone_index,
		    vert[0], vert[1], vert[2],
		    norm[0], norm[1], norm[2],
		    u, v );
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
	mstudiomesh_t		*mesh;
	mstudiotexture_t	*texture;
	mstudiotrivert_t	*triverts[3];
	short			*tricmds;

	fputs( "triangles\n", fp );

	for( i = 0; i < model->nummesh; i++ )
	{
		mesh = (mstudiomesh_t *)( (byte *)model_hdr + model->meshindex ) + i;
		tricmds = (short *)( (byte *)model_hdr + mesh->triindex );
		texture = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex ) + mesh->skinref;

		while( ( j = *( tricmds++ ) ) )
		{
			if( j >= 0 )
			{
				// triangle fan
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
				// triangle strip
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
	vec_t			 motion[6]; // x, y, z, xr, yr, zr
	mstudiobone_t		*bone;

	fprintf( fp, "time %i\n", frame );

	for( i = 0; i < model_hdr->numbones; i++, anim++ )
	{
		bone = (mstudiobone_t *)( (byte *)model_hdr + model_hdr->boneindex ) + i;

		CalcBonePosition( anim, bone, motion, frame );

		if( bone->parent == -1 )
			ProperBoneRotationZ( seqdesc, motion, frame, 270.0f );

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
	size_t			 len;
	FILE			*fp;
	mstudiomodel_t		*model;
	mstudiobodyparts_t	*bodypart;
	char			 name[64];
	char			 filename[MAX_SYSPATH];

	if( !CreateBoneTransformMatrices() )
		return;

	FillBoneTransformMatrices();

	for( i = 0; i < model_hdr->numbodyparts; i++ )
	{
		bodypart = (mstudiobodyparts_t *)( (byte *)model_hdr + model_hdr->bodypartindex ) + i;

		for( j = 0; j < bodypart->nummodels; j++ )
		{
			model = (mstudiomodel_t *)( (byte *)model_hdr + bodypart->modelindex ) + j;

			if( !Q_strncmp( model->name, "blank", 5 ) )
				continue;

			COM_FileBase( model->name, name );

			len = Q_snprintf( filename, MAX_SYSPATH, "%s%s.smd", destdir, name );

			if( len >= MAX_SYSPATH )
			{
				fprintf( stderr, "ERROR: Destination path is too long. Can't write %s.smd\n", name );
				RemoveBoneTransformMatrices();
				return;
			}

			fp = fopen( filename, "w" );

			if( !fp )
			{
				fprintf( stderr, "ERROR: Can't write %s\n", filename );
				RemoveBoneTransformMatrices();
				return;
			}

			fputs( "version 1\n", fp );

			WriteNodes( fp );
			WriteSkeleton( fp );
			WriteTriangles( fp, model );

			fclose( fp );

			printf( "Reference: %s\n", filename );
		}
	}

	RemoveBoneTransformMatrices();
}

/*
============
WriteSequences
============
*/
static void WriteSequences( void )
{
	int			 i, j;
	size_t			 len;
	FILE			*fp;
	char			 filename[MAX_SYSPATH];
	mstudioseqdesc_t	*seqdesc;

	for( i = 0; i < model_hdr->numseq; i++ )
	{
		seqdesc = (mstudioseqdesc_t *)( (byte *)model_hdr + model_hdr->seqindex ) + i;

		for( j = 0; j < seqdesc->numblends; j++ )
		{
			if( seqdesc->numblends == 1 )
				len = Q_snprintf( filename, MAX_SYSPATH, "%s%s.smd", destdir, seqdesc->label );
			else
				len = Q_snprintf( filename, MAX_SYSPATH, "%s%s_blend%i.smd", destdir, seqdesc->label, j + 1 );

			if( len >= MAX_SYSPATH )
			{
				fprintf( stderr, "ERROR: Destination path is too long. Can't write %s.smd\n", seqdesc->label );
				return;
			}

			fp = fopen( filename, "w" );

			if( !fp )
			{
				fprintf( stderr, "ERROR: Can't write %s\n", filename );
				return;
			}

			fputs( "version 1\n", fp );

			WriteNodes( fp );
			WriteAnimations( fp, seqdesc, j );

			fclose( fp );

			printf( "Sequence: %s\n", filename );
		}
	}
}

void WriteSMD( void )
{
	WriteReferences();
	WriteSequences();
}

