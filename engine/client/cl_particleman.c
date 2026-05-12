#include "common.h"
#include "client.h"
#include "cl_tent.h"

typedef enum
{
	PM_WEATHER_NONE = 0,
	PM_WEATHER_SNOW,
} pm_weather_type_t;

typedef struct
{
	qboolean enabled;
	float color[3];
	float start;
	float end;
	float density;
	qboolean skybox;
} pm_fog_state_t;

typedef struct
{
	pm_weather_type_t type;
	qboolean enabled;
	float spawn_rate;
	float accumulator;
	float speed;
	float drift;
	byte color[3];
} pm_weather_state_t;

static CVAR_DEFINE( cl_particleman, "cl_particleman", "0", FCVAR_READ_ONLY, "enable built-in GoldSrc particleman compatibility effects" );
static pm_weather_state_t cl_pmweather;
static pm_fog_state_t cl_pmfog;

static qboolean PM_IsSkyTexture( const char *name )
{
	if( COM_StringEmpty( name ))
		return false;

	return !Q_strnicmp( name, "sky", 3 ) || !Q_strnicmp( name, "skycull", 7 );
}

static void PM_ParseRenderColor( const char *value, byte color[3] )
{
	vec3_t parsed;

	if( COM_StringEmpty( value ))
		return;

	Q_atov( parsed, value, 3 );
	color[0] = bound( 0, (int)parsed[0], 255 );
	color[1] = bound( 0, (int)parsed[1], 255 );
	color[2] = bound( 0, (int)parsed[2], 255 );
}

static void PM_ParseRenderColorFloat( const char *value, float color[3] )
{
	vec3_t parsed;

	if( COM_StringEmpty( value ))
		return;

	Q_atov( parsed, value, 3 );
	color[0] = bound( 0.0f, parsed[0], 255.0f );
	color[1] = bound( 0.0f, parsed[1], 255.0f );
	color[2] = bound( 0.0f, parsed[2], 255.0f );
}

static void PM_SpawnSnowResidue( const vec3_t origin )
{
	int i;

	for( i = 0; i < 3; i++ )
	{
		particle_t *p = R_AllocParticle( NULL );

		if( !p )
			return;

		p->type = pt_slowgrav;
		p->die = cl.time + COM_RandomFloat( 0.18f, 0.32f );
		p->color = R_LookupColor( 235, 235, 235 );
		p->org[0] = origin[0] + COM_RandomFloat( -2.0f, 2.0f );
		p->org[1] = origin[1] + COM_RandomFloat( -2.0f, 2.0f );
		p->org[2] = origin[2] + COM_RandomFloat( 0.0f, 2.0f );
		p->vel[0] = COM_RandomFloat( -10.0f, 10.0f );
		p->vel[1] = COM_RandomFloat( -10.0f, 10.0f );
		p->vel[2] = COM_RandomFloat( 10.0f, 24.0f );
	}
}

static void PM_SnowThink( particle_t *p, float frametime )
{
	vec3_t oldorg, nextorg;
	pmtrace_t tr;
	float phase;

	if( !p )
		return;

	phase = p->ramp + frametime;
	VectorCopy( p->org, oldorg );
	VectorCopy( oldorg, nextorg );

	nextorg[0] += ( p->vel[0] + sin( phase * 3.0f ) * cl_pmweather.drift ) * frametime;
	nextorg[1] += ( p->vel[1] + cos( phase * 2.0f ) * cl_pmweather.drift ) * frametime;
	nextorg[2] += p->vel[2] * frametime;

	tr = CL_TraceLine( oldorg, nextorg, PM_STUDIO_IGNORE|PM_GLASS_IGNORE|PM_WORLD_ONLY );

	if( tr.fraction < 1.0f )
	{
		PM_SpawnSnowResidue( tr.endpos );
		p->die = -1.0f;
		return;
	}

	VectorCopy( nextorg, p->org );
	p->ramp = phase;

	if( p->org[2] < ( cl.simorg[2] - 192.0f ))
		p->die = -1.0f;
}

