/*
sv_gc_campaign.c - GameCube auto-campaign driver for recorded playthroughs
Copyright (C) 2026 xash3d-gc contributors

-gcautocampaign (gamecube.cfg "autocampaign [seconds]"): after a map has run
for the given time (default 20 s), move the player into the
trigger_changelevel that leads to the first map not visited yet, so the
game's own trigger fires the level change (landmark, carried entities and
all).  If nothing has changed level a few seconds later (use-only or locked
triggers), queue that trigger's changelevel directly and log it as FORCED.
Every map arrival is logged with the session time, so a recording of the run
can be cut and labelled by map.
*/

#include "common.h"
#include "server.h"

#if XASH_GAMECUBE

#define GC_CAMPAIGN_MAX_TRIGGERS	16
#define GC_CAMPAIGN_MAX_VISITED		160
#define GC_CAMPAIGN_FORCE_AFTER		6.0

typedef struct
{
	char map[32];
	char landmark[32];
	char model[16];
} gc_campaign_trigger_t;

static gc_campaign_trigger_t gc_camp_triggers[GC_CAMPAIGN_MAX_TRIGGERS];
static int gc_camp_num_triggers;
static char gc_camp_visited[GC_CAMPAIGN_MAX_VISITED][32];
static int gc_camp_num_visited;
static char gc_camp_prev[32];
static char gc_camp_current[32];
static double gc_camp_map_start;
static double gc_camp_session_start;
static qboolean gc_camp_moved, gc_camp_forced;
static int gc_camp_target = -1;
static int gc_camp_enabled = -1;

static qboolean SV_GCCampaignEnabled( void )
{
	if( gc_camp_enabled < 0 )
		gc_camp_enabled = Sys_CheckParm( "-gcautocampaign" ) ? 1 : 0;
	return gc_camp_enabled > 0;
}

qboolean SV_GCAutoCampaignActive( void )
{
	return SV_GCCampaignEnabled();
}

static qboolean SV_GCCampaignVisited( const char *map )
{
	int i;

	for( i = 0; i < gc_camp_num_visited; i++ )
	{
		if( !Q_stricmp( gc_camp_visited[i], map ))
			return true;
	}
	return false;
}

/* Called from SV_LoadFromFile before the entities spawn: remember every
 * trigger_changelevel (target map, landmark, brush model). */
void SV_GCCampaignScanEntities( const char *mapname, const char *entities )
{
	char token[256], key[64];
	gc_campaign_trigger_t cur;
	qboolean is_trigger = false;
	const char *p = entities;

	if( !SV_GCCampaignEnabled() || !entities )
		return;

	gc_camp_num_triggers = 0;
	memset( &cur, 0, sizeof( cur ));
	while(( p = COM_ParseFile( (char *)p, token, sizeof( token ))) != NULL )
	{
		if( token[0] == '{' )
		{
			memset( &cur, 0, sizeof( cur ));
			is_trigger = false;
			continue;
		}
		if( token[0] == '}' )
		{
			if( is_trigger && cur.map[0] && gc_camp_num_triggers < GC_CAMPAIGN_MAX_TRIGGERS )
				gc_camp_triggers[gc_camp_num_triggers++] = cur;
			continue;
		}
		Q_strncpy( key, token, sizeof( key ));
		if(( p = COM_ParseFile( (char *)p, token, sizeof( token ))) == NULL )
			break;
		if( !Q_stricmp( key, "classname" ))
			is_trigger = !Q_stricmp( token, "trigger_changelevel" );
		else if( !Q_stricmp( key, "map" ))
			Q_strncpy( cur.map, token, sizeof( cur.map ));
		else if( !Q_stricmp( key, "landmark" ))
			Q_strncpy( cur.landmark, token, sizeof( cur.landmark ));
		else if( !Q_stricmp( key, "model" ))
			Q_strncpy( cur.model, token, sizeof( cur.model ));
	}

	/* Arrival bookkeeping. */
	if( Q_stricmp( gc_camp_current, mapname ))
	{
		Q_strncpy( gc_camp_prev, gc_camp_current, sizeof( gc_camp_prev ));
		Q_strncpy( gc_camp_current, mapname, sizeof( gc_camp_current ));
	}
	if( !SV_GCCampaignVisited( mapname ) && gc_camp_num_visited < GC_CAMPAIGN_MAX_VISITED )
		Q_strncpy( gc_camp_visited[gc_camp_num_visited++], mapname, sizeof( gc_camp_visited[0] ));
	if( gc_camp_session_start <= 0.0 )
		gc_camp_session_start = Platform_DoubleTime();
	gc_camp_map_start = -1.0; /* armed at the first server frame of the map */
	gc_camp_moved = gc_camp_forced = false;
	gc_camp_target = -1;
	Con_Printf( "Xash3D GameCube: CAMPAIGN map %s at t=%.1fs (from %s) exits=%d\n",
		mapname, Platform_DoubleTime() - gc_camp_session_start,
		gc_camp_prev[0] ? gc_camp_prev : "start", gc_camp_num_triggers );
}

