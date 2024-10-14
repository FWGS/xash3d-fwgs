/*
cl_tent.h - efx api set
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef CL_TENT_H
#define CL_TENT_H

#include "triangleapi.h"

// EfxAPI
struct particle_s *R_AllocParticle( void (*callback)( struct particle_s*, float ));
void R_Explosion( vec3_t pos, int model, float scale, float framerate, int flags );
void R_ParticleExplosion( const vec3_t org );
void R_ParticleExplosion2( const vec3_t org, int colorStart, int colorLength );
void R_Implosion( const vec3_t end, float radius, int count, float life );
void R_Blood( const vec3_t org, const vec3_t dir, int pcolor, int speed );
void R_BloodStream( const vec3_t org, const vec3_t dir, int pcolor, int speed );
void R_BlobExplosion( const vec3_t org );
void R_EntityParticles( cl_entity_t *ent );
void R_FlickerParticles( const vec3_t org );
void R_RunParticleEffect( const vec3_t org, const vec3_t dir, int color, int count );
void R_ParticleBurst( const vec3_t org, int size, int color, float life );
void R_LavaSplash( const vec3_t org );
void R_TeleportSplash( const vec3_t org );
void R_RocketTrail( vec3_t start, vec3_t end, int type );
short R_LookupColor( byte r, byte g, byte b );
void R_GetPackedColor( short *packed, short color );
void R_TracerEffect( const vec3_t start, const vec3_t end );
void R_UserTracerParticle( float *org, float *vel, float life, int colorIndex, float length, byte deathcontext, void (*deathfunc)( struct particle_s* ));
struct particle_s *R_TracerParticles( float *org, float *vel, float life );
void R_ParticleLine( const vec3_t start, const vec3_t end, byte r, byte g, byte b, float life );
void R_ParticleBox( const vec3_t mins, const vec3_t maxs, byte r, byte g, byte b, float life );
void R_ShowLine( const vec3_t start, const vec3_t end );
void R_BulletImpactParticles( const vec3_t pos );
void R_SparkShower( const vec3_t org );
struct tempent_s *CL_TempEntAlloc( const vec3_t org, model_t *pmodel );
struct tempent_s *CL_TempEntAllocHigh( const vec3_t org, model_t *pmodel );
struct tempent_s *CL_TempEntAllocNoModel( const vec3_t org );
struct tempent_s *CL_TempEntAllocCustom( const vec3_t org, model_t *model, int high, void (*callback)( struct tempent_s*, float, float ));
void R_FizzEffect( cl_entity_t *pent, int modelIndex, int density );
void R_Bubbles( const vec3_t mins, const vec3_t maxs, float height, int modelIndex, int count, float speed );
void R_BubbleTrail( const vec3_t start, const vec3_t end, float flWaterZ, int modelIndex, int count, float speed );
void R_AttachTentToPlayer( int client, int modelIndex, float zoffset, float life );
void R_KillAttachedTents( int client );
void R_RicochetSprite( const vec3_t pos, model_t *pmodel, float duration, float scale );
void R_RocketFlare( const vec3_t pos );
void R_MuzzleFlash( const vec3_t pos, int type );
void R_BloodSprite( const vec3_t org, int colorIndex, int modelIndex, int modelIndex2, float size );
void R_BreakModel( const vec3_t pos, const vec3_t size, const vec3_t dir, float random, float life, int count, int modelIndex, char flags );
struct tempent_s *R_TempModel( const vec3_t pos, const vec3_t dir, const vec3_t angles, float life, int modelIndex, int soundtype );
struct tempent_s *R_TempSprite( vec3_t pos, const vec3_t dir, float scale, int modelIndex, int rendermode, int renderfx, float a, float life, int flags );
struct tempent_s *R_DefaultSprite( const vec3_t pos, int spriteIndex, float framerate );
void R_Sprite_Explode( struct tempent_s *pTemp, float scale, int flags );
void R_Sprite_Smoke( struct tempent_s *pTemp, float scale );
void R_Spray( const vec3_t pos, const vec3_t dir, int modelIndex, int count, int speed, int iRand, int renderMode );
void R_Sprite_Spray( const vec3_t pos, const vec3_t dir, int modelIndex, int count, int speed, int iRand );
void R_Sprite_Trail( int type, vec3_t vecStart, vec3_t vecEnd, int modelIndex, int nCount, float flLife, float flSize, float flAmplitude, int nRenderamt, float flSpeed );
void R_FunnelSprite( const vec3_t pos, int spriteIndex, int flags );
void R_LargeFunnel( const vec3_t pos, int reverse );
void R_SparkEffect( const vec3_t pos, int count, int velocityMin, int velocityMax );
void R_StreakSplash( const vec3_t pos, const vec3_t dir, int color, int count, float speed, int velMin, int velMax );
void R_SparkStreaks( const vec3_t pos, int count, int velocityMin, int velocityMax );
void R_Projectile( const vec3_t origin, const vec3_t velocity, int modelIndex, int life, int owner, void (*hitcallback)( struct tempent_s*, struct pmtrace_s* ));
void R_TempSphereModel( const vec3_t pos, float speed, float life, int count, int modelIndex );
void R_MultiGunshot( const vec3_t org, const vec3_t dir, const vec3_t noise, int count, int decalCount, int *decalIndices );
void R_FireField( float *org, int radius, int modelIndex, int count, int flags, float life );
void R_PlayerSprites( int client, int modelIndex, int count, int size );
void R_Sprite_WallPuff( struct tempent_s *pTemp, float scale );
void R_RicochetSound( const vec3_t pos );
struct dlight_s *CL_AllocDlight( int key );
struct dlight_s *CL_AllocElight( int key );
void CL_AddEntityEffects( cl_entity_t *ent );
void CL_AddModelEffects( cl_entity_t *ent );
void CL_DecalShoot( int textureIndex, int entityIndex, int modelIndex, float *pos, int flags );
void CL_DecalRemoveAll( int textureIndex );
int CL_DecalIndexFromName( const char *name );
int CL_DecalIndex( int id );

// RefAPI
struct particle_s *CL_AllocParticleFast( void );

// Beams
struct beam_s *R_BeamLightning( vec3_t start, vec3_t end, int modelIndex, float life, float width, float amplitude, float brightness, float speed );
struct beam_s *R_BeamEnts( int startEnt, int endEnt, int modelIndex, float life, float width, float amplitude, float brightness, float speed, int startFrame, float framerate, float r, float g, float b );
struct beam_s *R_BeamPoints( vec3_t start, vec3_t end, int modelIndex, float life, float width, float amplitude, float brightness, float speed, int startFrame, float framerate, float r, float g, float b );
struct beam_s *R_BeamCirclePoints( int type, vec3_t start, vec3_t end, int modelIndex, float life, float width, float amplitude, float brightness, float speed, int startFrame, float framerate, float r, float g, float b );
struct beam_s *R_BeamEntPoint( int startEnt, vec3_t end, int modelIndex, float life, float width, float amplitude, float brightness, float speed, int startFrame, float framerate, float r, float g, float b );
struct beam_s *R_BeamRing( int startEnt, int endEnt, int modelIndex, float life, float width, float amplitude, float brightness, float speed, int startFrame, float framerate, float r, float g, float b );
struct beam_s *R_BeamFollow( int startEnt, int modelIndex, float life, float width, float r, float g, float b, float brightness );
void R_BeamKill( int deadEntity );


// TriAPI
void TriRenderMode( int mode );
void TriColor4f( float r, float g, float b, float a );
void TriColor4ub( byte r, byte g, byte b, byte a );
void TriBrightness( float brightness );
void TriCullFace( TRICULLSTYLE mode );
int TriWorldToScreen( const float *world, float *screen );
int TriBoxInPVS( float *mins, float *maxs );
void TriLightAtPoint( float *pos, float *value );
void TriColor4fRendermode( float r, float g, float b, float a, int rendermode );
int TriSpriteTexture( model_t *pSpriteModel, int frame );

extern model_t	*cl_sprite_dot;
extern model_t	*cl_sprite_shell;

#endif//CL_TENT_H