static qboolean PM_FindSkySpawn( vec3_t out )
{
	int attempts;

	for( attempts = 0; attempts < 6; attempts++ )
	{
		vec3_t top, bottom;
		const char *texname;

		out[0] = refState.vieworg[0] + COM_RandomFloat( -512.0f, 512.0f );
		out[1] = refState.vieworg[1] + COM_RandomFloat( -512.0f, 512.0f );
		out[2] = refState.vieworg[2] + COM_RandomFloat( 224.0f, 480.0f );

		VectorCopy( out, top );
		VectorCopy( out, bottom );
		top[2] += 4096.0f;
		bottom[2] -= 512.0f;

		texname = PM_CL_TraceTexture( 0, top, bottom );
		if( PM_IsSkyTexture( texname ))
			return true;
	}

	return false;
}

static void PM_SpawnSnowParticle( void )
{
	particle_t *p;
	vec3_t origin;
	float base_speed;

	if( !PM_FindSkySpawn( origin ))
		return;

	p = R_AllocParticle( PM_SnowThink );
	if( !p )
		return;

	base_speed = cl_pmweather.speed + COM_RandomFloat( -30.0f, 30.0f );

	VectorCopy( origin, p->org );
	p->color = R_LookupColor( cl_pmweather.color[0], cl_pmweather.color[1], cl_pmweather.color[2] );
	p->vel[0] = COM_RandomFloat( -18.0f, 18.0f );
	p->vel[1] = COM_RandomFloat( -18.0f, 18.0f );
	p->vel[2] = -base_speed;
	p->die = cl.time + 8.0f;
	p->ramp = COM_RandomFloat( 0.0f, (float)M_PI * 2.0f );
}

static void PM_ParseCompatEntity( char **pdata )
{
	char token[2048];
	char keyname[256];
	char classname[64];
	byte color[3] = { 255, 255, 255 };
	float burst_size = 0.0f;
	float update_time = 0.0f;
	float drip_speed = 170.0f;
	float fog_color[3] = { 0.0f, 0.0f, 0.0f };
	float fog_density = 0.0f;
	float fog_start = 0.0f;
	float fog_end = 0.0f;
	int spawnflags = 0;

	classname[0] = '\0';

	while( 1 )
	{
		if(( *pdata = COM_ParseFile( *pdata, token, sizeof( token ))) == NULL )
			return;

		if( token[0] == '}' )
			break;

		Q_strncpy( keyname, token, sizeof( keyname ));

		if(( *pdata = COM_ParseFile( *pdata, token, sizeof( token ))) == NULL )
			return;

		if( !Q_stricmp( keyname, "classname" ))
		{
			Q_strncpy( classname, token, sizeof( classname ));
		}
		else if( !Q_stricmp( keyname, "spawnflags" ))
		{
			spawnflags = Q_atoi( token );
		}
		else if( !Q_stricmp( keyname, "rendercolor" ))
		{
			PM_ParseRenderColor( token, color );
			PM_ParseRenderColorFloat( token, fog_color );
		}
		else if( !Q_stricmp( keyname, "m_burstSize" ))
		{
			burst_size = Q_atof( token );
		}
		else if( !Q_stricmp( keyname, "m_flUpdateTime" ))
		{
			update_time = Q_atof( token );
		}
		else if( !Q_stricmp( keyname, "m_dripSpeed" ))
		{
			drip_speed = Q_atof( token );
		}
		else if( !Q_stricmp( keyname, "fogcolor" ))
		{
			PM_ParseRenderColorFloat( token, fog_color );
		}
		else if( !Q_stricmp( keyname, "density" ) || !Q_stricmp( keyname, "fogdensity" ))
		{
			fog_density = bound( 0.0f, Q_atof( token ), 1.0f );
		}
		else if( !Q_stricmp( keyname, "startdist" ) || !Q_stricmp( keyname, "start" ))
		{
			fog_start = Q_atof( token );
		}
		else if( !Q_stricmp( keyname, "enddist" ) || !Q_stricmp( keyname, "end" ))
		{
			fog_end = Q_atof( token );
		}
	}

	if( !Q_stricmp( classname, "env_fog" ) && !( spawnflags & 1 ))
	{
		cl_pmfog.enabled = true;
		cl_pmfog.color[0] = fog_color[0];
		cl_pmfog.color[1] = fog_color[1];
		cl_pmfog.color[2] = fog_color[2];
		cl_pmfog.density = fog_density;
		cl_pmfog.start = fog_start;
		cl_pmfog.end = fog_end;

		if( VectorIsNull( cl_pmfog.color ))
		{
			cl_pmfog.color[0] = 128.0f;
			cl_pmfog.color[1] = 128.0f;
			cl_pmfog.color[2] = 128.0f;
		}

		if( cl_pmfog.end <= cl_pmfog.start )
		{
			cl_pmfog.start = 0.0f;
			cl_pmfog.end = 4096.0f;
		}

		cl_pmfog.skybox = true;
	}

	if(( !Q_stricmp( classname, "env_snow" ) || !Q_stricmp( classname, "func_snow" )) && !( spawnflags & 1 ))
	{
		cl_pmweather.type = PM_WEATHER_SNOW;
		cl_pmweather.enabled = true;
		cl_pmweather.speed = Q_max( 80.0f, drip_speed );
		cl_pmweather.drift = 18.0f;
		cl_pmweather.color[0] = color[0];
		cl_pmweather.color[1] = color[1];
		cl_pmweather.color[2] = color[2];

		if( burst_size > 0.0f && update_time > 0.0f )
			cl_pmweather.spawn_rate = Q_max( burst_size / update_time, 6.0f );
		else cl_pmweather.spawn_rate = 48.0f;
	}
}