static int SV_GCCampaignPickExit( void )
{
	int i;

	/* First a map never visited, then the way back (hub maps), then any. */
	for( i = 0; i < gc_camp_num_triggers; i++ )
	{
		if( Q_stricmp( gc_camp_triggers[i].map, sv.name ) && !SV_GCCampaignVisited( gc_camp_triggers[i].map ))
			return i;
	}
	for( i = 0; i < gc_camp_num_triggers; i++ )
	{
		if( Q_stricmp( gc_camp_triggers[i].map, gc_camp_prev ))
			continue;
		return i;
	}
	return gc_camp_num_triggers > 0 ? 0 : -1;
}

static edict_t *SV_GCCampaignFindTrigger( const gc_campaign_trigger_t *t )
{
	int i;

	if( !t->model[0] )
		return NULL;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );

		if( !SV_IsValidEdict( e ))
			continue;
		if( Q_stricmp( SV_GetString( e->v.classname ), "trigger_changelevel" ))
			continue;
		if( !Q_stricmp( SV_GetString( e->v.model ), t->model ))
			return e;
	}
	return NULL;
}

/* Visible label in the recording: what happens next is the autopilot. */
static void SV_GCCampaignCenterPrint( const char *text )
{
	sv_client_t *cl = svs.clients;

	if( !cl || cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ))
		return;
	MSG_BeginServerCmd( &cl->netchan.message, svc_centerprint );
	MSG_WriteString( &cl->netchan.message, text );
}

/* Once per server frame while a map is running. */
void SV_GCCampaignFrame( void )
{
	char delay_arg[16];
	double delay = 20.0, elapsed;
	gc_campaign_trigger_t *t;
	edict_t *player, *trig;

	{
		static unsigned calls;

		if( SV_GCCampaignEnabled() && ( ++calls % 600 ) == 1 )
			Con_Printf( "Xash3D GameCube: CAMPAIGN tick n=%u map=%s cur=%s state=%d time=%.1f start=%.1f\n",
				calls, sv.name, gc_camp_current, sv.state, sv.time, gc_camp_map_start );
	}
	if( !SV_GCCampaignEnabled() || sv.state != ss_active || svs.maxclients < 1 )
		return;
	if( Q_stricmp( gc_camp_current, sv.name ))
		return; /* map not scanned (loaded some other way) */
	if( gc_camp_map_start < 0.0 )
		gc_camp_map_start = sv.time;
	if( Sys_GetParmFromCmdLine( "-gcautocampaign", delay_arg ) && atof( delay_arg ) > 1.0 )
		delay = atof( delay_arg );
	elapsed = sv.time - gc_camp_map_start;
	if( elapsed < delay )
		return;

	if( gc_camp_target < 0 )
	{
		gc_camp_target = SV_GCCampaignPickExit();
		if( gc_camp_target < 0 )
		{
			if( !gc_camp_forced )
			{
				gc_camp_forced = true;
				Con_Printf( "Xash3D GameCube: CAMPAIGN end: %s has no trigger_changelevel\n", sv.name );
			}
			return;
		}
	}
	t = &gc_camp_triggers[gc_camp_target];
	player = SV_EdictNum( 1 );

	if( !gc_camp_moved )
	{
		gc_camp_moved = true;
		trig = SV_GCCampaignFindTrigger( t );
		if( trig && SV_IsValidEdict( player ))
		{
			vec3_t center;

			VectorAverage( trig->v.absmin, trig->v.absmax, center );
			VectorCopy( center, player->v.origin );
			VectorClear( player->v.velocity );
			SV_LinkEdict( player, true );
			SV_GCCampaignCenterPrint( va( "AUTOPILOT: teleport to exit -> %s", t->map ));
			Con_Printf( "Xash3D GameCube: CAMPAIGN walk-in %s -> %s via %s (landmark %s)\n",
				sv.name, t->map, t->model, t->landmark[0] ? t->landmark : "-" );
		}
		else
			Con_Printf( "Xash3D GameCube: CAMPAIGN no trigger edict for %s -> %s\n", sv.name, t->map );
		return;
	}
	if( !gc_camp_forced && elapsed >= delay + GC_CAMPAIGN_FORCE_AFTER )
	{
		gc_camp_forced = true;
		SV_GCCampaignCenterPrint( va( "AUTOPILOT: forced level change -> %s", t->map ));
		Con_Printf( "Xash3D GameCube: CAMPAIGN FORCED changelevel %s -> %s (landmark %s)\n",
			sv.name, t->map, t->landmark[0] ? t->landmark : "-" );
		SV_QueueChangeLevel( t->map, t->landmark[0] ? t->landmark : NULL );
	}
}

#endif /* XASH_GAMECUBE */
