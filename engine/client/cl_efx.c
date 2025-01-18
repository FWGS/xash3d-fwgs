

#include "common.h"
#include "client.h"
#include "customentity.h"
#include "r_efx.h"
#include "cl_tent.h"
#include "pm_local.h"
#define PART_SIZE	Q_max( 0.5f, cl_draw_particles.value )

/*
==============================================================

PARTICLES MANAGEMENT

==============================================================
*/
// particle ramps
static int ramp1[8] = { 0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61 };
static int ramp2[8] = { 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66 };
static int ramp3[6] = { 0x6d, 0x6b, 6, 5, 4, 3 };
static int gSparkRamp[9] = { 0xfe, 0xfd, 0xfc, 0x6f, 0x6e, 0x6d, 0x6c, 0x67, 0x60 };

static CVAR_DEFINE_AUTO( tracerspeed, "6000", 0, "tracer speed" );
static CVAR_DEFINE_AUTO( tracerlength, "0.8", 0, "tracer length factor" );
static CVAR_DEFINE_AUTO( traceroffset, "30", 0, "tracer starting offset" );

static particle_t	*cl_active_particles;
static particle_t	*cl_active_tracers;
static particle_t	*cl_free_particles;
static particle_t	*cl_particles = NULL;	// particle pool
static vec3_t	cl_avelocities[NUMVERTEXNORMALS];
static float	cl_lasttimewarn = 0.0f;

// expand debugging BBOX particle hulls by this many units.
#define BOX_GAP	0.0f

/*
================
R_LookupColor

find nearest color in particle palette
================
*/
short GAME_EXPORT R_LookupColor( byte r, byte g, byte b )
{
	int	i, best;
	float	diff, bestdiff;
	float	rf, gf, bf;

	bestdiff = 999999;
	best = -1;

	for( i = 0; i < 256; i++ )
	{
		rf = r - clgame.palette[i].r;
		gf = g - clgame.palette[i].g;
		bf = b - clgame.palette[i].b;

		// convert color to monochrome
		diff = rf * (rf * 0.2f) + gf * (gf * 0.5f) + bf * (bf * 0.3f);

		if ( diff < bestdiff )
		{
			bestdiff = diff;
			best = i;
		}
	}

	return best;
}

/*
================
R_GetPackedColor

in hardware mode does nothing
================
*/
void GAME_EXPORT R_GetPackedColor( short *packed, short color )
{
	if( packed ) *packed = 0;
}

/*
================
CL_InitParticles

================
*/
void CL_InitParticles( void )
{
	int	i;

	cl_particles = Mem_Calloc( cls.mempool, sizeof( particle_t ) * GI->max_particles );
	CL_ClearParticles ();

	// this is used for EF_BRIGHTFIELD
	for( i = 0; i < NUMVERTEXNORMALS; i++ )
	{
		cl_avelocities[i][0] = COM_RandomFloat( 0.0f, 2.55f );
		cl_avelocities[i][1] = COM_RandomFloat( 0.0f, 2.55f );
		cl_avelocities[i][2] = COM_RandomFloat( 0.0f, 2.55f );
	}

	Cvar_RegisterVariable( &tracerspeed );
	Cvar_RegisterVariable( &tracerlength );
	Cvar_RegisterVariable( &traceroffset );
}

/*
================
CL_ClearParticles

================
*/
void CL_ClearParticles( void )
{
	int	i;

	if( !cl_particles ) return;

	cl_free_particles = cl_particles;
	cl_active_particles = NULL;
	cl_active_tracers = NULL;

	for( i = 0; i < GI->max_particles - 1; i++ )
		cl_particles[i].next = &cl_particles[i+1];

	cl_particles[GI->max_particles-1].next = NULL;
}

/*
================
CL_FreeParticles

================
*/
void CL_FreeParticles( void )
{
	if( cl_particles )
		Mem_Free( cl_particles );
	cl_particles = NULL;
}

/*
================
CL_AllocParticleFast

unconditionally give new particle pointer from cl_free_particles
================
*/
particle_t *CL_AllocParticleFast( void )
{
	particle_t *p = NULL;

	if( cl_free_particles )
	{
		p = cl_free_particles;
		cl_free_particles = p->next;
	}

	return p;
}

/*
================
R_AllocParticle

can return NULL if particles is out
================
*/
particle_t * GAME_EXPORT R_AllocParticle( void (*callback)( particle_t*, float ))
{
	particle_t	*p;

	if( !cl_draw_particles.value )
		return NULL;

	// never alloc particles when we not in game
	if( cl_clientframetime() == 0.0 ) return NULL;

	if( !cl_free_particles )
	{
		if( cl_lasttimewarn < host.realtime )
		{
			// don't spam about overflow
			Con_DPrintf( S_ERROR "Overflow %d particles\n", GI->max_particles );
			cl_lasttimewarn = host.realtime + 1.0f;
		}
		return NULL;
	}

	p = cl_free_particles;
	cl_free_particles = p->next;
	p->next = cl_active_particles;
	cl_active_particles = p;

	// clear old particle
	p->type = pt_static;
	VectorClear( p->vel );
	VectorClear( p->org );
	p->packedColor = 0;
	p->die = cl.time;
	p->color = 0;
	p->ramp = 0;

	if( callback )
	{
		p->type = pt_clientcustom;
		p->callback = callback;
	}

	return p;
}

/*
================
R_AllocTracer

can return NULL if particles is out
================
*/
static particle_t *R_AllocTracer( const vec3_t org, const vec3_t vel, float life )
{
	particle_t	*p;

	if( !cl_draw_tracers.value )
		return NULL;

	// never alloc particles when we not in game
	if( cl_clientframetime() == 0.0 ) return NULL;

	if( !cl_free_particles )
	{
		if( cl_lasttimewarn < host.realtime )
		{
			// don't spam about overflow
			Con_DPrintf( S_ERROR "Overflow %d tracers\n", GI->max_particles );
			cl_lasttimewarn = host.realtime + 1.0f;
		}
		return NULL;
	}

	p = cl_free_particles;
	cl_free_particles = p->next;
	p->next = cl_active_tracers;
	cl_active_tracers = p;

	// clear old particle
	p->type = pt_static;
	VectorCopy( org, p->org );
	VectorCopy( vel, p->vel );
	p->die = cl.time + life;
	p->ramp = tracerlength.value;
	p->color = TRACER_COLORINDEX_DEFAULT; // select custom color
	p->packedColor = 255; // alpha

	return p;
}
/*
==============================================================

VIEWBEAMS MANAGEMENT

==============================================================
*/
static BEAM		*cl_active_beams;
static BEAM		*cl_free_beams;
static BEAM		*cl_viewbeams = NULL;		// beams pool


/*
==============================================================

BEAM ALLOCATE & PROCESSING

==============================================================
*/


/*
==============
R_BeamSetAttributes

set beam attributes
==============
*/
static void R_BeamSetAttributes( BEAM *pbeam, float r, float g, float b, float framerate, int startFrame )
{
	pbeam->frame = (float)startFrame;
	pbeam->frameRate = framerate;
	pbeam->r = r;
	pbeam->g = g;
	pbeam->b = b;
}



/*
==============
R_BeamAlloc

==============
*/
static BEAM *R_BeamAlloc( void )
{
	BEAM	*pBeam;

	if( !cl_free_beams )
		return NULL;

	pBeam = cl_free_beams;
	cl_free_beams = pBeam->next;
	memset( pBeam, 0, sizeof( *pBeam ));
	pBeam->next = cl_active_beams;
	cl_active_beams = pBeam;
	pBeam->die = cl.time;

	return pBeam;
}

