/*
cmd.c - script command processing module
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

#include "common.h"
#include "client.h"
#include "server.h"
#include "base_cmd.h"

#define MAX_CMD_BUFFER	32768
#define MAX_CMD_LINE	2048
#define MAX_ALIAS_NAME	32

typedef struct
{
	byte *const data;
	const int maxsize;
	int cursize;
} cmdbuf_t;

static qboolean cmd_wait;
static byte     cmd_text_buf[MAX_CMD_BUFFER];
static byte     filteredcmd_text_buf[MAX_CMD_BUFFER];
static cmdbuf_t cmd_text =
{
	.data = cmd_text_buf,
	.maxsize = ARRAYSIZE( cmd_text_buf ),
};
static cmdbuf_t filteredcmd_text =
{
	.data = filteredcmd_text_buf,
	.maxsize = ARRAYSIZE( filteredcmd_text_buf ),
};
static cmdalias_t *cmd_alias;
static uint cmd_condition;
static int  cmd_condlevel;
static qboolean cmd_currentCommandIsPrivileged;
static poolhandle_t cmd_pool;

static void Cmd_ExecuteStringWithPrivilegeCheck( const char *text, qboolean isPrivileged );

/*
=============================================================================

			COMMAND BUFFER

=============================================================================
*/

/*
============
Cbuf_Clear
============
*/
void Cbuf_Clear( void )
{
	memset( cmd_text.data, 0, cmd_text.maxsize );
	memset( filteredcmd_text.data, 0, filteredcmd_text.maxsize );
	cmd_text.cursize = filteredcmd_text.cursize = 0;
}

