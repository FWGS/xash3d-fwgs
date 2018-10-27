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

#define CVAR_SENTINEL		0xDEADBEEF
#define CVAR_CHECK_SENTINEL( cv )	((uint)(cv)->next == CVAR_SENTINEL)

// NOTE: if this is changed, it must be changed in cvardef.h too
typedef struct convar_s
{
	// this part shared with cvar_t
	char		*name;
	char		*string;
	int		flags;
	float		value;
	struct convar_s	*next;

	// this part unique for convar_t
	char		*desc;		// variable descrition info
	char		*def_string;	// keep pointer to initial value
} convar_t;

// cvar internal flags
#define FCVAR_RENDERINFO		(1<<16)	// save to a seperate config called video.cfg
#define FCVAR_READ_ONLY		(1<<17)	// cannot be set by user at all, and can't be requested by CvarGetPointer from game dlls
#define FCVAR_EXTENDED		(1<<18)	// this is convar_t (sets on registration)
#define FCVAR_ALLOCATED		(1<<19)	// this convar_t is fully dynamic allocated (include description)
#define FCVAR_VIDRESTART		(1<<20)	// recreate the window is cvar with this flag was changed
#define FCVAR_TEMPORARY		(1<<21)	// these cvars holds their values and can be unlink in any time

#define CVAR_DEFINE( cv, cvname, cvstr, cvflags, cvdesc )	convar_t cv = { cvname, cvstr, cvflags, 0.0f, (void *)CVAR_SENTINEL, cvdesc }
#define CVAR_DEFINE_AUTO( cv, cvstr, cvflags, cvdesc )	convar_t cv = { #cv, cvstr, cvflags, 0.0f, (void *)CVAR_SENTINEL, cvdesc }
#define CVAR_TO_BOOL( x )		((x) && ((x)->value != 0.0f) ? true : false )

#define Cvar_FindVar( name )	Cvar_FindVarExt( name, 0 )
convar_t *Cvar_FindVarExt( const char *var_name, int ignore_group );
void Cvar_RegisterVariable( convar_t *var );
convar_t *Cvar_Get( const char *var_name, const char *value, int flags, const char *description );
void Cvar_LookupVars( int checkbit, void *buffer, void *ptr, setpair_t callback );
void Cvar_FullSet( const char *var_name, const char *value, int flags );
void Cvar_DirectSet( convar_t *var, const char *value );
void Cvar_Set( const char *var_name, const char *value );
void Cvar_SetValue( const char *var_name, float value );
const char *Cvar_BuildAutoDescription( int flags );
float Cvar_VariableValue( const char *var_name );
int Cvar_VariableInteger( const char *var_name );
char *Cvar_VariableString( const char *var_name );
void Cvar_WriteVariables( file_t *f, int group );
qboolean Cvar_Exists( const char *var_name );
void Cvar_Reset( const char *var_name );
void Cvar_SetCheatState( void );
qboolean Cvar_Command( void );
void Cvar_Init( void );
void Cvar_Unlink( int group );

#endif//CVAR_H