/*
==============
R_BeamFree

==============
*/
static void R_BeamFree( BEAM *pBeam )
{
	// free particles that have died off.
	R_FreeDeadParticles( &pBeam->particles );

	// now link into free list;
	pBeam->next = cl_free_beams;
	cl_free_beams = pBeam;
}


/*
================
CL_InitViewBeams

================
*/
void CL_InitViewBeams( void )
{
	cl_viewbeams = Mem_Calloc( cls.mempool, sizeof( BEAM ) * GI->max_beams );
	CL_ClearViewBeams();
}

/*
================
CL_ClearViewBeams

================
*/
void CL_ClearViewBeams( void )
{
	int	i;

	if( !cl_viewbeams ) return;

	// clear beams
	cl_free_beams = cl_viewbeams;
	cl_active_beams = NULL;

	for( i = 0; i < GI->max_beams - 1; i++ )
		cl_viewbeams[i].next = &cl_viewbeams[i+1];
	cl_viewbeams[GI->max_beams - 1].next = NULL;
}

/*
================
CL_FreeViewBeams

================
*/
void CL_FreeViewBeams( void )
{
	if( cl_viewbeams )
		Mem_Free( cl_viewbeams );
	cl_viewbeams = NULL;
}

/*
==============
R_BeamGetEntity

extract entity number from index
handle user entities
==============
*/
cl_entity_t *R_BeamGetEntity( int index )
{
	if( index < 0 )
		return clgame.dllFuncs.pfnGetUserEntity( BEAMENT_ENTITY( -index ));
	return CL_GetEntityByIndex( BEAMENT_ENTITY( index ));
}

/*
==============
CL_KillDeadBeams

==============
*/
void CL_KillDeadBeams( cl_entity_t *pDeadEntity )
{
	BEAM		*pbeam;
	BEAM		*pnewlist;
	BEAM		*pnext;
	particle_t	*pHead;	// build a new list to replace cl_active_beams.

	pbeam = cl_active_beams;	// old list.
	pnewlist = NULL;		// new list.

	while( pbeam )
	{
		cl_entity_t *beament;
		pnext = pbeam->next;

		// link into new list.
		if( R_BeamGetEntity( pbeam->startEntity ) != pDeadEntity )
		{
			pbeam->next = pnewlist;
			pnewlist = pbeam;

			pbeam = pnext;
			continue;
		}

		pbeam->flags &= ~(FBEAM_STARTENTITY | FBEAM_ENDENTITY);

		if( pbeam->type != TE_BEAMFOLLOW )
		{
			// remove beam
			pbeam->die = cl.time - 0.1f;

			// kill off particles
			pHead = pbeam->particles;
			while( pHead )
			{
				pHead->die = cl.time - 0.1f;
				pHead = pHead->next;
			}

			// free the beam
			R_BeamFree( pbeam );
		}
		else
		{
			// stay active
			pbeam->next = pnewlist;
			pnewlist = pbeam;
		}

		pbeam = pnext;
	}

	// We now have a new list with the bogus stuff released.
	cl_active_beams = pnewlist;
}


/*
===============
CL_ReadLineFile_f

Optimized version of pointfile - use beams instead of particles
===============
*/
void CL_ReadLineFile_f( void )
{
	byte *afile;
	char *pfile;
	vec3_t		p1, p2;
	int		count, modelIndex;
	char		filename[MAX_QPATH];
	model_t		*model;
	string		token;

	Q_snprintf( filename, sizeof( filename ), "maps/%s.lin", clgame.mapname );
	afile = FS_LoadFile( filename, NULL, false );

	if( !afile )
	{
		Con_Printf( S_ERROR "couldn't open %s\n", filename );
		return;
	}

	Con_Printf( "Reading %s...\n", filename );

	count = 0;
	pfile = (char *)afile;
	model = CL_LoadModel( DEFAULT_LASERBEAM_PATH, &modelIndex );

	while( 1 )
	{
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		p1[0] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		p1[1] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		p1[2] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;

		if( token[0] != '-' )
		{
			Con_Printf( S_ERROR "%s is corrupted\n", filename );
			break;
		}

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		p2[0] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		p2[1] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		p2[2] = Q_atof( token );

		count++;

		if( !R_BeamPoints( p1, p2, modelIndex, 0, 2, 0, 255, 0, 0, 0, 255.0f, 0.0f, 0.0f ))
		{
			if( !model || model->type != mod_sprite )
				Con_Printf( S_ERROR "failed to load \"%s\"!\n", DEFAULT_LASERBEAM_PATH );
			else Con_Printf( S_ERROR "not enough free beams!\n" );
			break;
		}
	}

	Mem_Free( afile );

	if( count ) Con_Printf( "%i lines read\n", count );
	else Con_Printf( "map %s has no leaks!\n", clgame.mapname );
}


/*
==============
R_BeamSprite

Create a beam with sprite at the end
Valve legacy
==============
*/
static void CL_BeamSprite( vec3_t start, vec3_t end, int beamIndex, int spriteIndex )
{
	R_BeamPoints( start, end, beamIndex, 0.01f, 0.4f, 0, COM_RandomFloat( 0.5f, 0.655f ), 5.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f );
	R_TempSprite( end, vec3_origin, 0.1f, spriteIndex, kRenderTransAdd, kRenderFxNone, 0.35f, 0.01f, 0.0f );
}


/*
==============
R_BeamSetup

generic function. all beams must be
passed through this
==============
*/
static void R_BeamSetup( BEAM *pbeam, vec3_t start, vec3_t end, int modelIndex, float life, float width, float amplitude, float brightness, float speed )
{
	model_t	*sprite = CL_ModelHandle( modelIndex );

	if( !sprite ) return;

	pbeam->type = BEAM_POINTS;
	pbeam->modelIndex = modelIndex;
	pbeam->frame = 0;
	pbeam->frameRate = 0;
	pbeam->frameCount = sprite->numframes;

	VectorCopy( start, pbeam->source );
	VectorCopy( end, pbeam->target );
	VectorSubtract( end, start, pbeam->delta );

	pbeam->freq = speed * cl.time;
	pbeam->die = life + cl.time;
	pbeam->amplitude = amplitude;
	pbeam->brightness = brightness;
	pbeam->width = width;
	pbeam->speed = speed;

	if( amplitude >= 0.50f )
		pbeam->segments = VectorLength( pbeam->delta ) * 0.25f + 3.0f;	// one per 4 pixels
	else pbeam->segments = VectorLength( pbeam->delta ) * 0.075f + 3.0f;		// one per 16 pixels

	pbeam->pFollowModel = NULL;
	pbeam->flags = 0;
}

/*
==============
CL_BeamAttemptToDie

Check for expired beams
==============
*/
static qboolean CL_BeamAttemptToDie( BEAM *pBeam )
{
	Assert( pBeam != NULL );

	// premanent beams never die automatically
	if( FBitSet( pBeam->flags, FBEAM_FOREVER ))
		return false;

	if( pBeam->type == TE_BEAMFOLLOW && pBeam->particles )
	{
		// wait for all trails are dead
		return false;
	}

	// other beams
	if( pBeam->die > cl.time )
		return false;

	return true;
}

