#include "r_local.h"

// not really draw alias models here, but use this to draw triangles


affinetridesc_t r_affinetridesc;


int           r_aliasblendcolor;


float         aliastransform[3][4];
float         aliasworldtransform[3][4];
float         aliasoldworldtransform[3][4];

float         s_ziscale;
static vec3_t s_alias_forward, s_alias_right, s_alias_up;


#define NUMVERTEXNORMALS 162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};


void R_AliasSetUpTransform( void );
void R_AliasProjectAndClipTestFinalVert( finalvert_t *fv );

void R_AliasTransformFinalVerts( int numpoints, finalvert_t *fv, dtrivertx_t *oldv, dtrivertx_t *newv );


/*
================
R_AliasCheckBBox
================
*/

#define BBOX_TRIVIAL_ACCEPT 0
#define BBOX_MUST_CLIP_XY   1
#define BBOX_MUST_CLIP_Z    2
#define BBOX_TRIVIAL_REJECT 8

static void VectorInverse( vec3_t v )
{
	v[0] = -v[0];
	v[1] = -v[1];
	v[2] = -v[2];
}

/*
================
R_SetUpWorldTransform
================
*/
void R_SetUpWorldTransform( void )
{
	int          i;
	static float viewmatrix[3][4];
	vec3_t       angles;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity
//

	s_ziscale = (float)0x8000 * (float)0x10000;
	angles[ROLL] = 0;
	angles[PITCH] = 0;
	angles[YAW] = 0;
	AngleVectors( angles, s_alias_forward, s_alias_right, s_alias_up );

// TODO: can do this with simple matrix rearrangement

	memset( aliasworldtransform, 0, sizeof( aliasworldtransform ));
	memset( aliasoldworldtransform, 0, sizeof( aliasworldtransform ));

	for( i = 0; i < 3; i++ )
	{
		aliasoldworldtransform[i][0] = aliasworldtransform[i][0] = s_alias_forward[i];
		aliasoldworldtransform[i][0] = aliasworldtransform[i][1] = -s_alias_right[i];
		aliasoldworldtransform[i][0] = aliasworldtransform[i][2] = s_alias_up[i];
	}

	aliasworldtransform[0][3] = -RI.vieworg[0];
	aliasworldtransform[1][3] = -RI.vieworg[1];
	aliasworldtransform[2][3] = -RI.vieworg[2];

	// aliasoldworldtransform[0][3] = RI.currententity->oldorigin[0]-r_origin[0];
	// aliasoldworldtransform[1][3] = RI.currententity->oldorigin[1]-r_origin[1];
	// aliasoldworldtransform[2][3] = RI.currententity->oldorigin[2]-r_origin[2];

// FIXME: can do more efficiently than full concatenation
//	memcpy( rotationmatrix, t2matrix, sizeof( rotationmatrix ) );

//	R_ConcatTransforms (t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
	VectorCopy( RI.vright, viewmatrix[0] );
	VectorCopy( RI.vup, viewmatrix[1] );
	VectorInverse( viewmatrix[1] );
	// VectorScale(viewmatrix[1], -1, viewmatrix[1]);
	VectorCopy( RI.vforward, viewmatrix[2] );

	viewmatrix[0][3] = 0;
	viewmatrix[1][3] = 0;
	viewmatrix[2][3] = 0;

//	memcpy( aliasworldtransform, rotationmatrix, sizeof( aliastransform ) );

	// R_ConcatTransforms (viewmatrix, aliasworldtransform, aliastransform);
	Matrix3x4_ConcatTransforms( aliastransform, viewmatrix, aliasworldtransform );

	aliasworldtransform[0][3] = 0;
	aliasworldtransform[1][3] = 0;
	aliasworldtransform[2][3] = 0;

	// aliasoldworldtransform[0][3] = RI.currententity->oldorigin[0];
	// aliasoldworldtransform[1][3] = RI.currententity->oldorigin[1];
	// aliasoldworldtransform[2][3] = RI.currententity->oldorigin[2];
}