void CL_ParticleManInit( void )
{
	Cvar_RegisterVariable( &cl_particleman );
	Cvar_DirectSet( &cl_particleman, Sys_CheckParm( "-particleman" ) ? "1" : "0" );
}

void CL_ParticleManClear( void )
{
	memset( &cl_pmweather, 0, sizeof( cl_pmweather ));
	memset( &cl_pmfog, 0, sizeof( cl_pmfog ));
}

void CL_ParticleManNewMap( void )
{
	char *data;
	char token[2048];

	CL_ParticleManClear();

	if( !cl_particleman.value || !cl.worldmodel || COM_StringEmpty( cl.worldmodel->entities ))
		return;

	data = cl.worldmodel->entities;

	while(( data = COM_ParseFile( data, token, sizeof( token ))) != NULL )
	{
		if( token[0] != '{' )
			continue;

		PM_ParseCompatEntity( &data );

		if( cl_pmweather.enabled && cl_pmfog.enabled )
			break;
	}
}

void CL_ParticleManUpdate( void )
{
	int count;
	float frametime;

	if( !cl_pmweather.enabled || cl_pmweather.type != PM_WEATHER_SNOW )
		return;

	frametime = cl_clientframetime();
	if( frametime <= 0.0f || frametime > 0.1f )
		return;

	cl_pmweather.accumulator += cl_pmweather.spawn_rate * frametime;
	count = bound( 0, (int)cl_pmweather.accumulator, 24 );
	cl_pmweather.accumulator -= count;

	while( count-- > 0 )
		PM_SpawnSnowParticle();
}

void CL_ParticleManApplyFog( const ref_viewpass_t *rvp )
{
	if( !cl_particleman.value || !cl_pmfog.enabled || !rvp )
		return;

	if( FBitSet( rvp->flags, RF_DRAW_OVERVIEW|RF_DRAW_CUBEMAP|RF_ONLY_CLIENTDRAW ))
		return;

	if( gTriApi.FogParams )
		gTriApi.FogParams( cl_pmfog.density, cl_pmfog.skybox );

	if( gTriApi.Fog )
		gTriApi.Fog( cl_pmfog.color, cl_pmfog.start, cl_pmfog.end, true );
}