/*
==============
R_BeamKill

Remove beam attached to specified entity
and all particle trails (if this is a beamfollow)
==============
*/
void GAME_EXPORT R_BeamKill( int deadEntity )
{
	BEAM *beam;

	for( beam = cl_active_beams; beam; beam = beam->next )
	{
		if( FBitSet( beam->flags, FBEAM_STARTENTITY ) && beam->startEntity == deadEntity )
		{
			if( beam->type != TE_BEAMFOLLOW )
				beam->die = cl.time;

			ClearBits( beam->flags, FBEAM_STARTENTITY );
		}

		if( FBitSet( beam->flags, FBEAM_ENDENTITY ) && beam->endEntity == deadEntity )
		{
			beam->die = cl.time;
			ClearBits( beam->flags, FBEAM_ENDENTITY );
		}
	}
}

/*
==============
CL_ParseViewBeam

handle beam messages
==============
*/
void CL_ParseViewBeam( sizebuf_t *msg, int beamType )
{
	vec3_t	start, end;
	int	modelIndex, startFrame;
	float	frameRate, life, width;
	int	startEnt, endEnt;
	float	noise, speed;
	float	r, g, b, a;

	switch( beamType )
	{
	case TE_BEAMPOINTS:
	case TE_BEAMENTPOINT:
	case TE_BEAMENTS:
		if( beamType == TE_BEAMENTS )
		{
			startEnt = MSG_ReadShort( msg );
			endEnt = MSG_ReadShort( msg );
		}
		else
		{
			if( beamType == TE_BEAMENTPOINT )
			{
				startEnt = MSG_ReadShort( msg );
			}
			else
			{
				start[0] = MSG_ReadCoord( msg );
				start[1] = MSG_ReadCoord( msg );
				start[2] = MSG_ReadCoord( msg );
			}
			end[0] = MSG_ReadCoord( msg );
			end[1] = MSG_ReadCoord( msg );
			end[2] = MSG_ReadCoord( msg );
		}
		modelIndex = MSG_ReadShort( msg );
		startFrame = MSG_ReadByte( msg );
		frameRate = (float)MSG_ReadByte( msg ) * 0.1f;
		life = (float)MSG_ReadByte( msg ) * 0.1f;
		width = (float)MSG_ReadByte( msg ) * 0.1f;
		noise = (float)MSG_ReadByte( msg ) * 0.01f;
		r = (float)MSG_ReadByte( msg ) / 255.0f;
		g = (float)MSG_ReadByte( msg ) / 255.0f;
		b = (float)MSG_ReadByte( msg ) / 255.0f;
		a = (float)MSG_ReadByte( msg ) / 255.0f;
		speed = (float)MSG_ReadByte( msg ) * 0.1f;
		if( beamType == TE_BEAMENTS )
			R_BeamEnts( startEnt, endEnt, modelIndex, life, width, noise, a, speed, startFrame, frameRate, r, g, b );
		else if( beamType == TE_BEAMENTPOINT )
			R_BeamEntPoint( startEnt, end, modelIndex, life, width, noise, a, speed, startFrame, frameRate, r, g, b );
		else
			R_BeamPoints( start, end, modelIndex, life, width, noise, a, speed, startFrame, frameRate, r, g, b );
		break;
	case TE_LIGHTNING:
		start[0] = MSG_ReadCoord( msg );
		start[1] = MSG_ReadCoord( msg );
		start[2] = MSG_ReadCoord( msg );
		end[0] = MSG_ReadCoord( msg );
		end[1] = MSG_ReadCoord( msg );
		end[2] = MSG_ReadCoord( msg );
		life = (float)MSG_ReadByte( msg ) * 0.1f;
		width = (float)MSG_ReadByte( msg ) * 0.1f;
		noise = (float)MSG_ReadByte( msg ) * 0.01f;
		modelIndex = MSG_ReadShort( msg );
		R_BeamLightning( start, end, modelIndex, life, width, noise, 0.6f, 3.5f );
		break;
	case TE_BEAM:
		break;
	case TE_BEAMSPRITE:
		start[0] = MSG_ReadCoord( msg );
		start[1] = MSG_ReadCoord( msg );
		start[2] = MSG_ReadCoord( msg );
		end[0] = MSG_ReadCoord( msg );
		end[1] = MSG_ReadCoord( msg );
		end[2] = MSG_ReadCoord( msg );
		modelIndex = MSG_ReadShort( msg );	// beam model
		startFrame = MSG_ReadShort( msg );	// sprite model
		CL_BeamSprite( start, end, modelIndex, startFrame );
		break;
	case TE_BEAMTORUS:
	case TE_BEAMDISK:
	case TE_BEAMCYLINDER:
		start[0] = MSG_ReadCoord( msg );
		start[1] = MSG_ReadCoord( msg );
		start[2] = MSG_ReadCoord( msg );
		end[0] = MSG_ReadCoord( msg );
		end[1] = MSG_ReadCoord( msg );
		end[2] = MSG_ReadCoord( msg );
		modelIndex = MSG_ReadShort( msg );
		startFrame = MSG_ReadByte( msg );
		frameRate = (float)MSG_ReadByte( msg ) * 0.1f;
		life = (float)MSG_ReadByte( msg ) * 0.1f;
		width = (float)MSG_ReadByte( msg );
		noise = (float)MSG_ReadByte( msg ) * 0.01f;
		r = (float)MSG_ReadByte( msg ) / 255.0f;
		g = (float)MSG_ReadByte( msg ) / 255.0f;
		b = (float)MSG_ReadByte( msg ) / 255.0f;
		a = (float)MSG_ReadByte( msg ) / 255.0f;
		speed = (float)MSG_ReadByte( msg ) * 0.1f;
		R_BeamCirclePoints( beamType, start, end, modelIndex, life, width, noise, a, speed, startFrame, frameRate, r, g, b );
		break;
	case TE_BEAMFOLLOW:
		startEnt = MSG_ReadShort( msg );
		modelIndex = MSG_ReadShort( msg );
		life = (float)MSG_ReadByte( msg ) * 0.1f;
		width = (float)MSG_ReadByte( msg );
		r = (float)MSG_ReadByte( msg ) / 255.0f;
		g = (float)MSG_ReadByte( msg ) / 255.0f;
		b = (float)MSG_ReadByte( msg ) / 255.0f;
		a = (float)MSG_ReadByte( msg ) / 255.0f;
		R_BeamFollow( startEnt, modelIndex, life, width, r, g, b, a );
		break;
	case TE_BEAMRING:
		startEnt = MSG_ReadShort( msg );
		endEnt = MSG_ReadShort( msg );
		modelIndex = MSG_ReadShort( msg );
		startFrame = MSG_ReadByte( msg );
		frameRate = (float)MSG_ReadByte( msg ) * 0.1f;
		life = (float)MSG_ReadByte( msg ) * 0.1f;
		width = (float)MSG_ReadByte( msg ) * 0.1f;
		noise = (float)MSG_ReadByte( msg ) * 0.01f;
		r = (float)MSG_ReadByte( msg ) / 255.0f;
		g = (float)MSG_ReadByte( msg ) / 255.0f;
		b = (float)MSG_ReadByte( msg ) / 255.0f;
		a = (float)MSG_ReadByte( msg ) / 255.0f;
		speed = (float)MSG_ReadByte( msg ) * 0.1f;
		R_BeamRing( startEnt, endEnt, modelIndex, life, width, noise, a, speed, startFrame, frameRate, r, g, b );
		break;
	case TE_BEAMHOSE:
		break;
	case TE_KILLBEAM:
		startEnt = MSG_ReadShort( msg );
		R_BeamKill( startEnt );
		break;
	}
}


