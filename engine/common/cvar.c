/*
cvar.c - dynamic variable tracking
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <math.h>	// fabs...
#include "common.h"
#include "base_cmd.h"
#include "eiface.h" // ARRAYSIZE

convar_t	*cvar_vars = NULL; // head of list
convar_t	*cmd_scripting;

#ifdef HACKS_RELATED_HLMODS
typedef struct cvar_filter_quirks_s
{
	const char *gamedir; // gamedir to enable for
	const char *cvars; // list of cvars should be excluded from filter
} cvar_filter_quirks_t;

static cvar_filter_quirks_t cvar_filter_quirks[] =
{
	// EXAMPLE:
	//{
	//	"valve",
	//	"test;test1;test100"
	//},
	{
		"ricochet",
		"r_drawviewmodel",
	},
};

static cvar_filter_quirks_t *cvar_active_filter_quirks = NULL;
#endif

CVAR_DEFINE_AUTO( cl_filterstuffcmd, "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "filter commands coming from server" );

/*
============
Cvar_GetList
============
*/
cvar_t *GAME_EXPORT Cvar_GetList( void )
{
	return (cvar_t *)cvar_vars;
}


/*
============
Cvar_FindVar

find the specified variable by name
============
*/
convar_t *Cvar_FindVarExt( const char *var_name, int ignore_group )
{
	// TODO: ignore group for cvar
#if defined(XASH_HASHED_VARS)
	return (convar_t *)BaseCmd_Find( HM_CVAR, var_name );
#else
	convar_t	*var;

	if( !var_name )
		return NULL;

	for( var = cvar_vars; var; var = var->next )
	{
		if( ignore_group && FBitSet( ignore_group, var->flags ))
			continue;

		if( !Q_stricmp( var_name, var->name ))
			return var;
	}

	return NULL;
#endif
}

/*
============
Cvar_BuildAutoDescription

build cvar auto description that based on the setup flags
============
*/
const char *Cvar_BuildAutoDescription( int flags )
{
	static char	desc[256];

	desc[0] = '\0';

	if( FBitSet( flags, FCVAR_EXTDLL ))
		Q_strncpy( desc, "game ", sizeof( desc ));
	else if( FBitSet( flags, FCVAR_CLIENTDLL ))
		Q_strncpy( desc, "client ", sizeof( desc ));
	else if( FBitSet( flags, FCVAR_GAMEUIDLL ))
		Q_strncpy( desc, "GameUI ", sizeof( desc ));

	if( FBitSet( flags, FCVAR_SERVER ))
		Q_strncat( desc, "server ", sizeof( desc ));

	if( FBitSet( flags, FCVAR_USERINFO ))
		Q_strncat( desc, "user ", sizeof( desc ));

	if( FBitSet( flags, FCVAR_ARCHIVE ))
		Q_strncat( desc, "archived ", sizeof( desc ));

	if( FBitSet( flags, FCVAR_PROTECTED ))
		Q_strncat( desc, "protected ", sizeof( desc ));

	if( FBitSet( flags, FCVAR_PRIVILEGED ))
		Q_strncat( desc, "privileged ", sizeof( desc ));

	Q_strncat( desc, "cvar", sizeof( desc ));

	return desc;
}

/*
============
Cvar_UpdateInfo

deal with userinfo etc
============
*/
static qboolean Cvar_UpdateInfo( convar_t *var, const char *value, qboolean notify )
{
	if( FBitSet( var->flags, FCVAR_USERINFO ))
	{
		if ( Host_IsDedicated() )
		{
			// g-cont. this is a very strange behavior...
			Info_SetValueForKey( SV_Serverinfo(), var->name, value, MAX_SERVERINFO_STRING ),
			SV_BroadcastCommand( "fullserverinfo \"%s\"\n", SV_Serverinfo( ));
		}
#if !XASH_DEDICATED
		else
		{
			if( !Info_SetValueForKey( CL_Userinfo(), var->name, value, MAX_INFO_STRING ))
				return false; // failed to change value

			// time to update server copy of userinfo
			CL_ServerCommand( true, "setinfo \"%s\" \"%s\"\n", var->name, value );
			CL_LegacyUpdateInfo();
		}
#endif
	}

	if( FBitSet( var->flags, FCVAR_SERVER ) && notify )
	{
		if( !FBitSet( var->flags, FCVAR_UNLOGGED ))
		{
			if( FBitSet( var->flags, FCVAR_PROTECTED ))
			{
				Log_Printf( "Server cvar \"%s\" = \"%s\"\n", var->name, "***PROTECTED***" );
				SV_BroadcastPrintf( NULL, "\"%s\" changed to \"%s\"\n", var->name, "***PROTECTED***" );
			}
			else
			{
				Log_Printf( "Server cvar \"%s\" = \"%s\"\n", var->name, value );
				SV_BroadcastPrintf( NULL, "\"%s\" changed to \"%s\"\n", var->name, value );
			}
		}
	}

	return true;
}