/*
================
R_AliasSetUpTransform
================
*/
void R_AliasSetUpTransform( void )
{
	int          i;
	static float viewmatrix[3][4];
	vec3_t       angles;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity
//

	s_ziscale = (float)0x8000 * (float)0x10000;
	angles[ROLL] = RI.currententity->angles[ROLL];
	angles[PITCH] = RI.currententity->angles[PITCH];
	angles[YAW] = RI.currententity->angles[YAW];
	AngleVectors( angles, s_alias_forward, s_alias_right, s_alias_up );

// TODO: can do this with simple matrix rearrangement

	memset( aliasworldtransform, 0, sizeof( aliasworldtransform ));
	memset( aliasoldworldtransform, 0, sizeof( aliasworldtransform ));

	for( i = 0; i < 3; i++ )
	{
		aliasoldworldtransform[i][0] = aliasworldtransform[i][0] = s_alias_forward[i];
		aliasoldworldtransform[i][0] = aliasworldtransform[i][1] = -s_alias_right[i];
		aliasoldworldtransform[i][0] = aliasworldtransform[i][2] = s_alias_up[i];
	}

	aliasworldtransform[0][3] = RI.currententity->origin[0] - RI.vieworg[0];
	aliasworldtransform[1][3] = RI.currententity->origin[1] - RI.vieworg[1];
	aliasworldtransform[2][3] = RI.currententity->origin[2] - RI.vieworg[2];

	// aliasoldworldtransform[0][3] = RI.currententity->oldorigin[0]-r_origin[0];
	// aliasoldworldtransform[1][3] = RI.currententity->oldorigin[1]-r_origin[1];
	// aliasoldworldtransform[2][3] = RI.currententity->oldorigin[2]-r_origin[2];

// FIXME: can do more efficiently than full concatenation
//	memcpy( rotationmatrix, t2matrix, sizeof( rotationmatrix ) );

//	R_ConcatTransforms (t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
	VectorCopy( RI.vright, viewmatrix[0] );
	VectorCopy( RI.vup, viewmatrix[1] );
	VectorInverse( viewmatrix[1] );
	// VectorScale(viewmatrix[1], -1, viewmatrix[1]);
	VectorCopy( RI.vforward, viewmatrix[2] );

	viewmatrix[0][3] = 0;
	viewmatrix[1][3] = 0;
	viewmatrix[2][3] = 0;

//	memcpy( aliasworldtransform, rotationmatrix, sizeof( aliastransform ) );

	// R_ConcatTransforms (viewmatrix, aliasworldtransform, aliastransform);
	Matrix3x4_ConcatTransforms( aliastransform, viewmatrix, aliasworldtransform );

	aliasworldtransform[0][3] = RI.currententity->origin[0];
	aliasworldtransform[1][3] = RI.currententity->origin[1];
	aliasworldtransform[2][3] = RI.currententity->origin[2];

	// aliasoldworldtransform[0][3] = RI.currententity->oldorigin[0];
	// aliasoldworldtransform[1][3] = RI.currententity->oldorigin[1];
	// aliasoldworldtransform[2][3] = RI.currententity->oldorigin[2];
}

/*
================
R_AliasProjectAndClipTestFinalVert
================
*/
void R_AliasProjectAndClipTestFinalVert( finalvert_t *fv )
{
	float zi;
	float x, y, z;

	// project points
	x = fv->xyz[0];
	y = fv->xyz[1];
	z = fv->xyz[2];
	zi = 1.0f / z;

	fv->zi = zi * s_ziscale;

	fv->u = ( x * aliasxscale * zi ) + aliasxcenter;
	fv->v = ( y * aliasyscale * zi ) + aliasycenter;

	if( fv->u < RI.aliasvrect.x )
		fv->flags |= ALIAS_LEFT_CLIP;
	if( fv->v < RI.aliasvrect.y )
		fv->flags |= ALIAS_TOP_CLIP;
	if( fv->u > RI.aliasvrectright )
		fv->flags |= ALIAS_RIGHT_CLIP;
	if( fv->v > RI.aliasvrectbottom )
		fv->flags |= ALIAS_BOTTOM_CLIP;
}

void R_SetupFinalVert( finalvert_t *fv, float x, float y, float z, int light, int s, int t )
{
	vec3_t v = {x, y, z};

	fv->xyz[0] = DotProduct( v, aliastransform[0] ) + aliastransform[0][3];
	fv->xyz[1] = DotProduct( v, aliastransform[1] ) + aliastransform[1][3];
	fv->xyz[2] = DotProduct( v, aliastransform[2] ) + aliastransform[2][3];

	fv->flags = 0;

	fv->l = light;

	if( fv->xyz[2] < ALIAS_Z_CLIP_PLANE )
	{
		fv->flags |= ALIAS_Z_CLIP;
	}
	else
	{
		R_AliasProjectAndClipTestFinalVert( fv );
	}

	fv->s = s << 16;
	fv->t = t << 16;
}

void R_RenderTriangle( finalvert_t *fv1, finalvert_t *fv2, finalvert_t *fv3 )
{

	if( fv1->flags & fv2->flags & fv3->flags )
		return; // completely clipped

	if( !( fv1->flags | fv2->flags | fv3->flags ))
	{ // totally unclipped
		aliastriangleparms.a = fv1;
		aliastriangleparms.b = fv2;
		aliastriangleparms.c = fv3;

		R_DrawTriangle();
	}
	else
	{ // partially clipped
		R_AliasClipTriangle( fv1, fv2, fv3 );
	}
}