/*
==============
R_BeamEnts

Create beam between two ents
==============
*/
BEAM * GAME_EXPORT R_BeamEnts( int startEnt, int endEnt, int modelIndex, float life, float width, float amplitude, float brightness,
	float speed, int startFrame, float framerate, float r, float g, float b )
{
	cl_entity_t	*start, *end;
	BEAM		*pbeam;
	model_t		*mod;

	mod = CL_ModelHandle( modelIndex );

	// need a valid model.
	if( !mod || mod->type != mod_sprite )
		return NULL;

	start = R_BeamGetEntity( startEnt );
	end = R_BeamGetEntity( endEnt );

	if( !start || !end )
		return NULL;

	// don't start temporary beams out of the PVS
	if( life != 0 && ( !start->model || !end->model ))
		return NULL;

	pbeam = R_BeamLightning( vec3_origin, vec3_origin, modelIndex, life, width, amplitude, brightness, speed );
	if( !pbeam ) return NULL;

	pbeam->type = TE_BEAMPOINTS;
	SetBits( pbeam->flags, FBEAM_STARTENTITY | FBEAM_ENDENTITY );
	if( life == 0 ) SetBits( pbeam->flags, FBEAM_FOREVER );

	pbeam->startEntity = startEnt;
	pbeam->endEntity = endEnt;

	R_BeamSetAttributes( pbeam, r, g, b, framerate, startFrame );

	return pbeam;
}

/*
==============
R_BeamPoints

Create beam between two points
==============
*/
BEAM * GAME_EXPORT R_BeamPoints( vec3_t start, vec3_t end, int modelIndex, float life, float width, float amplitude,
	float brightness, float speed, int startFrame, float framerate, float r, float g, float b )
{
	BEAM	*pbeam;

	if( life != 0 && ref.dllFuncs.R_BeamCull( start, end, true ))
		return NULL;

	pbeam = R_BeamAlloc();
	if( !pbeam ) return NULL;

	pbeam->die = cl.time;

	if( modelIndex < 0 )
		return NULL;

	R_BeamSetup( pbeam, start, end, modelIndex, life, width, amplitude, brightness, speed );
	if( life == 0 ) SetBits( pbeam->flags, FBEAM_FOREVER );

	R_BeamSetAttributes( pbeam, r, g, b, framerate, startFrame );

	return pbeam;
}

/*
==============
R_BeamCirclePoints

Create beam cicrle
==============
*/
BEAM * GAME_EXPORT R_BeamCirclePoints( int type, vec3_t start, vec3_t end, int modelIndex, float life, float width,
	float amplitude, float brightness, float speed, int startFrame, float framerate, float r, float g, float b )
{
	BEAM	*pbeam = R_BeamLightning( start, end, modelIndex, life, width, amplitude, brightness, speed );

	if( !pbeam ) return NULL;
	pbeam->type = type;
	if( life == 0 ) SetBits( pbeam->flags, FBEAM_FOREVER );
	R_BeamSetAttributes( pbeam, r, g, b, framerate, startFrame );

	return pbeam;
}


/*
==============
R_BeamEntPoint

Create beam between entity and point
==============
*/
BEAM *GAME_EXPORT R_BeamEntPoint( int startEnt, vec3_t end, int modelIndex, float life, float width, float amplitude,
	float brightness, float speed, int startFrame, float framerate, float r, float g, float b )
{
	BEAM		*pbeam;
	cl_entity_t	*start;

	start = R_BeamGetEntity( startEnt );

	if( !start ) return NULL;

	if( life == 0 && !start->model )
		return NULL;

	pbeam = R_BeamAlloc();
	if ( !pbeam ) return NULL;

	pbeam->die = cl.time;
	if( modelIndex < 0 )
		return NULL;

	R_BeamSetup( pbeam, vec3_origin, end, modelIndex, life, width, amplitude, brightness, speed );

	pbeam->type = TE_BEAMPOINTS;
	SetBits( pbeam->flags, FBEAM_STARTENTITY );
	if( life == 0 ) SetBits( pbeam->flags, FBEAM_FOREVER );
	pbeam->startEntity = startEnt;
	pbeam->endEntity = 0;

	R_BeamSetAttributes( pbeam, r, g, b, framerate, startFrame );

	return pbeam;
}

/*
==============
R_BeamRing

Create beam between two ents
==============
*/
BEAM * GAME_EXPORT R_BeamRing( int startEnt, int endEnt, int modelIndex, float life, float width, float amplitude, float brightness,
	float speed, int startFrame, float framerate, float r, float g, float b )
{
	BEAM		*pbeam;
	cl_entity_t	*start, *end;

	start = R_BeamGetEntity( startEnt );
	end = R_BeamGetEntity( endEnt );

	if( !start || !end )
		return NULL;

	if( life != 0 && ( !start->model || !end->model ))
		return NULL;

	pbeam = R_BeamLightning( vec3_origin, vec3_origin, modelIndex, life, width, amplitude, brightness, speed );
	if( !pbeam ) return NULL;

	pbeam->type = TE_BEAMRING;
	SetBits( pbeam->flags, FBEAM_STARTENTITY | FBEAM_ENDENTITY );
	if( life == 0 ) SetBits( pbeam->flags, FBEAM_FOREVER );
	pbeam->startEntity = startEnt;
	pbeam->endEntity = endEnt;

	R_BeamSetAttributes( pbeam, r, g, b, framerate, startFrame );

	return pbeam;
}

/*
==============
R_BeamFollow

Create beam following with entity
==============
*/
BEAM *GAME_EXPORT R_BeamFollow( int startEnt, int modelIndex, float life, float width, float r, float g, float b, float brightness )
{
	BEAM	*pbeam = R_BeamAlloc();

	if( !pbeam ) return NULL;
	pbeam->die = cl.time;

	if( modelIndex < 0 )
		return NULL;

	R_BeamSetup( pbeam, vec3_origin, vec3_origin, modelIndex, life, width, life, brightness, 1.0f );

	pbeam->type = TE_BEAMFOLLOW;
	SetBits( pbeam->flags, FBEAM_STARTENTITY );
	pbeam->startEntity = startEnt;

	R_BeamSetAttributes( pbeam, r, g, b, 1.0f, 0 );

	return pbeam;
}


/*
==============
R_BeamLightning

template for new beams
==============
*/
BEAM *GAME_EXPORT R_BeamLightning( vec3_t start, vec3_t end, int modelIndex, float life, float width, float amplitude, float brightness, float speed )
{
	BEAM	*pbeam = R_BeamAlloc();

	if( !pbeam ) return NULL;
	pbeam->die = cl.time;

	if( modelIndex < 0 )
		return NULL;

	R_BeamSetup( pbeam, start, end, modelIndex, life, width, amplitude, brightness, speed );

	return pbeam;
}



/*
===============
R_EntityParticles

set EF_BRIGHTFIELD effect
===============
*/
void GAME_EXPORT R_EntityParticles( cl_entity_t *ent )
{
	float		angle;
	float		sr, sp, sy, cr, cp, cy;
	vec3_t		forward;
	particle_t	*p;
	int		i;

	for( i = 0; i < NUMVERTEXNORMALS; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		angle = cl.time * cl_avelocities[i][0];
		SinCos( angle, &sy, &cy );
		angle = cl.time * cl_avelocities[i][1];
		SinCos( angle, &sp, &cp );
		angle = cl.time * cl_avelocities[i][2];
		SinCos( angle, &sr, &cr );

		VectorSet( forward, cp * cy, cp * sy, -sp );

		p->die = cl.time + 0.001f;
		p->color = 111; // yellow

		VectorMAMAM( 1.0f, ent->origin, 64.0f, m_bytenormals[i], 16.0f, forward, p->org );
	}
}