/*
============
Cvar_ValidateString

deal with userinfo etc
============
*/
const char *Cvar_ValidateString( convar_t *var, const char *value )
{
	const char	*pszValue;
	static char	szNew[MAX_STRING];

	pszValue = value;
	szNew[0] = 0;

	// this cvar's string must only contain printable characters.
	// strip out any other crap. we'll fill in "empty" if nothing is left
	if( FBitSet( var->flags, FCVAR_PRINTABLEONLY ))
	{
		char	*szVal = szNew;
		int	len = 0;

		// step through the string, only copying back in characters that are printable
		while( *pszValue && len < MAX_STRING )
		{
			if( ((byte)*pszValue) < 32 )
			{
				pszValue++;
				continue;
			}
			*szVal++ = *pszValue++;
			len++;
		}

		*szVal = '\0';
		pszValue = szNew;

		// g-cont. is this even need?
		if( !COM_CheckStringEmpty( szNew ) ) Q_strncpy( szNew, "empty", sizeof( szNew ));
	}

	if( FBitSet( var->flags, FCVAR_NOEXTRAWHITEPACE ))
	{
		char	*szVal = szNew;
		int	len = 0;

		// step through the string, only copying back in characters that are printable
		while( *pszValue && len < MAX_STRING )
		{
			if( *pszValue == ' ' )
			{
				pszValue++;
				continue;
			}
			*szVal++ = *pszValue++;
			len++;
		}

		*szVal = '\0';
		pszValue = szNew;
	}

	return pszValue;
}

/*
============
Cvar_ValidateVarName
============
*/
static qboolean Cvar_ValidateVarName( const char *s, qboolean isvalue )
{
	if( !s )
		return false;
	if( Q_strchr( s, '\\' ) && !isvalue )
		return false;
	if( Q_strchr( s, '\"' ))
		return false;
	if( Q_strchr( s, ';' ) && !isvalue )
		return false;
	return true;
}

/*
============
Cvar_UnlinkVar

unlink the variable
============
*/
int Cvar_UnlinkVar( const char *var_name, int group )
{
	int	count = 0;
	convar_t	**prev;
	convar_t	*var;

	prev = &cvar_vars;

	while( 1 )
	{
		var = *prev;
		if( !var ) break;

		// do filter by name
		if( var_name && Q_strcmp( var->name, var_name ))
		{
			prev = &var->next;
			continue;
		}

		// do filter by specified group
		if( group && !FBitSet( var->flags, group ))
		{
			prev = &var->next;
			continue;
		}

#if defined(XASH_HASHED_VARS)
		BaseCmd_Remove( HM_CVAR, var->name );
#endif

		// unlink variable from list
		freestring( var->string );
		*prev = var->next;

		// only allocated cvars can throw these fields
		if( FBitSet( var->flags, FCVAR_ALLOCATED ))
		{
			freestring( var->name );
			freestring( var->def_string );
			freestring( var->desc );
			Mem_Free( var );
		}
		count++;
	}

	return count;
}

/*
============
Cvar_Changed

Tell the engine parts about cvar changing
============
*/
static void Cvar_Changed( convar_t *var )
{
	Assert( var != NULL );

	// tell about changes
	SetBits( var->flags, FCVAR_CHANGED );

	// tell the engine parts with global state
	if( FBitSet( var->flags, FCVAR_USERINFO ))
		host.userinfo_changed = true;

	if( FBitSet( var->flags, FCVAR_MOVEVARS ))
		host.movevars_changed = true;

	if( FBitSet( var->flags, FCVAR_VIDRESTART ))
		host.renderinfo_changed = true;

	if( !Q_strcmp( var->name, "sv_cheats" ))
		host.allow_cheats = Q_atoi( var->string );
}

