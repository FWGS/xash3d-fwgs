#include "vk_lightmap.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_cvar.h"

#include "com_strings.h"
#include "xash3d_mathlib.h"
#include "protocol.h"

#include <memory.h>

typedef struct
{
	int		allocated[BLOCK_SIZE_MAX];
	int		current_lightmap_texture;
	//msurface_t	*dynamic_surfaces;
	//msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];
	byte		lightmap_buffer[BLOCK_SIZE_MAX*BLOCK_SIZE_MAX*4];

	int		lightstylevalue[MAX_LIGHTSTYLES];	// value 0 - 65536
} gllightmapstate_t;

static gllightmapstate_t gl_lms;

// TODO this doesn't really need to be this huge
static uint		r_blocklights[BLOCK_SIZE_MAX*BLOCK_SIZE_MAX*3]; // This is just a temp HDR-ish buffer for lightmap generation

static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ));
}

static int LM_AllocBlock( int w, int h, int *x, int *y )
{
	int	i, j;
	int	best, best2;

	best = BLOCK_SIZE;

	for( i = 0; i < BLOCK_SIZE - w; i++ )
	{
		best2 = 0;

		for( j = 0; j < w; j++ )
		{
			if( gl_lms.allocated[i+j] >= best )
				break;
			if( gl_lms.allocated[i+j] > best2 )
				best2 = gl_lms.allocated[i+j];
		}

		if( j == w )
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if( best + h > BLOCK_SIZE )
		return false;

	for( i = 0; i < w; i++ )
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

static void LM_UploadDynamicBlock( void )
{
	int	height = 0, i;

	for( i = 0; i < BLOCK_SIZE; i++ )
	{
		if( gl_lms.allocated[i] > height )
			height = gl_lms.allocated[i];
	}

	gEngine.Con_Printf(S_ERROR "VK NOT IMPLEMENTED %s\n", __FUNCTION__);
	//pglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, BLOCK_SIZE, height, GL_RGBA, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffer );
}

static void LM_UploadBlock( qboolean dynamic )
{
	int	i;

	if( dynamic )
	{
		int	height = 0;

		for( i = 0; i < BLOCK_SIZE; i++ )
		{
			if( gl_lms.allocated[i] > height )
				height = gl_lms.allocated[i];
		}

		gEngine.Con_Printf(S_ERROR "VK NOT IMPLEMENTED %s dynamic \n", __FUNCTION__);
		/* GL_Bind( XASH_TEXTURE0, gl_lms.dlightTexture ); */
		/* pglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, BLOCK_SIZE, height, GL_RGBA, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffer ); */
	}
	else
	{
		rgbdata_t	r_lightmap;
		char	lmName[16];

		i = gl_lms.current_lightmap_texture;

		// upload static lightmaps only during loading
		memset( &r_lightmap, 0, sizeof( r_lightmap ));
		Q_snprintf( lmName, sizeof( lmName ), "*lightmap%i", i );

		r_lightmap.width = BLOCK_SIZE;
		r_lightmap.height = BLOCK_SIZE;
		r_lightmap.type = PF_RGBA_32;
		r_lightmap.size = r_lightmap.width * r_lightmap.height * 4;
		r_lightmap.flags = IMAGE_HAS_COLOR;
		r_lightmap.buffer = gl_lms.lightmap_buffer;
		tglob.lightmapTextures[i] = VK_LoadTextureInternal( lmName, &r_lightmap, TF_FONT|TF_ATLAS_PAGE|TF_NOMIPMAP );

		if( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
			gEngine.Host_Error( "AllocBlock: full\n" );
	}
}

/*
=================
R_BuildLightmap

Combine and scale multiple lightmaps into the floating
format in r_blocklights
=================
*/
static void R_BuildLightMap( msurface_t *surf, byte *dest, int stride, qboolean dynamic )
{
	int		smax, tmax;
	uint		*bl;
	int		i, map, size, s, t;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	color24		*lm;
	sample_size = gEngine.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;
	size = smax * tmax;

	lm = surf->samples;

	memset( r_blocklights, 0, sizeof( uint ) * size * 3 );

	// add all the lightmaps
	for( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255 && lm; map++ )
	{
		const uint scale = gl_lms.lightstylevalue[surf->styles[map]];
		for( i = 0, bl = r_blocklights; i < size; i++, bl += 3, lm++ )
		{
			bl[0] += gEngine.LightToTexGamma( lm->r ) * scale;
			bl[1] += gEngine.LightToTexGamma( lm->g ) * scale;
			bl[2] += gEngine.LightToTexGamma( lm->b ) * scale;
		}
	}

	/* TODO
	// add all the dynamic lights
	if( surf->dlightframe == gl_lms.framecount && dynamic )
		R_AddDynamicLights( surf );
	*/

	// Put into texture format
	stride -= (smax << 2);
	bl = r_blocklights;

	for( t = 0; t < tmax; t++, dest += stride )
	{
		for( s = 0; s < smax; s++ )
		{
			dest[0] = Q_min((bl[0] >> 7), 255 );
			dest[1] = Q_min((bl[1] >> 7), 255 );
			dest[2] = Q_min((bl[2] >> 7), 255 );
			dest[3] = 255;

			bl += 3;
			dest += 4;
		}
	}
}

void VK_CreateSurfaceLightmap( msurface_t *surf, const model_t *loadmodel )
{
	int		smax, tmax;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	byte		*base;

	if( !loadmodel->lightdata )
		return;

	if( FBitSet( surf->flags, SURF_DRAWTILED ))
		return;

	sample_size = gEngine.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;

	if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
	{
		LM_UploadBlock( false );
		LM_InitBlock();

		if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
			gEngine.Host_Error( "AllocBlock: full\n" );
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += ( surf->light_t * BLOCK_SIZE + surf->light_s ) * 4;

	// FIXME R_SetCacheState( surf );
	R_BuildLightMap( surf, base, BLOCK_SIZE * 4, false );
}

void VK_UploadLightmap( void )
{
	LM_UploadBlock( false );
}

void VK_ClearLightmap( void )
{
	for (int i = 0; i < gl_lms.current_lightmap_texture; ++i)
		VK_FreeTexture(tglob.lightmapTextures[i]);
	gl_lms.current_lightmap_texture = 0;

	LM_InitBlock();
}

void VK_RunLightStyles( void )
{
	int		i, k, flight, clight;
	float		l, lerpfrac, backlerp;
	float		frametime = (gpGlobals->time -   gpGlobals->oldtime);
	float		scale;
	lightstyle_t	*ls;
	const model_t *world = gEngine.pfnGetModelByIndex( 1 );

	if( !world ) return;

	scale = r_lighting_modulate->value;

	// light animations
	// 'm' is normal light, 'a' is no light, 'z' is double bright
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		ls = gEngine.GetLightStyle( i );
		if( !world->lightdata )
		{
			gl_lms.lightstylevalue[i] = 256 * 256;
			continue;
		}

		if( !gEngine.EngineGetParm( PARAM_GAMEPAUSED, 0 ) && frametime <= 0.1f )
			ls->time += frametime; // evaluate local time

		flight = (int)Q_floor( ls->time * 10 );
		clight = (int)Q_ceil( ls->time * 10 );
		lerpfrac = ( ls->time * 10 ) - flight;
		backlerp = 1.0f - lerpfrac;

		if( !ls->length )
		{
			gl_lms.lightstylevalue[i] = 256 * scale;
			continue;
		}
		else if( ls->length == 1 )
		{
			// single length style so don't bother interpolating
			gl_lms.lightstylevalue[i] = ls->map[0] * 22 * scale;
			continue;
		}
		else if( !ls->interp || !CVAR_TO_BOOL( cl_lightstyle_lerping ))
		{
			gl_lms.lightstylevalue[i] = ls->map[flight%ls->length] * 22 * scale;
			continue;
		}

		// interpolate animating light
		// frame just gone
		k = ls->map[flight % ls->length];
		l = (float)( k * 22.0f ) * backlerp;

		// upcoming frame
		k = ls->map[clight % ls->length];
		l += (float)( k * 22.0f ) * lerpfrac;

		gl_lms.lightstylevalue[i] = (int)l * scale;
	}
}
