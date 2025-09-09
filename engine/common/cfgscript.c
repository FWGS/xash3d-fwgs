/*
cfgscript.c - "Valve script" parsing routines
Copyright (C) 2016 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"

typedef enum
{
	T_NONE = 0,
	T_BOOL,
	T_NUMBER,
	T_LIST,
	T_STRING,
	T_COUNT
} cvartype_t;

static const char *const cvartypes[] = { NULL, "BOOL", "NUMBER", "LIST", "STRING" };

typedef struct parserstate_s
{
	char		*buf;
	char		token[MAX_STRING];
	const char	*filename;
} parserstate_t;

typedef struct scrvardef_s
{
	char		name[MAX_STRING];
	char		value[MAX_STRING];
	char		desc[MAX_STRING];
	float		fMin, fMax;
	cvartype_t	type;
	int		flags;
	qboolean		fHandled;
} scrvardef_t;

/*
===================
CSCR_ExpectString

Return true if next token is pExpext and skip it
===================
*/
static qboolean CSCR_ExpectString( parserstate_t *ps, const char *pExpect, qboolean skip, qboolean error )
{
	char	*tmp = COM_ParseFile( ps->buf, ps->token, sizeof( ps->token ));

	if( !Q_stricmp( ps->token, pExpect ) )
	{
		ps->buf = tmp;
		return true;
	}

	if( skip ) ps->buf = tmp;
	if( error ) Con_DPrintf( S_ERROR "Syntax error in %s: got \"%s\" instead of \"%s\"\n", ps->filename, ps->token, pExpect );

	return false;
}

/*
===================
CSCR_ParseType

Determine script variable type
===================
*/
static cvartype_t CSCR_ParseType( parserstate_t *ps )
{
	int	i;

	for( i = 1; i < T_COUNT; i++ )
	{
		if( CSCR_ExpectString( ps, cvartypes[i], false, false ))
			return i;
	}

	Con_DPrintf( S_ERROR "Cannot parse %s: Bad type %s\n", ps->filename, ps->token );
	return T_NONE;
}



/*
=========================
CSCR_ParseSingleCvar
=========================
*/
static qboolean CSCR_ParseSingleCvar( parserstate_t *ps, scrvardef_t *result )
{
	// read the name
	ps->buf = COM_ParseFile( ps->buf, result->name, sizeof( result->name ));

	if( !CSCR_ExpectString( ps, "{", false, true ))
		return false;

	// read description
	ps->buf = COM_ParseFile( ps->buf, result->desc, sizeof( result->desc ));

	if( !CSCR_ExpectString( ps, "{", false, true ))
		return false;

	result->type = CSCR_ParseType( ps );

	switch( result->type )
	{
	case T_BOOL:
		// bool only has description
		if( !CSCR_ExpectString( ps, "}", false, true ))
			return false;
		break;
	case T_NUMBER:
		// min
		ps->buf = COM_ParseFile( ps->buf, ps->token, sizeof( ps->token ));
		result->fMin = Q_atof( ps->token );

		// max
		ps->buf = COM_ParseFile( ps->buf, ps->token, sizeof( ps->token ));
		result->fMax = Q_atof( ps->token );

		if( !CSCR_ExpectString( ps, "}", false, true ))
			return false;
		break;
	case T_STRING:
		if( !CSCR_ExpectString( ps, "}", false, true ))
			return false;
		break;
	case T_LIST:
		while( !CSCR_ExpectString( ps, "}", true, false ))
		{
			// read token for each item here
		}
		break;
	default:
		return false;
	}

	if( !CSCR_ExpectString( ps, "{", false, true ))
		return false;

	// default value
	ps->buf = COM_ParseFile( ps->buf, result->value, sizeof( result->value ));

	if( !CSCR_ExpectString( ps, "}", false, true ))
		return false;

	if( CSCR_ExpectString( ps, "SetInfo", false, false ))
		result->flags |= FCVAR_USERINFO;

	if( !CSCR_ExpectString( ps, "}", false, true ))
		return false;

	return true;
}