/*
============
Cvar_LookupVars
============
*/
void Cvar_LookupVars( int checkbit, void *buffer, void *ptr, setpair_t callback )
{
	convar_t	*var;

	// nothing to process ?
	if( !callback ) return;

	// force checkbit to 0 for lookup all cvars
	for( var = cvar_vars; var; var = var->next )
	{
		if( checkbit && !FBitSet( var->flags, checkbit ))
			continue;

		if( buffer )
		{
			callback( var->name, var->string, buffer, ptr );
		}
		else
		{
			// NOTE: dlls cvars doesn't have description
			if( FBitSet( var->flags, FCVAR_ALLOCATED|FCVAR_EXTENDED ))
				callback( var->name, var->string, var->desc, ptr );
			else callback( var->name, var->string, "", ptr );
		}
	}
}

/*
============
Cvar_Get

If the variable already exists, the value will not be set
The flags will be or'ed in if the variable exists.
============
*/
convar_t *Cvar_Get( const char *name, const char *value, int flags, const char *var_desc )
{
	convar_t	*cur, *find, *var;

	ASSERT( name && *name );

	// check for command coexisting
	if( Cmd_Exists( name ))
	{
		Con_DPrintf( S_ERROR "can't register variable '%s', is already defined as command\n", name );
		return NULL;
	}

	var = Cvar_FindVar( name );

	if( var )
	{
		// already existed?
		if( FBitSet( flags, FCVAR_GLCONFIG ))
		{
			// NOTE: cvars without description produced by Cvar_FullSet
			// which executed from the config file. So we don't need to
			// change value here: we *already* have actual value from config.
			// in other cases we need to rewrite them
			if( COM_CheckStringEmpty( var->desc ))
			{
				// directly set value
				freestring( var->string );
				var->string = copystring( value );
				var->value = Q_atof( var->string );
				SetBits( var->flags, flags );

				// tell engine about changes
				Cvar_Changed( var );
			}
		}
		else
		{
			SetBits( var->flags, flags );
			Cvar_DirectSet( var, value );
		}

		if( FBitSet( var->flags, FCVAR_ALLOCATED ) && Q_strcmp( var_desc, var->desc ))
		{
			if( !FBitSet( flags, FCVAR_GLCONFIG ))
				Con_Reportf( "%s change description from %s to %s\n", var->name, var->desc, var_desc );
			// update description if needs
			freestring( var->desc );
			var->desc = copystring( var_desc );
		}

		return var;
	}

	// allocate a new cvar
	var = Z_Malloc( sizeof( *var ));
	var->name = copystring( name );
	var->string = copystring( value );
	var->def_string = copystring( value );
	var->desc = copystring( var_desc );
	var->value = Q_atof( var->string );
	var->flags = flags|FCVAR_ALLOCATED;

	// link the variable in alphanumerical order
	for( cur = NULL, find = cvar_vars; find && Q_strcmp( find->name, var->name ) < 0; cur = find, find = find->next );

	if( cur ) cur->next = var;
	else cvar_vars = var;
	var->next = find;

	// fill it cls.userinfo, svs.serverinfo
	Cvar_UpdateInfo( var, var->string, false );

	// tell engine about changes
	Cvar_Changed( var );

#if defined(XASH_HASHED_VARS)
	// add to map
	BaseCmd_Insert( HM_CVAR, var, var->name );
#endif

	return var;
}