/*
===============
R_ParticleExplosion

===============
*/
void GAME_EXPORT R_ParticleExplosion( const vec3_t org )
{
	particle_t	*p;
	int		i, j;

	for( i = 0; i < 1024; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 5.0f;
		p->ramp = COM_RandomLong( 0, 3 );
		p->color = ramp1[0];

		for( j = 0; j < 3; j++ )
		{
			p->org[j] = org[j] + COM_RandomFloat( -16.0f, 16.0f );
			p->vel[j] = COM_RandomFloat( -256.0f, 256.0f );
		}

		if( i & 1 ) p->type = pt_explode;
		else p->type = pt_explode2;
	}
}

/*
===============
R_ParticleExplosion2

===============
*/
void GAME_EXPORT R_ParticleExplosion2( const vec3_t org, int colorStart, int colorLength )
{
	int		i, j;
	int		colorMod = 0, packedColor;
	particle_t	*p;

	packedColor = Host_IsQuakeCompatible( ) ? 255 : 0; // use old code for blob particles

	for( i = 0; i < 512; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 0.3f;
		p->color = colorStart + ( colorMod % colorLength );
		p->packedColor = packedColor;
		colorMod++;

		p->type = pt_blob;

		for( j = 0; j < 3; j++ )
		{
			p->org[j] = org[j] + COM_RandomFloat( -16.0f, 16.0f );
			p->vel[j] = COM_RandomFloat( -256.0f, 256.0f );
		}
	}
}

/*
===============
R_BlobExplosion

===============
*/
void GAME_EXPORT R_BlobExplosion( const vec3_t org )
{
	particle_t	*p;
	int		i, j, packedColor;

	packedColor = Host_IsQuakeCompatible( ) ? 255 : 0; // use old code for blob particles

	for( i = 0; i < 1024; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + COM_RandomFloat( 1.0f, 1.4f );
		p->packedColor = packedColor;

		if( i & 1 )
		{
			p->type = pt_blob;
			p->color = COM_RandomLong( 66, 71 );
		}
		else
		{
			p->type = pt_blob2;
			p->color = COM_RandomLong( 150, 155 );
		}

		for( j = 0; j < 3; j++ )
		{
			p->org[j] = org[j] + COM_RandomFloat( -16.0f, 16.0f );
			p->vel[j] = COM_RandomFloat( -256.0f, 256.0f );
		}
	}
}

/*
===============
ParticleEffect

PARTICLE_EFFECT on server
===============
*/
void GAME_EXPORT R_RunParticleEffect( const vec3_t org, const vec3_t dir, int color, int count )
{
	particle_t	*p;
	int		i;

	if( count == 1024 )
	{
		// rocket explosion
		R_ParticleExplosion( org );
		return;
	}

	for( i = 0; i < count; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->color = (color & ~7) + COM_RandomLong( 0, 7 );
		p->die = cl.time + COM_RandomFloat( 0.1f, 0.4f );
		p->type = pt_slowgrav;

		VectorAddScalar( org, COM_RandomFloat( -8.0f, 8.0f ), p->org );
		VectorScale( dir, 15.0f, p->vel );
	}
}

/*
===============
R_Blood

particle spray
===============
*/
void GAME_EXPORT R_Blood( const vec3_t org, const vec3_t ndir, int pcolor, int speed )
{
	vec3_t		pos, dir, vec;
	float		pspeed = speed * 3.0f;
	int		i, j;
	particle_t	*p;

	VectorNormalize2( ndir, dir );

	for( i = 0; i < (speed / 2); i++ )
	{
		VectorAddScalar( org, COM_RandomFloat( -3.0f, 3.0f ), pos );
		VectorAddScalar( dir, COM_RandomFloat( -0.06f, 0.06f ), vec );

		for( j = 0; j < 7; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			p->die = cl.time + 1.5f;
			p->color = pcolor + COM_RandomLong( 0, 9 );
			p->type = pt_vox_grav;

			VectorAddScalar( pos, COM_RandomFloat( -1.0f, 1.0f ), p->org );
			VectorScale( vec, pspeed, p->vel );
		}
	}
}

/*
===============
R_BloodStream

particle spray 2
===============
*/
void GAME_EXPORT R_BloodStream( const vec3_t org, const vec3_t ndir, int pcolor, int speed )
{
	particle_t	*p;
	int		i, j;
	float		arc;
	int		accel = speed; // must be integer due to bug in GoldSrc
	vec3_t dir;

	VectorNormalize2( ndir, dir );

	for( arc = 0.05f, i = 0; i < 100; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 2.0f;
		p->type = pt_vox_grav;
		p->color = pcolor + COM_RandomLong( 0, 9 );

		VectorCopy( org, p->org );
		VectorCopy( dir, p->vel );

		p->vel[2] -= arc;
		arc -= 0.005f;
		VectorScale( p->vel, accel, p->vel );
		accel -= 0.00001f; // so last few will drip
	}

	for( arc = 0.075f, i = 0; i < ( speed / 5 ); i++ )
	{
		float	num;

		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 3.0f;
		p->color = pcolor + COM_RandomLong( 0, 9 );
		p->type = pt_vox_slowgrav;

		VectorCopy( org, p->org );
		VectorCopy( dir, p->vel );

		p->vel[2] -= arc;
		arc -= 0.005f;

		num = COM_RandomFloat( 0.0f, 1.0f );
		accel = speed * num;
		num *= 1.7f;

		VectorScale( p->vel, num, p->vel );
		VectorScale( p->vel, accel, p->vel );

		for( j = 0; j < 2; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			p->die = cl.time + 3.0f;
			p->color = pcolor + COM_RandomLong( 0, 9 );
			p->type = pt_vox_slowgrav;

			p->org[0] = org[0] + COM_RandomFloat( -1.0f, 1.0f );
			p->org[1] = org[1] + COM_RandomFloat( -1.0f, 1.0f );
			p->org[2] = org[2] + COM_RandomFloat( -1.0f, 1.0f );

			VectorCopy( dir, p->vel );
			p->vel[2] -= arc;

			VectorScale( p->vel, num, p->vel );
			VectorScale( p->vel, accel, p->vel );
		}
	}
}

/*
===============
R_LavaSplash

===============
*/
void GAME_EXPORT R_LavaSplash( const vec3_t org )
{
	particle_t	*p;
	float		vel;
	vec3_t		dir;
	int		i, j, k;

	for( i = -16; i < 16; i++ )
	{
		for( j = -16; j <16; j++ )
		{
			for( k = 0; k < 1; k++ )
			{
				p = R_AllocParticle( NULL );
				if( !p ) return;

				p->die = cl.time + COM_RandomFloat( 2.0f, 2.62f );
				p->color = COM_RandomLong( 224, 231 );
				p->type = pt_slowgrav;

				dir[0] = j * 8.0f + COM_RandomFloat( 0.0f, 7.0f );
				dir[1] = i * 8.0f + COM_RandomFloat( 0.0f, 7.0f );
				dir[2] = 256.0f;

				p->org[0] = org[0] + dir[0];
				p->org[1] = org[1] + dir[1];
				p->org[2] = org[2] + COM_RandomFloat( 0.0f, 63.0f );

				VectorNormalize( dir );
				vel = COM_RandomFloat( 50.0f, 113.0f );
				VectorScale( dir, vel, p->vel );
			}
		}
	}
}