/*
======================
CSCR_ParseHeader

Check version and seek to first cvar name
======================
*/
static qboolean CSCR_ParseHeader( parserstate_t *ps )
{
	if( !CSCR_ExpectString( ps, "VERSION", false, true ))
		return false;

	// Parse in the version #
	// Get the first token.
	ps->buf = COM_ParseFile( ps->buf, ps->token, sizeof( ps->token ));

	if( Q_atof( ps->token ) != 1 )
	{
		Con_DPrintf( S_ERROR "File %s has wrong version %s!\n", ps->filename, ps->token );
		return false;
	}

	if( !CSCR_ExpectString( ps, "DESCRIPTION", false, true ))
		return false;

	ps->buf = COM_ParseFile( ps->buf, ps->token, sizeof( ps->token ));

	if( Q_stricmp( ps->token, "INFO_OPTIONS") && Q_stricmp( ps->token, "SERVER_OPTIONS" ))
	{
		Con_DPrintf( S_ERROR "DESCRIPTION must be INFO_OPTIONS or SERVER_OPTIONS\n");
		return false;
	}

	if( !CSCR_ExpectString( ps, "{", false, true ))
		return false;

	return true;
}

/*
==============
CSCR_ParseFile

generic scr parser
will callback on each scrvardef_t
==============
*/
static int CSCR_ParseFile( const char *scriptfilename,
	void (*callback)( scrvardef_t *var, void * ), void *userdata )
{
	parserstate_t	state = { 0 };
	qboolean		success = false;
	int		count = 0;
	fs_offset_t		length = 0;
	char		*start;

	state.filename = scriptfilename;
	state.buf = start = (char *)FS_LoadFile( scriptfilename, &length, true );

	if( !state.buf || !length )
		return 0;

	Con_DPrintf( "Reading config script file %s\n", scriptfilename );

	if( !CSCR_ParseHeader( &state ))
		goto finish;

	while( !CSCR_ExpectString( &state, "}", false, false ))
	{
		scrvardef_t	var = { 0 };

		// Create a new object
		if( CSCR_ParseSingleCvar( &state, &var ) )
		{
			callback( &var, userdata );
			count++;
		}
		else
			break;

		if( count > 1024 )
			break;
	}

	if( COM_ParseFile( state.buf, state.token, sizeof( state.token )))
		Con_DPrintf( S_ERROR "Got extra tokens!\n" );
	else success = true;
finish:
	if( !success )
	{
		state.token[sizeof( state.token ) - 1] = 0;
		if( start && state.buf )
			Con_DPrintf( S_ERROR "Parse error in %s, byte %d, token %s\n", scriptfilename, (int)( state.buf - start ), state.token );
		else Con_DPrintf( S_ERROR "Parse error in %s, token %s\n", scriptfilename, state.token );
	}

	if( start ) Mem_Free( start );

	return count;
}

static void CSCR_WriteVariableToFile( scrvardef_t *var, void *file )
{
	file_t   *cfg  = (file_t*)file;
	convar_t *cvar = Cvar_FindVar( var->name );

	if( cvar && !FBitSet( cvar->flags, FCVAR_SERVER|FCVAR_ARCHIVE ))
	{
		// cvars will be placed in game.cfg and restored on map start
		if( var->flags & FCVAR_USERINFO )
			FS_Printf( cfg, "setinfo %s \"%s\"\n", var->name, cvar->string );
		else FS_Printf( cfg, "%s \"%s\"\n", var->name, cvar->string );
	}
}

/*
======================
CSCR_WriteGameCVars

Print all cvars declared in script to game.cfg file
======================
*/
int CSCR_WriteGameCVars( file_t *cfg, const char *scriptfilename )
{
	return CSCR_ParseFile( scriptfilename, CSCR_WriteVariableToFile, cfg );
}

static void CSCR_RegisterVariable( scrvardef_t *var, void *unused )
{
	if( !Cvar_FindVar( var->name ))
		Cvar_Get( var->name, var->value, var->flags|FCVAR_TEMPORARY, var->desc );
}

/*
======================
CSCR_LoadDefaultCVars

Register all cvars declared in config file and set default values
======================
*/
int CSCR_LoadDefaultCVars( const char *scriptfilename )
{
	return CSCR_ParseFile( scriptfilename, CSCR_RegisterVariable, NULL );
}