/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable( convar_t *var )
{
	convar_t	*cur, *find, *dup;

	ASSERT( var != NULL );

	// first check to see if it has allready been defined
	dup = Cvar_FindVar( var->name );

	if( dup )
	{
		if( !FBitSet( dup->flags, FCVAR_TEMPORARY ))
		{
			Con_DPrintf( S_ERROR "can't register variable '%s', is already defined\n", var->name );
			return;
		}

		// time to replace temp variable with real
		Cvar_UnlinkVar( var->name, FCVAR_TEMPORARY );
	}

	// check for overlap with a command
	if( Cmd_Exists( var->name ))
	{
		Con_DPrintf( S_ERROR "can't register variable '%s', is already defined as command\n", var->name );
		return;
	}

	// NOTE: all the 'long' engine cvars have an special setntinel on static declaration
	// (all the engine cvars should be declared through CVAR_DEFINE macros or they shouldn't working properly anyway)
	// so we can determine long version 'convar_t' and short version 'cvar_t' more reliable than by FCVAR_EXTDLL flag
	if( CVAR_CHECK_SENTINEL( var )) SetBits( var->flags, FCVAR_EXTENDED );

	// copy the value off, because future sets will free it
	if( FBitSet( var->flags, FCVAR_EXTENDED ))
		var->def_string = var->string; // just swap pointers

	var->string = copystring( var->string );
	var->value = Q_atof( var->string );

	// find the supposed position in chain (alphanumerical order)
	for( cur = NULL, find = cvar_vars; find && Q_strcmp( find->name, var->name ) < 0; cur = find, find = find->next );

	// now link variable
	if( cur ) cur->next = var;
	else cvar_vars = var;
	var->next = find;

	// fill it cls.userinfo, svs.serverinfo
	Cvar_UpdateInfo( var, var->string, false );

	// tell engine about changes
	Cvar_Changed( var );

#if defined(XASH_HASHED_VARS)
	// add to map
	BaseCmd_Insert( HM_CVAR, var, var->name );
#endif
}

/*
============
Cvar_Set2
============
*/
static convar_t *Cvar_Set2( const char *var_name, const char *value )
{
	convar_t	*var;
	const char	*pszValue;
	qboolean	dll_variable = false;
	qboolean	force = false;

	if( !Cvar_ValidateVarName( var_name, false ))
	{
		Con_DPrintf( S_ERROR "Invalid cvar name string: %s\n", var_name );
		return NULL;
	}

	var = Cvar_FindVar( var_name );
	if( !var )
	{
		// if cvar not found, create it
		return Cvar_Get( var_name, value, FCVAR_USER_CREATED, NULL );
	}
	else
	{	
		if( !Cmd_CurrentCommandIsPrivileged( ))
		{
			if( FBitSet( var->flags, FCVAR_PRIVILEGED ))
			{
				Con_Printf( "%s is priveleged.\n", var->name );
				return var;
			}

			if( cl_filterstuffcmd.value > 0.0f && FBitSet( var->flags, FCVAR_FILTERABLE ))
			{
				Con_Printf( "%s is filterable.\n", var->name );
				return var;
			}
		}
	}

	// use this check to prevent acessing for unexisting fields
	// for cvar_t: latched_string, description, etc
	dll_variable = FBitSet( var->flags, FCVAR_EXTDLL );

	// check value
	if( !value )
	{
		if( !FBitSet( var->flags, FCVAR_EXTENDED|FCVAR_ALLOCATED ))
		{
			Con_Printf( "%s has no default value and can't be reset.\n", var->name );
			return var;
		}

		if( dll_variable )
			value = "0";
		else
			value = var->def_string; // reset to default value
	}

	if( !Q_strcmp( value, var->string ))
		return var;

	// any latched values not allowed for game cvars
	if( dll_variable )
		force = true;

	if( !force )
	{ 
		if( FBitSet( var->flags, FCVAR_READ_ONLY ))
		{
			Con_Printf( "%s is read-only.\n", var->name );
			return var;
		}

		if( FBitSet( var->flags, FCVAR_CHEAT ) && !host.allow_cheats )
		{
			Con_Printf( "%s is cheat protected.\n", var->name );
			return var;
		}

		// just tell user about deferred changes
		if( FBitSet( var->flags, FCVAR_LATCH ) && ( SV_Active() || CL_Active( )))
			Con_Printf( "%s will be changed upon restarting.\n", var->name );
	}

	pszValue = Cvar_ValidateString( var, value );

	// nothing to change
	if( !Q_strcmp( pszValue, var->string ))
		return var;

	// fill it cls.userinfo, svs.serverinfo
	if( !Cvar_UpdateInfo( var, pszValue, true ))
		return var;

	// and finally changed the cvar itself
	freestring( var->string );
	var->string = copystring( pszValue );
	var->value = Q_atof( var->string );

	// tell engine about changes
	Cvar_Changed( var );
	return var;
}