/*
===============
R_ParticleBurst

===============
*/
void GAME_EXPORT R_ParticleBurst( const vec3_t org, int size, int color, float life )
{
	particle_t	*p;
	vec3_t		dir, dest;
	int		i, j;
	float		dist;

	for( i = 0; i < 32; i++ )
	{
		for( j = 0; j < 32; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			p->die = cl.time + life + COM_RandomFloat( -0.5f, 0.5f );
			p->color = color + COM_RandomLong( 0, 10 );
			p->ramp = 1.0f;

			VectorCopy( org, p->org );
			VectorAddScalar( org, COM_RandomFloat( -size, size ), dest );
			VectorSubtract( dest, p->org, dir );
			dist = VectorNormalizeLength( dir );
			VectorScale( dir, ( dist / life ), p->vel );
		}
	}
}

/*
===============
R_LargeFunnel

===============
*/
void GAME_EXPORT R_LargeFunnel( const vec3_t org, int reverse )
{
	particle_t	*p;
	float		vel, dist;
	vec3_t		dir, dest;
	int		i, j;

	for( i = -8; i < 8; i++ )
	{
		for( j = -8; j < 8; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			dest[0] = (i * 32.0f) + org[0];
			dest[1] = (j * 32.0f) + org[1];
			dest[2] = org[2] + COM_RandomFloat( 100.0f, 800.0f );

			if( reverse )
			{
				VectorCopy( org, p->org );
				VectorSubtract( dest, p->org, dir );
			}
			else
			{
				VectorCopy( dest, p->org );
				VectorSubtract( org, p->org, dir );
			}

			vel = dest[2] / 8.0f;
			if( vel < 64.0f ) vel = 64.0f;

			dist = VectorNormalizeLength( dir );
			vel += COM_RandomFloat( 64.0f, 128.0f );
			VectorScale( dir, vel, p->vel );
			p->die = cl.time + (dist / vel );
			p->color = 244; // green color
		}
	}
}

/*
===============
R_TeleportSplash

===============
*/
void GAME_EXPORT R_TeleportSplash( const vec3_t org )
{
	particle_t	*p;
	vec3_t		dir;
	float		vel;
	int		i, j, k;

	for( i = -16; i < 16; i += 4 )
	{
		for( j = -16; j < 16; j += 4 )
		{
			for( k = -24; k < 32; k += 4 )
			{
				p = R_AllocParticle( NULL );
				if( !p ) return;

				p->die = cl.time + COM_RandomFloat( 0.2f, 0.34f );
				p->color = COM_RandomLong( 7, 14 );
				p->type = pt_slowgrav;

				dir[0] = j * 8.0f;
				dir[1] = i * 8.0f;
				dir[2] = k * 8.0f;

				p->org[0] = org[0] + i + COM_RandomFloat( 0.0f, 3.0f );
				p->org[1] = org[1] + j + COM_RandomFloat( 0.0f, 3.0f );
				p->org[2] = org[2] + k + COM_RandomFloat( 0.0f, 3.0f );

				VectorNormalize( dir );
				vel = COM_RandomFloat( 50.0f, 113.0f );
				VectorScale( dir, vel, p->vel );
			}
		}
	}
}

/*
===============
R_RocketTrail

===============
*/
void GAME_EXPORT R_RocketTrail( vec3_t start, vec3_t end, int type )
{
	vec3_t		vec, right, up;
	float		len, dec;
	particle_t	*p;

	VectorSubtract( end, start, vec );
	len = VectorNormalizeLength( vec );

	if( type == 7 )
	{
		dec = 1.0f;
		VectorVectors( vec, right, up );
	}
	else if( type < 128 )
	{
		dec = 3.0f;
	}
	else
	{
		// initialize if type will be 7 here
		VectorVectors( vec, right, up );

		dec = 1.0f;
		type -= 128;
	}

	VectorScale( vec, dec, vec );

	while( len > 0 )
	{
		p = R_AllocParticle( NULL );
		if( !p )
			return;

		len -= dec;
		p->die = cl.time + 2.0f;

		switch( type )
		{
		case 0:
		case 1:
			p->ramp = COM_RandomLong( 0 + type * 2, 3 + type * 2 );
			p->color = ramp3[(int)p->ramp];
			p->type = pt_fire;
			VectorAddScalar( start, COM_RandomFloat( -3.0f, 3.0f ), p->org );
			break;
		case 2:
			p->color = COM_RandomLong( 67, 74 );
			p->type = pt_grav;
			VectorAddScalar( start, COM_RandomFloat( -3.0f, 3.0f ), p->org );
			break;
		case 3:
		case 5:
		{
			static int	tracercount;
			p->die = cl.time + 0.5f;
			p->color = ( tracercount & 4 ) * 2;

			if( type == 3 )
				p->color += 52;
			else
				p->color += 230;

			VectorCopy( start, p->org );
			tracercount++;

			p->vel[0] = 30.0f * vec[1];
			p->vel[1] = 30.0f * vec[0];
			p->vel[tracercount & 1] = -p->vel[tracercount & 1];
			break;
		}
		case 4:
			p->color = COM_RandomLong( 67, 70 );
			p->type = pt_grav;
			VectorAddScalar( start, COM_RandomFloat( -3.0f, 3.0f ), p->org );
			len -= 3.0f;
			break;
		case 6:
			p->type = pt_fire;
			p->ramp = COM_RandomLong( 0, 3 );
			p->color = ramp3[(int)p->ramp];
			VectorCopy( start, p->org );
			break;
		case 7:
		{
			float x = COM_RandomLong( 0, 65535 );
			float y = COM_RandomLong( 8, 16 );
			float s, c;

			SinCos( x, &s, &c );
			s *= y;
			c *= y;

			VectorMAMAM( 1.0f, start, s, right, c, up, p->org );
			VectorSubtract( start, p->org, p->vel );
			VectorScale( p->vel, 2.0f, p->vel );

			x = COM_RandomFloat( 96.0f, 111.0f );
			VectorMA( p->vel, x, vec, p->vel );

			p->ramp = COM_RandomLong( 0, 3 );
			p->color = ramp3[(int)p->ramp];
			p->type = pt_explode2;
			break;
		}
		default:
			VectorCopy( start, p->org );
			break;
		}

		VectorAdd( start, vec, start );
	}
}

/*
===============
PM_ParticleLine

draw line from particles
================
*/
static void PM_ParticleLine( const vec3_t start, const vec3_t end, int pcolor, float life, float zvel )
{
	float	len, curdist;
	vec3_t	diff, pos;

	// determine distance
	VectorSubtract( end, start, diff );
	len = VectorNormalizeLength( diff );
	curdist = 0;

	while( curdist <= len )
	{
		VectorMA( start, curdist, diff, pos );
		CL_Particle( pos, pcolor, life, 0, zvel );
		curdist += 2.0f;
	}
}

/*
================
PM_DrawRectangle

================
*/
static void PM_DrawRectangle( const vec3_t tl, const vec3_t bl, const vec3_t tr, const vec3_t br, int pcolor, float life )
{
	PM_ParticleLine( tl, bl, pcolor, life, 0 );
	PM_ParticleLine( bl, br, pcolor, life, 0 );
	PM_ParticleLine( br, tr, pcolor, life, 0 );
	PM_ParticleLine( tr, tl, pcolor, life, 0 );
}