/*
============
Cbuf_GetSpace
============
*/
static void *Cbuf_GetSpace( cmdbuf_t *buf, int length )
{
	void    *data;

	if(( buf->cursize + length ) > buf->maxsize )
	{
		buf->cursize = 0;
		Host_Error( "%s: overflow\n", __func__ );
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

static void Cbuf_AddTextToBuffer( cmdbuf_t *buf, const char *text )
{
	int l = Q_strlen( text );

	if(( buf->cursize + l ) >= buf->maxsize )
	{
		Con_Reportf( S_WARN "%s: overflow\n", __func__ );
		return;
	}

	memcpy( Cbuf_GetSpace( buf, l ), text, l );
}

/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText( const char *text )
{
	Cbuf_AddTextToBuffer( &cmd_text, text );
}

void Cbuf_AddTextf( const char *fmt, ... )
{
	va_list va;
	char buf[MAX_VA_STRING];

	va_start( va, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	Cbuf_AddText( buf );
}

/*
============
Cbuf_AddFilteredText
============
*/
void Cbuf_AddFilteredText( const char *text )
{
	Cbuf_AddTextToBuffer( &filteredcmd_text, text );
}

/*
============
Cbuf_InsertText

Adds command text immediately after the current command
============
*/
static void Cbuf_InsertTextToBuffer( cmdbuf_t *buf, const char *text, size_t len, size_t requested_len )
{
	if(( buf->cursize + requested_len ) >= buf->maxsize )
	{
		Con_Reportf( S_WARN "%s: overflow\n", __func__ );
	}
	else
	{
		memmove( buf->data + len, buf->data, buf->cursize );
		memcpy( buf->data, text, len );
		buf->cursize += len;
	}
}

void Cbuf_InsertTextLen( const char *text, size_t len, size_t requested_len )
{
	// sometimes we need to insert more data than we have
	// but also prevent overflow
	Cbuf_InsertTextToBuffer( &cmd_text, text, len, requested_len );
}

void Cbuf_InsertText( const char *text )
{
	size_t l = Q_strlen( text );
	Cbuf_InsertTextToBuffer( &cmd_text, text, l, l );
}

/*
============
Cbuf_Execute
============
*/
static void Cbuf_ExecuteCommandsFromBuffer( cmdbuf_t *buf, qboolean isPrivileged, int cmdsToExecute )
{
	char	*text;
	char	line[MAX_CMD_LINE];
	int	i, quotes;
	char	*comment;

	while( buf->cursize )
	{
		// limit amount of commands that can be issued
		if( cmdsToExecute >= 0 )
		{
			if( !cmdsToExecute-- )
				break;
		}

		// find a \n or ; line break
		text = (char *)buf->data;

		quotes = false;
		comment = NULL;

		for( i = 0; i < buf->cursize; i++ )
		{
			if( !comment )
			{
				if( text[i] == '"' ) quotes = !quotes;

				if( quotes )
				{
					// make sure i doesn't get > cursize which causes a negative size in memmove, which is fatal --blub
					if( i < ( buf->cursize - 1 ) && ( text[i+0] == '\\' && (text[i+1] == '"' || text[i+1] == '\\')))
						i++;
				}
				else
				{
					if( text[i+0] == '/' && text[i+1] == '/' && ( i == 0 || (byte)text[i - 1] <= ' ' ))
						comment = &text[i];
					if( text[i] == ';' ) break; // don't break if inside a quoted string or comment
				}
			}

			if( text[i] == '\n' || text[i] == '\r' )
				break;
		}

		if( i >= ( MAX_CMD_LINE - 1 ))
		{
			Con_DPrintf( S_ERROR "%s: command string owerflow\n", __func__ );
			line[0] = 0;
		}
		else
		{
			memcpy( line, text, comment ? (comment - text) : i );
			line[comment ? (comment - text) : i] = 0;
		}

		// delete the text from the command buffer and move remaining commands down
		// this is necessary because commands (exec) can insert data at the
		// beginning of the text buffer
		if( i == buf->cursize )
		{
			buf->cursize = 0;
		}
		else
		{
			i++;
			buf->cursize -= i;
			memmove( buf->data, text + i, buf->cursize );
		}

		// execute the command line
		Cmd_ExecuteStringWithPrivilegeCheck( line, isPrivileged );

		if( cmd_wait )
		{
			// skip out while text still remains in buffer,
			// leaving it for next frame
			cmd_wait = false;
			break;
		}
	}
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute( void )
{
	Cbuf_ExecuteCommandsFromBuffer( &cmd_text, true, -1 );

	// a1ba: goldsrc limits unprivileged commands per frame to 1 here
	// I don't see any sense in restricting that at this moment
	// but in future we may limit this

	// a1ba: there is little to no sense limit privileged commands in
	// local game, as client runs server code anyway
	// do this for singleplayer only though, to make it easier to catch
	// possible bugs during local multiplayer testing
	Cbuf_ExecuteCommandsFromBuffer( &filteredcmd_text, SV_Active() && SV_GetMaxClients() == 1, -1 );
}

/*
===============
Cbuf_ExecStuffCmds

execute commandline
===============
*/
void Cbuf_ExecStuffCmds( void )
{
	char	build[MAX_CMD_LINE]; // this is for all commandline options combined (and is bounds checked)
	int	i, j, l = 0;

	// no reason to run the commandline arguments twice
	if( !host.stuffcmds_pending )
		return;
	build[0] = 0;

	for( i = 0; i < host.argc; i++ )
	{
		if( host.argv[i] && host.argv[i][0] == '+' && ( host.argv[i][1] < '0' || host.argv[i][1] > '9' ) && l + Q_strlen( host.argv[i] ) - 1 <= sizeof( build ) - 1 )
		{
			j = 1;

			while( host.argv[i][j] )
				build[l++] = host.argv[i][j++];

			for( i++; i < host.argc; i++ )
			{
				if( !host.argv[i] ) continue;
				if(( host.argv[i][0] == '+' || host.argv[i][0] == '-' ) && ( host.argv[i][1] < '0' || host.argv[i][1] > '9' ))
					break;
				if( l + Q_strlen( host.argv[i] ) + 4 > sizeof( build ) - 1 )
					break;
				build[l++] = ' ';

				if( Q_strchr( host.argv[i], ' ' ))
					build[l++] = '\"';

				for( j = 0; host.argv[i][j]; j++ )
					build[l++] = host.argv[i][j];

				if( Q_strchr( host.argv[i], ' ' ))
					build[l++] = '\"';
			}
			build[l++] = '\n';
			i--;
		}
	}

	// now terminate the combined string and prepend it to the command buffer
	// we already reserved space for the terminator
	build[l++] = 0;
	Cbuf_InsertText( build );
	Cbuf_Execute(); // apply now

	// this command can be called only from .rc
	Cmd_RemoveCommand( "stuffcmds" );
	host.stuffcmds_pending = false;
}

/*
==============================================================================

			SCRIPT COMMANDS

==============================================================================
*/
qboolean Cmd_CurrentCommandIsPrivileged( void )
{
	return cmd_currentCommandIsPrivileged;
}

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
hl.exe -dev 3 +map c1a0d
hl.exe -nosound -game bshift
===============
*/
static void Cmd_StuffCmds_f( void )
{
	host.stuffcmds_pending = true;
}

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "cmd use rocket ; +attack ; wait ; -attack ; cmd use blaster"
============
*/
static void Cmd_Wait_f( void )
{
	cmd_wait = true;
}

/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
static void Cmd_Echo_f( void )
{
	int	i;

	for( i = 1; i < Cmd_Argc(); i++ )
		Con_Printf( "%s ", Cmd_Argv( i ));
	Con_Printf( "\n" );
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
static void Cmd_Alias_f( void )
{
	cmdalias_t	*a;
	char		cmd[MAX_CMD_LINE];
	int		i, c;
	const char		*s;

	if( Cmd_Argc() == 1 )
	{
		Con_Printf( "Current alias commands:\n" );
		for( a = cmd_alias; a; a = a->next )
			Con_Printf( "^2%s^7 : ^3%s^7\n", a->name, a->value );
		return;
	}

	s = Cmd_Argv( 1 );

	if( Q_strlen( s ) >= MAX_ALIAS_NAME )
	{
		Con_Printf( "Alias name is too long\n" );
		return;
	}

	// if the alias already exists, reuse it
	for( a = cmd_alias; a; a = a->next )
	{
		if( !Q_strcmp( s, a->name ))
		{
			Mem_Free( a->value );
			break;
		}
	}

	if( !a )
	{
		cmdalias_t	*cur, *prev;

		a = Mem_Malloc( cmd_pool, sizeof( cmdalias_t ));

		Q_strncpy( a->name, s, sizeof( a->name ));

		// insert it at the right alphanumeric position
		for( prev = NULL, cur = cmd_alias; cur && Q_strcmp( cur->name, a->name ) < 0; prev = cur, cur = cur->next );

		if( prev ) prev->next = a;
		else cmd_alias = a;
		a->next = cur;

#if defined( XASH_HASHED_VARS )
		BaseCmd_Insert( HM_CMDALIAS, a, a->name );
#endif
	}

	// copy the rest of the command line
	cmd[0] = 0; // start out with a null string

	c = Cmd_Argc();

	for( i = 2; i < c; i++ )
	{
		if( i != 2 ) Q_strncat( cmd, " ", sizeof( cmd ));
		Q_strncat( cmd, Cmd_Argv( i ), sizeof( cmd ));
	}

	Q_strncat( cmd, "\n", sizeof( cmd ));
	a->value = copystringpool( cmd_pool, cmd );
}

/*
===============
Cmd_UnAlias_f

Remove existing aliases.
===============
*/
static void Cmd_UnAlias_f ( void )
{
	cmdalias_t	*a, *p;
	const char	*s;
	int		i;

	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "unalias alias1 [alias2 ...]\n" );
		return;
	}

	for( i = 1; i < Cmd_Argc(); i++ )
	{
		s = Cmd_Argv( i );
		p = NULL;

		for( a = cmd_alias; a; p = a, a = a->next )
		{
			if( !Q_strcmp( s, a->name ))
			{
#if defined( XASH_HASHED_VARS )
				BaseCmd_Remove( HM_CMDALIAS, a->name );
#endif
				if( a == cmd_alias )
					cmd_alias = a->next;
				if( p ) p->next = a->next;
				Mem_Free( a->value );
				Mem_Free( a );
				break;
			}
		}

		if( !a ) Con_Printf( "%s not found\n", s );
	}
}

/*
=============================================================================

			COMMAND EXECUTION

=============================================================================
*/
struct cmd_s
{
	cmd_t      *next;
	char       *name;
	xcommand_t  function;
	int         flags;
	char        desc[];
};

int           cmd_argc;
const char   *cmd_args = NULL;
char         *cmd_argv[MAX_CMD_TOKENS];
static cmd_t *cmd_functions;			// possible commands to execute
/*
===========================

Client exports

===========================
*/
/*
============
Cmd_AliasGetList
============
*/
cmdalias_t *GAME_EXPORT Cmd_AliasGetList( void )
{
	return cmd_alias;
}

/*
============
Cmd_GetList
============
*/
cmd_t *GAME_EXPORT Cmd_GetFirstFunctionHandle( void )
{
	return cmd_functions;
}

/*
============
Cmd_GetNext
============
*/
cmd_t *GAME_EXPORT Cmd_GetNextFunctionHandle( cmd_t *cmd )
{
	return (cmd) ? cmd->next : NULL;
}

/*
============
Cmd_GetName
============
*/
const char *GAME_EXPORT Cmd_GetName( cmd_t *cmd )
{
	return cmd->name;
}

/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
The text is copied to a seperate buffer and 0 characters
are inserted in the apropriate place, The argv array
will point into this temporary buffer.
============
*/
void Cmd_TokenizeString( const char *text )
{
	char	cmd_token[MAX_CMD_BUFFER];
	int	i;

	// clear the args from the last string
	for( i = 0; i < cmd_argc; i++ )
		Mem_Free( cmd_argv[i] );

	cmd_argc = 0; // clear previous args
	cmd_args = NULL;

	if( !text ) return;

	while( 1 )
	{
		// skip whitespace up to a /n
		while( *text && ((byte)*text ) <= ' ' && *text != '\r' && *text != '\n' )
			text++;

		if( *text == '\n' || *text == '\r' )
		{
			// a newline seperates commands in the buffer
			if( *text == '\r' && text[1] == '\n' )
				text++;
			text++;
			break;
		}

		if( !*text )
			return;

		if( cmd_argc == 1 )
			 cmd_args = text;

		text = COM_ParseFileSafe( (char*)text, cmd_token, sizeof( cmd_token ), PFILE_IGNOREBRACKET, NULL, NULL );

		if( !text ) return;

		if( cmd_argc < MAX_CMD_TOKENS )
		{
			cmd_argv[cmd_argc] = copystringpool( cmd_pool, cmd_token );
			cmd_argc++;
		}
	}
}

/*
============
Cmd_AddCommandEx
============
*/
int Cmd_AddCommandEx( const char *cmd_name, xcommand_t function, const char *cmd_desc, int iFlags, const char *funcname )
{
	cmd_t  *cmd, *cur, *prev;
	size_t desc_len;

	if( !COM_CheckString( cmd_name ))
	{
		Con_Reportf( S_ERROR "%s: NULL name\n", funcname );
		return 0;
	}

	// fail if the command is a variable name
	if( Cvar_FindVar( cmd_name ))
	{
		Con_DPrintf( S_ERROR "%s: %s already defined as a var\n", funcname, cmd_name );
		return 0;
	}

	// fail if the command already exists and cannot be overriden
	cmd = Cmd_Exists( cmd_name );
	if( cmd )
	{
		// some mods register commands that share the name with some engine's commands
		// when they aren't critical to keep engine running, we can let mods to override them
		// unfortunately, we lose original command this way
		if( FBitSet( cmd->flags, CMD_OVERRIDABLE ))
		{
			desc_len = Q_strlen( cmd->desc ) + 1;
			Q_strncpy( cmd->desc, cmd_desc, desc_len );
			cmd->function = function;
			cmd->flags = iFlags;

			Con_DPrintf( S_WARN "%s: %s already defined but is allowed to be overriden\n", funcname, cmd_name );
			return 1;
		}
		else
		{
			Con_DPrintf( "%s%s: %s already defined\n", cmd->function == function ? S_WARN : S_ERROR, funcname, cmd_name );
			return 0;
		}
	}

	// use a small malloc to avoid zone fragmentation
	desc_len = Q_strlen( cmd_desc ) + 1;
	cmd = Mem_Malloc( cmd_pool, sizeof( cmd_t ) + desc_len );
	cmd->name = copystringpool( cmd_pool, cmd_name );
	Q_strncpy( cmd->desc, cmd_desc, desc_len );
	cmd->function = function;
	cmd->flags = iFlags;

	// insert it at the right alphanumeric position
	for( prev = NULL, cur = cmd_functions; cur && Q_strcmp( cur->name, cmd_name ) < 0; prev = cur, cur = cur->next );

	if( prev ) prev->next = cmd;
	else cmd_functions = cmd;
	cmd->next = cur;

#if defined(XASH_HASHED_VARS)
	BaseCmd_Insert( HM_CMD, cmd, cmd->name );
#endif

	return 1;
}

/*
============
Cmd_RemoveCommand
============
*/
void GAME_EXPORT Cmd_RemoveCommand( const char *cmd_name )
{
	cmd_t	*cmd, **back;

	if( !cmd_name || !*cmd_name )
		return;

	back = &cmd_functions;
	while( 1 )
	{
		cmd = *back;
		if( !cmd ) return;

		if( !Q_strcmp( cmd_name, cmd->name ))
		{
#if defined(XASH_HASHED_VARS)
			BaseCmd_Remove( HM_CMD, cmd->name );
#endif

			*back = cmd->next;

			if( cmd->name )
				Mem_Free( cmd->name );

			Mem_Free( cmd );
			return;
		}
		back = &cmd->next;
	}
}

/*
============
Cmd_LookupCmds
============
*/
void Cmd_LookupCmds( void *buffer, void *ptr, setpair_t callback )
{
	cmd_t	*cmd;
	cmdalias_t	*alias;

	// nothing to process ?
	if( !callback ) return;

	for( cmd = cmd_functions; cmd; cmd = cmd->next )
	{
		if( !buffer ) callback( cmd->name, (char *)cmd->function, cmd->desc, ptr );
		else callback( cmd->name, (char *)cmd->function, buffer, ptr );
	}

	// lookup an aliases too
	for( alias = cmd_alias; alias; alias = alias->next )
		callback( alias->name, alias->value, buffer, ptr );
}

/*
============
Cmd_Exists
============
*/
cmd_t *Cmd_Exists( const char *cmd_name )
{
#if defined(XASH_HASHED_VARS)
	return BaseCmd_Find( HM_CMD, cmd_name );
#else
	cmd_t	*cmd;
	for( cmd = cmd_functions; cmd; cmd = cmd->next )
	{
		if( !Q_strcmp( cmd_name, cmd->name ))
			return cmd;
	}
	return NULL;
#endif
}

/*
============
Cmd_If_f

Compare and et condition bit if true
============
*/
static void Cmd_If_f( void )
{
	// reset bit first
	cmd_condition &= ~BIT( cmd_condlevel );

	// usage
	if( cmd_argc == 1 )
	{
		Con_Printf( S_USAGE "if <op1> [ <operator> <op2> ]\n");
		Con_Printf( ":<action1>\n" );
		Con_Printf( ":<action2>\n" );
		Con_Printf( "else\n" );
		Con_Printf( ":<action3>\n" );
		Con_Printf( "operands are string or float values\n" );
		Con_Printf( "and substituted cvars like '$cl_lw'\n" );
		Con_Printf( "operator is '='', '==', '>', '<', '>=', '<=' or '!='\n" );
		return;
	}

	// one argument - check if nonzero
	if( cmd_argc == 2 )
	{
		if( Q_atof( cmd_argv[1] ))
			cmd_condition |= BIT( cmd_condlevel );
	}
	else if( cmd_argc == 4 )
	{
		// simple compare
		float	f1 = Q_atof( cmd_argv[1] );
		float	f2 = Q_atof( cmd_argv[3] );

		if( !cmd_argv[2][0] ) // this is wrong
			return;

		if(( cmd_argv[2][0] == '=' ) || ( cmd_argv[2][1] == '=' )) // =, ==, >=, <=
		{
			if( !Q_strcmp( cmd_argv[1], cmd_argv[3] ) || (( f1 || f2 ) && ( f1 == f2 )))
				cmd_condition |= BIT( cmd_condlevel );
		}

		if( cmd_argv[2][0] == '!' ) 					// !=
		{
			cmd_condition ^= BIT( cmd_condlevel );
			return;
		}

		if(( cmd_argv[2][0] == '>' ) && ( f1 > f2 )) // >, >=
			cmd_condition |= BIT( cmd_condlevel );

		if(( cmd_argv[2][0] == '<' ) && ( f1 < f2 )) // <, <=
			cmd_condition |= BIT( cmd_condlevel );
	}
}

/*
============
Cmd_Else_f

Invert condition bit
============
*/
static void Cmd_Else_f( void )
{
	cmd_condition ^= BIT( cmd_condlevel );
}

static qboolean Cmd_ShouldAllowCommand( cmd_t *cmd, qboolean isPrivileged )
{
	const char *prefixes[] = { "cl_", "gl_", "r_", "m_", "hud_", "joy_", "con_", "scr_" };
	int i;

	// always allow local commands
	if( isPrivileged )
		return true;

	// never allow local only commands from remote
	if( FBitSet( cmd->flags, CMD_PRIVILEGED ))
		return false;

	// allow engine commands if user don't mind
	if( cl_filterstuffcmd.value <= 0.0f )
		return true;

	if( FBitSet( cmd->flags, CMD_FILTERABLE ))
		return false;

	for( i = 0; i < ARRAYSIZE( prefixes ); i++ )
	{
		if( !Q_strnicmp( cmd->name, prefixes[i], Q_strlen( prefixes[i] )))
			return false;
	}

	return true;
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
============
*/
static void Cmd_ExecuteStringWithPrivilegeCheck( const char *text, qboolean isPrivileged )
{
	cmd_t	*cmd = NULL;
	cmdalias_t	*a = NULL;
	convar_t *cvar = NULL;
	char		command[MAX_CMD_LINE];
	char		*pcmd = command;
	int		len = 0;

	cmd_condlevel = 0;

	// cvar value substitution
	if( cmd_scripting.value && isPrivileged )
	{
		while( *text )
		{
			// check for escape
			if(( *text == '\\' || *text == '$' ) && (*( text + 1 ) == '$' ))
			{
				text ++;
			}
			else if( *text == '$' )
			{
				char	token[MAX_CMD_LINE];
				char	*ptoken = token;

				// check for correct cvar name
				text++;
				while(( *text >= '0' && *text <= '9' ) || ( *text >= 'A' && *text <= 'Z' ) || ( *text >= 'a' && *text <= 'z' ) || ( *text == '_' ))
					*ptoken++ = *text++;
				*ptoken = 0;

				len += Q_strncpy( pcmd, Cvar_VariableString( token ), sizeof( token ) - len );
				pcmd = command + len;

				if( !*text ) break;
			}

			*pcmd++ = *text++;
			len++;
		}

		*pcmd = 0;
		text = command;

		while( *text == ':' )
		{
			if( !FBitSet( cmd_condition, BIT( cmd_condlevel )))
				return;
			cmd_condlevel++;
			text++;
		}
	}

	// execute the command line
	Cmd_TokenizeString( text );

	if( !Cmd_Argc( )) return; // no tokens

#if defined( XASH_HASHED_VARS )
	BaseCmd_FindAll( cmd_argv[0], &cmd, &a, &cvar );
#endif

	if( !host.apply_game_config )
	{
#if !defined( XASH_HASHED_VARS )
		// check aliases
		for( a = cmd_alias; a; a = a->next )
		{
			if( !Q_stricmp( cmd_argv[0], a->name ))
				break;
		}
#endif

		if( a )
		{
			size_t len = Q_strlen( a->value );
			Cbuf_InsertTextToBuffer(
				isPrivileged ? &cmd_text : &filteredcmd_text,
				a->value, len, len );
			return;
		}
	}

	// special mode for restore game.dll archived cvars
	if( !host.apply_game_config || !Q_strcmp( cmd_argv[0], "exec" ))
	{
#if !defined( XASH_HASHED_VARS )
		for( cmd = cmd_functions; cmd; cmd = cmd->next )
		{
			if( !Q_stricmp( cmd_argv[0], cmd->name ) && cmd->function )
				break;
		}
#endif

		// check functions
		if( cmd && cmd->function )
		{
			if( Cmd_ShouldAllowCommand( cmd, isPrivileged ))
			{
				cmd_currentCommandIsPrivileged = isPrivileged;
				cmd->function();
				cmd_currentCommandIsPrivileged = true;
			}
			else
			{
				Con_Printf( S_WARN "Could not execute privileged command %s\n", cmd->name );
			}

			return;
		}
	}

	// check cvars
	if( Cvar_CommandWithPrivilegeCheck( cvar, isPrivileged )) return;

	if( host.apply_game_config )
		return; // don't send nothing to server: we are a server!

	// forward the command line to the server, so the entity DLL can parse it
	if( host.type == HOST_NORMAL )
	{
#if !XASH_DEDICATED
		if( cls.state >= ca_connected )
		{
			Cmd_ForwardToServer();
		}
		else
#endif // XASH_DEDICATED
		if( Cvar_VariableInteger( "host_gameloaded" ))
		{
			Con_Printf( S_WARN "Unknown command \"%s\"\n", Cmd_Argv( 0 ) );
		}
	}
}

void Cmd_ExecuteString( const char *text )
{
	Cmd_ExecuteStringWithPrivilegeCheck( text, true );
}

/*
===================
Cmd_ForwardToServer

adds the current command line as a clc_stringcmd to the client message.
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
#if !XASH_DEDICATED
void Cmd_ForwardToServer( void )
{
	char	str[MAX_CMD_BUFFER];

	if( cls.demoplayback )
	{
		if( !Q_stricmp( Cmd_Argv( 0 ), "pause" ))
			cl.paused ^= 1;
		return;
	}

	if( cls.state < ca_connected || cls.state > ca_active )
	{
		if( Q_stricmp( Cmd_Argv( 0 ), "setinfo" ))
			Con_Printf( "Can't \"%s\", not connected\n", Cmd_Argv( 0 ));
		return; // not connected
	}

	MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );

	str[0] = 0;
	if( Q_stricmp( Cmd_Argv( 0 ), "cmd" ))
	{
		Q_strncat( str, Cmd_Argv( 0 ), sizeof( str ));
		Q_strncat( str, " ", sizeof( str ));
	}

	if( Cmd_Argc() > 1 )
		Q_strncat( str, Cmd_Args( ), sizeof( str ));
	else Q_strncat( str, "\n", sizeof( str ));

	MSG_WriteString( &cls.netchan.message, str );
}
#endif // XASH_DEDICATED

/*
============
Cmd_List_f
============
*/
static void Cmd_List_f( void )
{
	cmd_t	*cmd;
	int	i = 0;
	size_t	matchlen = 0;
	const char *match = NULL;

	if( Cmd_Argc() > 1 )
	{
		match = Cmd_Argv( 1 );
		matchlen = Q_strlen( match );
	}

	for( cmd = cmd_functions; cmd; cmd = cmd->next )
	{
		if( cmd->name[0] == '@' )
			continue;	// never show system cmds

		if( match && !Q_strnicmpext( match, cmd->name, matchlen ))
			continue;

		Con_Printf( " %-*s ^3%s^7\n", 32, cmd->name, cmd->desc );
		i++;
	}

	Con_Printf( "%i commands\n", i );
}

/*
============
Cmd_Unlink

unlink all commands with specified flag
============
*/
void Cmd_Unlink( int group )
{
	cmd_t	*cmd;
	cmd_t	**prev;
	int	count = 0;

	if( FBitSet( group, CMD_SERVERDLL ) && Cvar_VariableInteger( "host_gameloaded" ))
		return;

	if( FBitSet( group, CMD_CLIENTDLL ) && Cvar_VariableInteger( "host_clientloaded" ))
		return;

	if( FBitSet( group, CMD_GAMEUIDLL ) && Cvar_VariableInteger( "host_gameuiloaded" ))
		return;

	prev = &cmd_functions;

	while( 1 )
	{
		cmd = *prev;
		if( !cmd ) break;

		// do filter by specified group
		if( group && !FBitSet( cmd->flags, group ))
		{
			prev = &cmd->next;
			continue;
		}

#if defined(XASH_HASHED_VARS)
		BaseCmd_Remove( HM_CMD, cmd->name );
#endif

		*prev = cmd->next;

		if( cmd->name ) Mem_Free( cmd->name );

		Mem_Free( cmd );
		count++;
	}

	Con_Reportf( "unlink %i commands\n", count );
}

static void Cmd_Apropos_f( void )
{
	cmd_t *cmd;
	convar_t *var;
	cmdalias_t *alias;
	const char *partial;
	int count = 0;
	char buf[MAX_VA_STRING];

	if( Cmd_Argc() < 2 )
	{
		Msg( "apropos what?\n" );
		return;
	}

	partial = Cmd_Args();

	if( !Q_strpbrk( partial, "*?" ))
	{
		Q_snprintf( buf, sizeof( buf ), "*%s*", partial );
		partial = buf;
	}

	for( var = (convar_t*)Cvar_GetList(); var; var = var->next )
	{
		if( !matchpattern_with_separator( var->name, partial, true, "", false ) )
		{
			const char *desc;

			if( var->flags & FCVAR_EXTENDED )
				desc = var->desc;
			else desc = "game cvar";

			if( !desc )
				desc = "user cvar";

			if( !matchpattern_with_separator( desc, partial, true, "", false ))
				continue;
		}

		// TODO: maybe add flags output like cvarlist, also
		// fix inconsistencies in output from different commands
		Msg( "cvar ^3%s^7 is \"%s\" [\"%s\"] %s\n",
			var->name, var->string,
			( var->flags & FCVAR_EXTENDED ) ? var->def_string : "",
			( var->flags & FCVAR_EXTENDED ) ? var->desc : "game cvar");
		count++;
	}

	for( cmd = Cmd_GetFirstFunctionHandle(); cmd; cmd = Cmd_GetNextFunctionHandle( cmd ) )
	{
		if( cmd->name[0] == '@' )
			continue;	// never show system cmds

		if( !matchpattern_with_separator( cmd->name, partial, true, "", false ) &&
			!matchpattern_with_separator( cmd->desc, partial, true, "", false ))
			continue;

		Msg( "command ^2%s^7: %s\n", cmd->name, cmd->desc );
		count++;
	}

	for( alias = Cmd_AliasGetList(); alias; alias = alias->next )
	{
		// proceed a bit differently here as an alias value always got a final \n
		if( !matchpattern_with_separator( alias->name, partial, true, "", false ) &&
			!matchpattern_with_separator( alias->value, partial, true, "\n", false )) // when \n is a separator, wildcards don't match it //-V666
			continue;

		Msg( "alias ^5%s^7: %s", alias->name, alias->value ); // do not print an extra \n
		count++;
	}

	Msg( "\n%i result%s\n\n", count, (count > 1) ? "s" : "" );
}


/*
============
Cmd_Null_f

null function for some cmd stubs
============
*/
void Cmd_Null_f( void )
{
}

/*
=============
Cmd_MakePrivileged_f
=============
*/
static void Cmd_MakePrivileged_f( void )
{
	const char *s = Cmd_Argv( 1 );
	convar_t *cv;
	cmd_t *cmd;
	cmdalias_t *alias;

	if( Cmd_Argc( ) != 2 )
	{
		Con_Printf( S_USAGE "make_privileged <cvar or command>\n" );
		return;
	}

#if defined( XASH_HASHED_VARS )
	BaseCmd_FindAll( s, &cmd, &alias, &cv );
#else
	cmd = Cmd_Exists( s );
	cv = Cvar_FindVar( s );
#endif

	if( !cv && !cmd )
	{
		Con_Printf( "Nothing was found.\n" );
		return;
	}

	if( cv )
	{
		SetBits( cv->flags, FCVAR_PRIVILEGED );
		Con_Printf( "Cvar %s set to be privileged\n", cv->name );
	}

	if( cmd )
	{
		SetBits( cmd->flags, CMD_PRIVILEGED );
		Con_Printf( "Command %s set to be privileged\n", cmd->name );
	}
}

/*
==========
Cmd_Escape

inserts escape sequences
==========
*/
void Cmd_Escape( char *newCommand, const char *oldCommand, int len )
{
	int c;
	int scripting = cmd_scripting.value;

	while( (c = *oldCommand++) && len > 1 )
	{
		if( c == '"' )
		{
			*newCommand++ = '\\';
			len--;
		}

		if( scripting && c == '$')
		{
			*newCommand++ = '$';
			len--;
		}

		*newCommand++ = c; len--;
	}

	*newCommand++ = 0;
}


/*
============
Cmd_Init

============
*/
void Cmd_Init( void )
{
	cmd_pool = Mem_AllocPool( "Console Commands" );
	cmd_functions = NULL;
	cmd_condition = 0;
	cmd_alias = NULL;
	cmd_args = NULL;
	cmd_argc = 0;

	// register our commands
	Cmd_AddCommand( "echo", Cmd_Echo_f, "print a message to the console (useful in scripts)" );
	Cmd_AddCommand( "wait", Cmd_Wait_f, "make script execution wait for some rendered frames" );
	Cmd_AddCommand( "cmdlist", Cmd_List_f, "display all console commands beginning with the specified prefix" );
	Cmd_AddRestrictedCommand( "stuffcmds", Cmd_StuffCmds_f, "execute commandline parameters (must be present in .rc script)" );
	Cmd_AddCommand( "apropos", Cmd_Apropos_f, "lists all console variables/commands/aliases containing the specified string in the name or description" );
#if !XASH_DEDICATED
	Cmd_AddCommand( "cmd", Cmd_ForwardToServer, "send a console commandline to the server" );
#endif // XASH_DEDICATED
	Cmd_AddRestrictedCommand( "alias", Cmd_Alias_f, "create a script function. Without arguments show the list of all alias" );
	Cmd_AddRestrictedCommand( "unalias", Cmd_UnAlias_f, "remove a script function" );
	Cmd_AddRestrictedCommand( "if", Cmd_If_f, "compare and set condition bits" );
	Cmd_AddRestrictedCommand( "else", Cmd_Else_f, "invert condition bit" );

	Cmd_AddRestrictedCommand( "make_privileged", Cmd_MakePrivileged_f, "makes command or variable privileged (protected from access attempts from server)" );

#if defined(XASH_HASHED_VARS)
	Cmd_AddCommand( "basecmd_stats", BaseCmd_Stats_f, "print info about basecmd usage" );
	Cmd_AddCommand( "basecmd_test", BaseCmd_Test_f, "test basecmd" );
#endif
}

void Cmd_Shutdown( void )
{
	Mem_FreePool( &cmd_pool );
}

#if XASH_ENGINE_TESTS
#include "tests.h"

enum
{
	NO_CALL = 0,
	PRIV = 1,
	UNPRIV = 2
};

static int test_flags[3] = { NO_CALL, NO_CALL, NO_CALL };

static void Test_PrivilegedCommand_f( void )
{
	test_flags[0] = Cmd_CurrentCommandIsPrivileged() ? PRIV : UNPRIV;
}

static void Test_UnprivilegedCommand_f( void )
{
	test_flags[1] = Cmd_CurrentCommandIsPrivileged() ? PRIV : UNPRIV;
}

static void Test_FilteredCommand_f( void )
{
	test_flags[2] = Cmd_CurrentCommandIsPrivileged() ? PRIV : UNPRIV;
}

void Test_RunCmd( void )
{
	Cmd_AddCommand( "test_privileged", Test_PrivilegedCommand_f, "bark bark" );
	Cmd_AddRestrictedCommand( "test_unprivileged", Test_UnprivilegedCommand_f, "meow meow" );
	Cmd_AddCommand( "hud_filtered", Test_FilteredCommand_f, "dummy description" );

	Cbuf_AddText( "test_privileged; test_unprivileged; hud_filtered\n" );
	Cbuf_Execute();
	TASSERT( test_flags[0] == PRIV );
	TASSERT( test_flags[1] == PRIV );
	TASSERT( test_flags[2] == PRIV );

	VectorSet( test_flags, NO_CALL, NO_CALL, NO_CALL );
	Cvar_DirectSet( &cl_filterstuffcmd, "0" );
	Cbuf_AddFilteredText( "test_privileged; test_unprivileged; hud_filtered\n" );
	Cbuf_Execute();
	TASSERT( test_flags[0] == UNPRIV );
	TASSERT( test_flags[1] == NO_CALL );
	TASSERT( test_flags[2] == UNPRIV );

	VectorSet( test_flags, NO_CALL, NO_CALL, NO_CALL );
	Cvar_DirectSet( &cl_filterstuffcmd, "1" );
	Cbuf_AddFilteredText( "test_privileged; test_unprivileged; hud_filtered\n" );
	Cbuf_Execute();
	TASSERT( test_flags[0] == UNPRIV );
	TASSERT( test_flags[1] == NO_CALL );
	TASSERT( test_flags[2] == NO_CALL );

	Cmd_RemoveCommand( "hud_filtered" );
	Cmd_RemoveCommand( "test_unprivileged" );
	Cmd_RemoveCommand( "test_privileged" );
}
#endif