/*
============
Cvar_DirectSet

way to change value for many cvars
============
*/
void Cvar_DirectSet( convar_t *var, const char *value )
{
	const char	*pszValue;

	if( !var ) return;	// ???

	// lookup for registration
	if( CVAR_CHECK_SENTINEL( var ) || ( var->next == NULL && !FBitSet( var->flags, FCVAR_EXTENDED|FCVAR_ALLOCATED )))
	{
		// need to registering cvar fisrt
		Cvar_RegisterVariable( var );	// ok, register it
	}

	// lookup for registration again
	if( var != Cvar_FindVar( var->name ))
		return; // how this possible?

	if( FBitSet( var->flags, FCVAR_READ_ONLY ))
	{
		Con_Printf( "%s is read-only.\n", var->name );
		return;
	}

	if( FBitSet( var->flags, FCVAR_CHEAT ) && !host.allow_cheats )
	{
		Con_Printf( "%s is cheat protected.\n", var->name );
		return;
	}

	// just tell user about deferred changes
	if( FBitSet( var->flags, FCVAR_LATCH ) && ( SV_Active() || CL_Active( )))
		Con_Printf( "%s will be changed upon restarting.\n", var->name );

	// check value
	if( !value )
	{
		if( !FBitSet( var->flags, FCVAR_EXTENDED|FCVAR_ALLOCATED ))
		{
			Con_Printf( "%s has no default value and can't be reset.\n", var->name );
			return;
		}

		value = var->def_string; // reset to default value
	}

	pszValue = Cvar_ValidateString( var, value );

	// nothing to change
	if( !Q_strcmp( pszValue, var->string ))
		return;

	// fill it cls.userinfo, svs.serverinfo
	if( !Cvar_UpdateInfo( var, pszValue, true ))
		return;

	// and finally changed the cvar itself
	freestring( var->string );
	var->string = copystring( pszValue );
	var->value = Q_atof( var->string );

	// tell engine about changes
	Cvar_Changed( var );
}

/*
============
Cvar_FullSet

can set any protected cvars
============
*/
void Cvar_FullSet( const char *var_name, const char *value, int flags )
{
	convar_t	*var = Cvar_FindVar( var_name );

	if( !var )
	{
		Cvar_Get( var_name, value, flags, "" );
		return;
	}

	freestring( var->string );
	var->string = copystring( value );
	var->value = Q_atof( var->string );
	SetBits( var->flags, flags );

	// tell engine about changes
	Cvar_Changed( var );
}

/*
============
Cvar_Set
============
*/
void GAME_EXPORT Cvar_Set( const char *var_name, const char *value )
{
	convar_t	*var;

	if( !var_name )
	{
		// there is an error in C code if this happens
		Con_Printf( "Cvar_Set: passed NULL variable name\n" );
		return;
	}

	var = Cvar_FindVar( var_name );

	if( !var )
	{
		// there is an error in C code if this happens
		Con_Printf( "Cvar_Set: variable '%s' not found\n", var_name );
		return;
	}

	Cvar_DirectSet( var, value );
}

/*
============
Cvar_SetValue
============
*/
void GAME_EXPORT Cvar_SetValue( const char *var_name, float value )
{
	char	val[32];

	if( fabs( value - (int)value ) < 0.000001 )
		Q_snprintf( val, sizeof( val ), "%d", (int)value );
	else Q_snprintf( val, sizeof( val ), "%f", value );

	Cvar_Set( var_name, val );
}

/*
============
Cvar_Reset
============
*/
void Cvar_Reset( const char *var_name )
{
	Cvar_Set( var_name, NULL );
}

/*
============
Cvar_VariableValue
============
*/
float GAME_EXPORT Cvar_VariableValue( const char *var_name )
{
	convar_t	*var;

	if( !var_name )
	{
		// there is an error in C code if this happens
		Con_Printf( "Cvar_VariableValue: passed NULL variable name\n" );
		return 0.0f;
	}

	var = Cvar_FindVar( var_name );
	if( !var ) return 0.0f;

	return Q_atof( var->string );
}

/*
============
Cvar_VariableInteger
============
*/
int Cvar_VariableInteger( const char *var_name )
{
	convar_t	*var;

	var = Cvar_FindVar( var_name );
	if( !var ) return 0;

	return Q_atoi( var->string );
}

