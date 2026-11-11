/*
r_gx_water.c - Lost Coast water material fade (water_ati_cheap001)

Source's cheap water replaces the full reflection with a cheaper one as the
camera moves away: full quality inside $CHEAPWATERSTARTDISTANCE (500), cheap
beyond $CHEAPWATERENDDISTANCE (1000). On Flipper the reflection is the same
IndTex/env stage either way, so the fade is applied as vertex alpha: the
reflection and the fog-colour body fade toward a dimmer, flatter cheap water.

Kept as its own file so the water emitter in r_gx_world.c only touches one
call site.
*/

#include "r_local.h"

#if XASH_GAMECUBE

#include <gccore.h>
#include <ogc/gx.h>

extern void GC_GetWaterFaceFade( int index, float *start, float *end );

/* Alpha of the full water (fog body + reflection) at distance d. */
#define GC_WATER_ALPHA_NEAR	0xD0u
#define GC_WATER_ALPHA_FAR	0x90u

/*
 * RGBA8888 vertex tint for one water vertex.
 * base: face tint (fog colour, alpha already set by the bake loader).
 */
u32 R_GXWaterVertTint( u32 base, const float *pt, const float *eye, int face )
{
	float start, end, dx, dy, dz, d, t;
	u32 a, r;

	GC_GetWaterFaceFade( face, &start, &end );
	dx = pt[0] - eye[0];
	dy = pt[1] - eye[1];
	dz = pt[2] - eye[2];
	d = sqrtf( dx * dx + dy * dy + dz * dz );
	if( end <= start )
		return base;
	t = ( d - start ) / ( end - start );
	if( t < 0.0f ) t = 0.0f;
	if( t > 1.0f ) t = 1.0f;
	a = (u32)( (float)GC_WATER_ALPHA_NEAR + ( (float)GC_WATER_ALPHA_FAR - (float)GC_WATER_ALPHA_NEAR ) * t );
	r = base & 0xFFFFFF00u;
	return r | a;
}

#endif /* XASH_GAMECUBE */