/*
================
PM_DrawBBox

================
*/
static void PM_DrawBBox( const vec3_t mins, const vec3_t maxs, const vec3_t origin, int pcolor, float life )
{
	vec3_t	p[8], tmp;
	float	gap = BOX_GAP;
	int	i;

	for( i = 0; i < 8; i++ )
	{
		tmp[0] = (i & 1) ? mins[0] - gap : maxs[0] + gap;
		tmp[1] = (i & 2) ? mins[1] - gap : maxs[1] + gap ;
		tmp[2] = (i & 4) ? mins[2] - gap : maxs[2] + gap ;

		VectorAdd( tmp, origin, tmp );
		VectorCopy( tmp, p[i] );
	}

	for( i = 0; i < 6; i++ )
	{
		PM_DrawRectangle( p[boxpnt[i][1]], p[boxpnt[i][0]], p[boxpnt[i][2]], p[boxpnt[i][3]], pcolor, life );
	}
}

/*
================
R_ParticleLine

================
*/
void GAME_EXPORT R_ParticleLine( const vec3_t start, const vec3_t end, byte r, byte g, byte b, float life )
{
	int	pcolor;

	pcolor = R_LookupColor( r, g, b );
	PM_ParticleLine( start, end, pcolor, life, 0 );
}

/*
================
R_ParticleBox

================
*/
void GAME_EXPORT R_ParticleBox( const vec3_t absmin, const vec3_t absmax, byte r, byte g, byte b, float life )
{
	vec3_t	mins, maxs;
	vec3_t	origin;
	int	pcolor;

	pcolor = R_LookupColor( r, g, b );

	VectorAverage( absmax, absmin, origin );
	VectorSubtract( absmax, origin, maxs );
	VectorSubtract( absmin, origin, mins );

	PM_DrawBBox( mins, maxs, origin, pcolor, life );
}

/*
================
R_ShowLine

================
*/
void GAME_EXPORT R_ShowLine( const vec3_t start, const vec3_t end )
{
	vec3_t		dir, org;
	float		len;
	particle_t	*p;

	VectorSubtract( end, start, dir );
	len = VectorNormalizeLength( dir );
	VectorScale( dir, 5.0f, dir );
	VectorCopy( start, org );

	while( len > 0 )
	{
		len -= 5.0f;

		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 30;
		p->color = 75;

		VectorCopy( org, p->org );
		VectorAdd( org, dir, org );
	}
}

/*
===============
R_BulletImpactParticles

===============
*/
void GAME_EXPORT R_BulletImpactParticles( const vec3_t pos )
{
	int		i, quantity;
	int		color;
	float		dist;
	vec3_t		dir;
	particle_t	*p;

	VectorSubtract( pos, refState.vieworg, dir );
	dist = VectorLength( dir );
	if( dist > 1000.0f ) dist = 1000.0f;

	quantity = (1000.0f - dist) / 100.0f;
	if( quantity == 0 ) quantity = 1;

	color = 3 - ((30 * quantity) / 100 );
	R_SparkStreaks( pos, 2, -200, 200 );

	for( i = 0; i < quantity * 4; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		VectorCopy( pos, p->org);

		p->vel[0] = COM_RandomFloat( -1.0f, 1.0f );
		p->vel[1] = COM_RandomFloat( -1.0f, 1.0f );
		p->vel[2] = COM_RandomFloat( -1.0f, 1.0f );
		VectorScale( p->vel, COM_RandomFloat( 50.0f, 100.0f ), p->vel );

		p->die = cl.time + 0.5;
		p->color = 3 - color;
		p->type = pt_grav;
	}
}

/*
===============
R_FlickerParticles

===============
*/
void GAME_EXPORT R_FlickerParticles( const vec3_t org )
{
	particle_t	*p;
	int		i;

	for( i = 0; i < 15; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		VectorCopy( org, p->org );
		p->vel[0] = COM_RandomFloat( -32.0f, 32.0f );
		p->vel[1] = COM_RandomFloat( -32.0f, 32.0f );
		p->vel[2] = COM_RandomFloat( 80.0f, 143.0f );

		p->die = cl.time + 2.0f;
		p->type = pt_blob2;
		p->color = 254;
	}
}

/*
===============
R_StreakSplash

create a splash of streaks
===============
*/
void GAME_EXPORT R_StreakSplash( const vec3_t pos, const vec3_t dir, int color, int count, float speed, int velocityMin, int velocityMax )
{
	vec3_t		vel, vel2;
	particle_t	*p;
	int		i;

	VectorScale( dir, speed, vel );

	for( i = 0; i < count; i++ )
	{
		VectorAddScalar( vel, COM_RandomFloat( velocityMin, velocityMax ), vel2 );
		p = R_AllocTracer( pos, vel2, COM_RandomFloat( 0.1f, 0.5f ));
		if( !p ) return;

		p->type = pt_grav;
		p->color = color;
		p->ramp = 1.0f;
	}
}

/*
===============
CL_Particle

pmove debugging particle
===============
*/
void CL_Particle( const vec3_t org, int color, float life, int zpos, int zvel )
{
	particle_t	*p;

	p = R_AllocParticle( NULL );
	if( !p ) return;

	if( org ) VectorCopy( org, p->org );
	p->die = cl.time + life;
	p->vel[2] += zvel;	// ???
	p->color = color;
}

/*
===============
R_TracerEffect

===============
*/
void GAME_EXPORT R_TracerEffect( const vec3_t start, const vec3_t end )
{
	vec3_t	pos, vel, dir;
	float	len, speed;
	float	offset;

	speed = Q_max( tracerspeed.value, 3.0f );

	VectorSubtract( end, start, dir );
	len = VectorLength( dir );
	if( len == 0.0f ) return;

	VectorScale( dir, 1.0f / len, dir ); // normalize
	offset = COM_RandomFloat( -10.0f, 9.0f ) + traceroffset.value;
	VectorScale( dir, offset, vel );
	VectorAdd( start, vel, pos );
	VectorScale( dir, speed, vel );

	R_AllocTracer( pos, vel, len / speed );
}

/*
===============
R_UserTracerParticle

===============
*/
void GAME_EXPORT R_UserTracerParticle( float *org, float *vel, float life, int colorIndex, float length, byte deathcontext, void (*deathfunc)( particle_t *p ))
{
	particle_t	*p;

	if( colorIndex < 0 )
		return;

	if(( p = R_AllocTracer( org, vel, life )) != NULL )
	{
		p->context = deathcontext;
		p->deathfunc = deathfunc;
		p->color = colorIndex;
		p->ramp = length;
	}
}

/*
===============
R_TracerParticles

allow more customization
===============
*/
particle_t *R_TracerParticles( float *org, float *vel, float life )
{
	return R_AllocTracer( org, vel, life );
}

/*
===============
R_SparkStreaks

create a streak tracers
===============
*/
void GAME_EXPORT R_SparkStreaks( const vec3_t pos, int count, int velocityMin, int velocityMax )
{
	particle_t	*p;
	vec3_t		vel;
	int		i;

	for( i = 0; i<count; i++ )
	{
		vel[0] = COM_RandomFloat( velocityMin, velocityMax );
		vel[1] = COM_RandomFloat( velocityMin, velocityMax );
		vel[2] = COM_RandomFloat( velocityMin, velocityMax );

		p = R_AllocTracer( pos, vel, COM_RandomFloat( 0.1f, 0.5f ));
		if( !p ) return;

		p->color = 5;
		p->type = pt_grav;
		p->ramp = 0.5f;
	}
}