/*
============
Cvar_VariableString
============
*/
const char *Cvar_VariableString( const char *var_name )
{
	convar_t	*var;

	if( !var_name )
	{
		// there is an error in C code if this happens
		Con_Printf( "Cvar_VariableString: passed NULL variable name\n" );
		return "";
	}

	var = Cvar_FindVar( var_name );
	if( !var ) return "";

	return var->string;
}

/*
============
Cvar_Exists
============
*/
qboolean Cvar_Exists( const char *var_name )
{
	if( Cvar_FindVar( var_name ))
		return true;
	return false;
}

/*
============
Cvar_SetCheatState

Any testing variables will be reset to the safe values
============
*/
void Cvar_SetCheatState( void )
{
	convar_t	*var;

	// set all default vars to the safe value
	for( var = cvar_vars; var; var = var->next )
	{
		// can't process dll cvars - missed def_string
		if( !FBitSet( var->flags, FCVAR_ALLOCATED|FCVAR_EXTENDED ))
			continue;

		if( FBitSet( var->flags, FCVAR_CHEAT ))
		{
			if( Q_strcmp( var->def_string, var->string ))
				Cvar_DirectSet( var, var->def_string );
		}
	}
}

/*
============
Cvar_SetGL

As Cvar_Set, but also flags it as glconfig
============
*/
static void Cvar_SetGL( const char *name, const char *value )
{
	convar_t *var = Cvar_FindVar( name );

	if( var && !FBitSet( var->flags, FCVAR_GLCONFIG ))
	{
		Con_Reportf( S_ERROR "Can't set non-GL cvar %s to %s\n", name, value );
		return;
	}

	Cvar_FullSet( name, value, FCVAR_GLCONFIG );
}

static qboolean Cvar_ShouldSetCvar( convar_t *v, qboolean isPrivileged )
{
	const char *prefixes[] = { "cl_", "gl_", "m_", "r_", "hud_" };
	int i;

	if( isPrivileged )
		return true;

	if( FBitSet( v->flags, FCVAR_PRIVILEGED ))
		return false;

	if( cl_filterstuffcmd.value <= 0.0f )
		return true;

#ifdef HACKS_RELATED_HLMODS
	// check if game-specific filter exceptions should be applied
	// TODO: for cmd exceptions, make generic function
	if( cvar_active_filter_quirks )
	{
		const char *cur, *next;

		cur = cvar_active_filter_quirks->cvars;
		next = Q_strchr( cur, ';' );

		// TODO: implement Q_strchrnul
		while( cur && *cur )
		{
			size_t len = next ? next - cur : Q_strlen( cur );

			// found, quit
			if( !Q_strnicmp( cur, v->name, len ))
				return true;

			if( next )
			{
				cur = next + 1;
				next = Q_strchr( cur, ';' );
			}
			else
			{
				// stop
				cur = NULL;
			}
		}
	}
#endif

	if( FBitSet( v->flags, FCVAR_FILTERABLE ))
		return false;

	for( i = 0; i < ARRAYSIZE( prefixes ); i++ )
	{
		if( !Q_strnicmp( v->name, prefixes[i], Q_strlen( prefixes[i] )))
			return false;
	}

	return true;
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean Cvar_CommandWithPrivilegeCheck( convar_t *v, qboolean isPrivileged )
{
	// special case for setup opengl configuration
	if( host.apply_opengl_config )
	{
		Cvar_SetGL( Cmd_Argv( 0 ), Cmd_Argv( 1 ) );
		return true;
	}

	// check variables
	if( !v ) // already found in basecmd
		v = Cvar_FindVar( Cmd_Argv( 0 ));
	if( !v )
		return false;

	// perform a variable print or set
	if( Cmd_Argc() == 1 )
	{
		if( FBitSet( v->flags, FCVAR_ALLOCATED|FCVAR_EXTENDED ))
			Con_Printf( "\"%s\" is \"%s\" ( ^3\"%s\"^7 )\n", v->name, v->string, v->def_string );
		else Con_Printf( "\"%s\" is \"%s\"\n", v->name, v->string );

		return true;
	}

	if( host.apply_game_config )
	{
		if( !FBitSet( v->flags, FCVAR_EXTDLL ))
			return true; // only game.dll cvars passed
	}

	if( FBitSet( v->flags, FCVAR_SPONLY ) && CL_GetMaxClients() > 1 )
	{
		Con_Printf( "can't set \"%s\" in multiplayer\n", v->name );
		return false;
	}
	else if( !Cvar_ShouldSetCvar( v, isPrivileged ))
	{
		Con_Printf( "%s is a privileged variable\n", v->name );
		return true;
	}
	else
	{
		Cvar_DirectSet( v, Cmd_Argv( 1 ));
		return true;
	}
}

/*
============
Cvar_WriteVariables

Writes lines containing "variable value" for all variables
with the specified flag set to true.
============
*/
void Cvar_WriteVariables( file_t *f, int group )
{
	convar_t	*var;

	for( var = cvar_vars; var; var = var->next )
	{
		if( FBitSet( var->flags, group ))
			FS_Printf( f, "%s \"%s\"\n", var->name, var->string );
	}
}

/*
============
Cvar_Toggle_f

Toggles a cvar for easy single key binding
============
*/
void Cvar_Toggle_f( void )
{
	int	v;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "toggle <variable>\n" );
		return;
	}

	v = !Cvar_VariableInteger( Cmd_Argv( 1 ));

	Cvar_Set( Cmd_Argv( 1 ), va( "%i", v ));
}

