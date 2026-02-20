/*
cvar.h - dynamic variable tracking
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef CVAR_H
#define CVAR_H

#include "cvardef.h"

// As some mods dynamically allocate cvars and free them without notifying the engine
// let's construct a list of cvars that must be removed
typedef struct pending_cvar_s
{
	struct pending_cvar_s *next;

	convar_t *cv_cur; // preserve the data that might get freed
	convar_t *cv_next;
	qboolean  cv_allocated; // if it's allocated by us, it's safe to access cv_cur
	char      cv_name[];
} pending_cvar_t;

typedef void (*setpair_t)( const char *key, const void *value, const void *buffer, void *numpairs );

cvar_t *Cvar_GetList( void );
convar_t *Cvar_FindVar( const char *var_name );
void Cvar_RegisterVariable( convar_t *var );
convar_t *Cvar_Get( const char *var_name, const char *value, uint32_t flags, const char *description );
convar_t *Cvar_Getf( const char *var_name, uint32_t flags, const char *description, const char *format, ... ) FORMAT_CHECK( 4 );
void Cvar_LookupVars( int checkbit, void *buffer, void *ptr, setpair_t callback );
void Cvar_FullSet( const char *var_name, const char *value, uint32_t flags );
void Cvar_DirectSet( convar_t *var, const char *value );
void Cvar_DirectSetValue( convar_t *var, float value );
void Cvar_DirectFullSet( convar_t *var, const char *value, uint32_t flags );
void Cvar_Set( const char *var_name, const char *value );
void Cvar_SetValue( const char *var_name, float value );
const char *Cvar_BuildAutoDescription( const char *szName, uint32_t flags ) RETURNS_NONNULL;
float Cvar_VariableValue( const char *var_name );
int Cvar_VariableInteger( const char *var_name );
const char *Cvar_VariableString( const char *var_name ) RETURNS_NONNULL;
void Cvar_WriteVariables( file_t *f, int group );
qboolean Cvar_Exists( const char *var_name );
void Cvar_Reset( const char *var_name );
void Cvar_SetCheatState( void );
qboolean Cvar_CommandWithPrivilegeCheck( convar_t *v, qboolean isPrivileged );
void Cvar_Init( void );
void Cvar_Shutdown( void );
void Cvar_PostFSInit( void );
void Cvar_Unlink( uint32_t group );

pending_cvar_t *Cvar_PrepareToUnlink( uint32_t group );
void Cvar_UnlinkPendingCvars( pending_cvar_t *pending_cvars );

#endif//CVAR_H