/*
===============
R_Implosion

make implosion tracers
===============
*/
void GAME_EXPORT R_Implosion( const vec3_t end, float radius, int count, float life )
{
	float		dist = ( radius / 100.0f );
	vec3_t		start, temp, vel;
	float		factor;
	particle_t	*p;
	int		i;

	if( life <= 0.0f ) life = 0.1f; // to avoid divide by zero
	factor = -1.0 / life;

	for ( i = 0; i < count; i++ )
	{
		temp[0] = dist * COM_RandomFloat( -100.0f, 100.0f );
		temp[1] = dist * COM_RandomFloat( -100.0f, 100.0f );
		temp[2] = dist * COM_RandomFloat( 0.0f, 100.0f );
		VectorScale( temp, factor, vel );
		VectorAdd( temp, end, start );

		if(( p = R_AllocTracer( start, vel, life )) == NULL )
			return;

		p->type = pt_explode;
	}
}


/*
==============
R_FreeDeadParticles

Free particles that time has expired
==============
*/
void R_FreeDeadParticles( particle_t **ppparticles )
{
	particle_t	*p, *kill;

	// kill all the ones hanging direcly off the base pointer
	while( 1 )
	{
		kill = *ppparticles;
		if( kill && kill->die < cl.time )
		{
			if( kill->deathfunc )
				kill->deathfunc( kill );
			kill->deathfunc = NULL;
			*ppparticles = kill->next;
			kill->next = cl_free_particles;
			cl_free_particles = kill;
			continue;
		}
		break;
	}

	// kill off all the others
	for( p = *ppparticles; p; p = p->next )
	{
		while( 1 )
		{
			kill = p->next;
			if( kill && kill->die < cl.time )
			{
				if( kill->deathfunc )
					kill->deathfunc( kill );
				kill->deathfunc = NULL;
				p->next = kill->next;
				kill->next = cl_free_particles;
				cl_free_particles = kill;
				continue;
			}
			break;
		}
	}
}

/*
===============
CL_ReadPointFile_f

===============
*/
void CL_ReadPointFile_f( void )
{
	byte *afile;
	char *pfile;
	vec3_t		org;
	int		count;
	particle_t	*p;
	char		filename[64];
	string		token;

	Q_snprintf( filename, sizeof( filename ), "maps/%s.pts", clgame.mapname );
	afile = FS_LoadFile( filename, NULL, false );

	if( !afile )
	{
		Con_Printf( S_ERROR "couldn't open %s\n", filename );
		return;
	}

	Con_Printf( "Reading %s...\n", filename );

	count = 0;
	pfile = (char *)afile;

	while( 1 )
	{
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		org[0] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		org[1] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;
		org[2] = Q_atof( token );

		count++;

		if( !cl_free_particles )
		{
			Con_Printf( S_ERROR "not enough free particles!\n" );
			break;
		}

		// NOTE: can't use R_AllocParticle because this command
		// may be executed from the console, while frametime is 0
		p = cl_free_particles;
		cl_free_particles = p->next;
		p->next = cl_active_particles;
		cl_active_particles = p;

		p->ramp = 0;
		p->type = pt_static;
		p->die = cl.time + 99999;
		p->color = (-count) & 15;
		VectorCopy( org, p->org );
		VectorClear( p->vel );
	}

	Mem_Free( afile );

	if( count ) Con_Printf( "%i points read\n", count );
	else Con_Printf( "map %s has no leaks!\n", clgame.mapname );
}

static void CL_FreeDeadBeams( void )
{
	BEAM *pBeam, *pNext, *pPrev = NULL;
	// draw temporary entity beams
	for( pBeam = cl_active_beams; pBeam; pBeam = pNext )
	{
		// need to store the next one since we may delete this one
		pNext = pBeam->next;

		// retire old beams
		if( CL_BeamAttemptToDie( pBeam ))
		{
			// reset links
			if( pPrev ) pPrev->next = pNext;
			else cl_active_beams = pNext;

			// free the beam
			R_BeamFree( pBeam );

			pBeam = NULL;
			continue;
		}

		pPrev = pBeam;
	}
}

void CL_DrawEFX( float time, qboolean fTrans )
{
	CL_FreeDeadBeams();
	if( cl_draw_beams.value )
		ref.dllFuncs.CL_DrawBeams( fTrans, cl_active_beams );

	if( fTrans )
	{
		R_FreeDeadParticles( &cl_active_particles );
		if( cl_draw_particles.value )
			ref.dllFuncs.CL_DrawParticles( time, cl_active_particles, PART_SIZE );
		R_FreeDeadParticles( &cl_active_tracers );
		if( cl_draw_tracers.value )
			ref.dllFuncs.CL_DrawTracers( time, cl_active_tracers );
	}
}

void CL_ThinkParticle( double frametime, particle_t *p )
{
	float		time3 = 15.0f * frametime;
	float		time2 = 10.0f * frametime;
	float		time1 = 5.0f * frametime;
	float		dvel = 4.0f * frametime;
	float		grav = frametime * clgame.movevars.gravity * 0.05f;


	if( p->type != pt_clientcustom )
	{
		// update position.
		VectorMA( p->org, frametime, p->vel, p->org );
	}

	switch( p->type )
	{
	case pt_static:
		break;
	case pt_fire:
		p->ramp += time1;
		if( p->ramp >= 6.0f ) p->die = -1.0f;
		else p->color = ramp3[(int)p->ramp];
		p->vel[2] += grav;
		break;
	case pt_explode:
		p->ramp += time2;
		if( p->ramp >= 8.0f ) p->die = -1.0f;
		else p->color = ramp1[(int)p->ramp];
		VectorMA( p->vel, dvel, p->vel, p->vel );
		p->vel[2] -= grav;
		break;
	case pt_explode2:
		p->ramp += time3;
		if( p->ramp >= 8.0f ) p->die = -1.0f;
		else p->color = ramp2[(int)p->ramp];
		VectorMA( p->vel,-frametime, p->vel, p->vel );
		p->vel[2] -= grav;
		break;
	case pt_blob:
		if( p->packedColor == 255 )
		{
			// normal blob explosion
			VectorMA( p->vel, dvel, p->vel, p->vel );
			p->vel[2] -= grav;
			break;
		}
		// intentionally fallthrough
	case pt_blob2:
		if( p->packedColor == 255 )
		{
			// normal blob explosion
			p->vel[0] -= p->vel[0] * dvel;
			p->vel[1] -= p->vel[1] * dvel;
			p->vel[2] -= grav;
		}
		else
		{
			p->ramp += time2;
			if( p->ramp >= 9.0f ) p->ramp = 0.0f;
			p->color = gSparkRamp[(int)p->ramp];
			VectorMA( p->vel, -frametime * 0.5f, p->vel, p->vel );
			p->type = COM_RandomLong( 0, 3 ) ? pt_blob : pt_blob2;
			p->vel[2] -= grav * 5.0f;
		}
		break;
	case pt_grav:
		p->vel[2] -= grav * 20.0f;
		break;
	case pt_slowgrav:
		p->vel[2] -= grav;
		break;
	case pt_vox_grav:
		p->vel[2] -= grav * 8.0f;
		break;
	case pt_vox_slowgrav:
		p->vel[2] -= grav * 4.0f;
		break;
	case pt_clientcustom:
		if( p->callback )
			p->callback( p, frametime );
		break;
	}
}