/*
============
Cvar_Set_f

Allows setting and defining of arbitrary cvars from console, even if they
weren't declared in C code.
============
*/
void Cvar_Set_f( void )
{
	int	i, c, l = 0, len;
	char	combined[MAX_CMD_TOKENS];

	c = Cmd_Argc();
	if( c < 3 )
	{
		Msg( S_USAGE "set <variable> <value>\n" );
		return;
	}
	combined[0] = 0;

	for( i = 2; i < c; i++ )
	{
		len = Q_strlen( Cmd_Argv(i) + 1 );
		if( l + len >= MAX_CMD_TOKENS - 2 )
			break;
		Q_strcat( combined, Cmd_Argv( i ));
		if( i != c-1 ) Q_strcat( combined, " " );
		l += len;
	}

	Cvar_Set2( Cmd_Argv( 1 ), combined );
}

/*
============
Cvar_SetGL_f

As Cvar_Set, but also flags it as glconfig
============
*/
void Cvar_SetGL_f( void )
{
	convar_t *var;

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "setgl <variable> <value>\n" );
		return;
	}

	Cvar_SetGL( Cmd_Argv( 1 ), Cmd_Argv( 2 ) );
}

/*
============
Cvar_Reset_f
============
*/
void Cvar_Reset_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "reset <variable>\n" );
		return;
	}

	Cvar_Reset( Cmd_Argv( 1 ));
}

/*
============
Cvar_List_f
============
*/
void Cvar_List_f( void )
{
	convar_t	*var;
	const char	*match = NULL;
	char	*value;
	int	count = 0;
	size_t	matchlen = 0;

	if( Cmd_Argc() > 1 )
	{
		match = Cmd_Argv( 1 );
		matchlen = Q_strlen( match );
	}

	for( var = cvar_vars; var; var = var->next )
	{
		if( var->name[0] == '@' )
			continue;	// never shows system cvars

		if( match && !Q_strnicmpext( match, var->name, matchlen ))
			continue;

		if( Q_colorstr( var->string ))
			value = va( "\"%s\"", var->string );
		else value = va( "\"^2%s^7\"", var->string );

		if( FBitSet( var->flags, FCVAR_EXTENDED|FCVAR_ALLOCATED ))
			Con_Printf( " %-*s %s ^3%s^7\n", 32, var->name, value, var->desc );
		else Con_Printf( " %-*s %s ^3%s^7\n", 32, var->name, value, Cvar_BuildAutoDescription( var->flags ));

		count++;
	}

	Con_Printf( "\n%i cvars\n", count );
}

/*
============
Cvar_Unlink

unlink all cvars with specified flag
============
*/
void Cvar_Unlink( int group )
{
	int	count;

	if( Cvar_VariableInteger( "host_gameloaded" ) && FBitSet( group, FCVAR_EXTDLL ))
		return;

	if( Cvar_VariableInteger( "host_clientloaded" ) && FBitSet( group, FCVAR_CLIENTDLL ))
		return;

	if( Cvar_VariableInteger( "host_gameuiloaded" ) && FBitSet( group, FCVAR_GAMEUIDLL ))
		return;

	count = Cvar_UnlinkVar( NULL, group );
	Con_Reportf( "unlink %i cvars\n", count );
}

/*
============
Cvar_Init

Reads in all archived cvars
============
*/
void Cvar_Init( void )
{
	cvar_vars = NULL;
	cvar_active_filter_quirks = NULL;
	cmd_scripting = Cvar_Get( "cmd_scripting", "0", FCVAR_ARCHIVE|FCVAR_PRIVILEGED, "enable simple condition checking and variable operations" );
	Cvar_RegisterVariable( &host_developer ); // early registering for dev
	Cvar_RegisterVariable( &cl_filterstuffcmd );
	Cmd_AddRestrictedCommand( "setgl", Cvar_SetGL_f, "change the value of a opengl variable" );	// OBSOLETE
	Cmd_AddRestrictedCommand( "toggle", Cvar_Toggle_f, "toggles a console variable's values (use for more info)" );
	Cmd_AddRestrictedCommand( "reset", Cvar_Reset_f, "reset any type variable to initial value" );
	Cmd_AddCommand( "set", Cvar_Set_f, "create or change the value of a console variable" );
	Cmd_AddCommand( "cvarlist", Cvar_List_f, "display all console variables beginning with the specified prefix" );
}

/*
============
Cvar_PostFSInit

============
*/
void Cvar_PostFSInit( void )
{
	int i;

	for( i = 0; i < ARRAYSIZE( cvar_filter_quirks ); i++ )
	{
		if( !Q_stricmp( cvar_filter_quirks[i].gamedir, GI->gamefolder ))
		{
			cvar_active_filter_quirks = &cvar_filter_quirks[i];
			break;
		}
	}
}

#if XASH_ENGINE_TESTS
#include "tests.h"

void Test_RunCvar( void )
{
	convar_t *test_privileged = Cvar_Get( "test_privileged", "0", FCVAR_PRIVILEGED, "bark bark" );
	convar_t *test_unprivileged = Cvar_Get( "test_unprivileged", "0", 0, "meow meow" );
	convar_t *hud_filtered = Cvar_Get( "hud_filtered", "0", 0, "dummy description" );
	convar_t *filtered2 = Cvar_Get( "filtered2", "0", FCVAR_FILTERABLE, "filtered2" );

	Cbuf_AddText( "test_privileged 1; test_unprivileged 1; hud_filtered 1; filtered2 1\n" );
	Cbuf_Execute();
	TASSERT( test_privileged->value   != 0.0f );
	TASSERT( test_unprivileged->value != 0.0f );
	TASSERT( hud_filtered->value      != 0.0f );
	TASSERT( filtered2->value         != 0.0f );

	Cvar_DirectSet( test_privileged,   "0" );
	Cvar_DirectSet( test_unprivileged, "0" );
	Cvar_DirectSet( hud_filtered,      "0" );
	Cvar_DirectSet( filtered2,         "0" );
	Cvar_DirectSet( &cl_filterstuffcmd, "0" );
	Cbuf_AddFilteredText( "test_privileged 1; test_unprivileged 1; hud_filtered 1; filtered2 1\n" );
	Cbuf_Execute();
	Cbuf_Execute();
	Cbuf_Execute();
	TASSERT( test_privileged->value   == 0.0f );
	TASSERT( test_unprivileged->value != 0.0f );
	TASSERT( hud_filtered->value      != 0.0f );
	TASSERT( filtered2->value         != 0.0f );

	Cvar_DirectSet( test_privileged,   "0" );
	Cvar_DirectSet( test_unprivileged, "0" );
	Cvar_DirectSet( hud_filtered,      "0" );
	Cvar_DirectSet( filtered2,         "0" );
	Cvar_DirectSet( &cl_filterstuffcmd, "1" );
	Cbuf_AddFilteredText( "test_privileged 1; test_unprivileged 1; hud_filtered 1; filtered2 1\n" );
	Cbuf_Execute();
	Cbuf_Execute();
	Cbuf_Execute();
	TASSERT( test_privileged->value   == 0.0f );
	TASSERT( test_unprivileged->value != 0.0f );
	TASSERT( hud_filtered->value      == 0.0f );
	TASSERT( filtered2->value         == 0.0f );
}
#endif
