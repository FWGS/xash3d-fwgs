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
	byte		*data;
	int		cursize;
	int		maxsize;
} cmdbuf_t;

qboolean			cmd_wait;
cmdbuf_t			cmd_text;
byte			cmd_text_buf[MAX_CMD_BUFFER];
cmdalias_t		*cmd_alias;
uint			cmd_condition;
int			cmd_condlevel;

/*
=============================================================================

			COMMAND BUFFER

=============================================================================
*/

/*
============
Cbuf_Init
============
*/
void Cbuf_Init( void )
{
	cmd_text.data = cmd_text_buf;
	cmd_text.maxsize = MAX_CMD_BUFFER;
	cmd_text.cursize = 0;
}

/*
============
Cbuf_Clear
============
*/
void Cbuf_Clear( void )
{
	memset( cmd_text.data, 0, sizeof( cmd_text_buf ));
	cmd_text.cursize = 0;
}

/*
============
Cbuf_GetSpace
============
*/
void *Cbuf_GetSpace( cmdbuf_t *buf, int length )
{
	void    *data;
	
	if(( buf->cursize + length ) > buf->maxsize )
	{
		buf->cursize = 0;
		Host_Error( "Cbuf_GetSpace: overflow\n" );
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;
	
	return data;
}

/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText( const char *text )
{
	int	l = Q_strlen( text );

	if(( cmd_text.cursize + l ) >= cmd_text.maxsize )
	{
		Con_Reportf( S_WARN "Cbuf_AddText: overflow\n" );
	}
	else
	{
		memcpy( Cbuf_GetSpace( &cmd_text, l ), text, l );
	}
}

/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
============
*/
void Cbuf_InsertText( const char *text )
{
	int	l = Q_strlen( text );

	if(( cmd_text.cursize + l ) >= cmd_text.maxsize )
	{
		Con_Reportf( S_WARN "Cbuf_InsertText: overflow\n" );
	}
	else
	{
		memmove( cmd_text.data + l, cmd_text.data, cmd_text.cursize );
		memcpy( cmd_text.data, text, l );
		cmd_text.cursize += l;
	}
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute( void )
{
	char	*text;
	char	line[MAX_CMD_LINE];
	int	i, quotes;
	char	*comment;

	while( cmd_text.cursize )
	{
		// find a \n or ; line break
		text = (char *)cmd_text.data;

		quotes = false;
		comment = NULL;

		for( i = 0; i < cmd_text.cursize; i++ )
		{
			if( !comment )
			{
				if( text[i] == '"' ) quotes = !quotes;

				if( quotes )
				{
					// make sure i doesn't get > cursize which causes a negative size in memmove, which is fatal --blub
					if( i < ( cmd_text.cursize - 1 ) && ( text[i+0] == '\\' && (text[i+1] == '"' || text[i+1] == '\\')))
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
			Con_DPrintf( S_ERROR "Cbuf_Execute: command string owerflow\n" );
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
		if( i == cmd_text.cursize )
		{
			cmd_text.cursize = 0;
		}
		else
		{
			i++;
			cmd_text.cursize -= i;
			memmove( cmd_text.data, text + i, cmd_text.cursize );
		}

		// execute the command line
		Cmd_ExecuteString( line );

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
/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
hl.exe -dev 3 +map c1a0d
hl.exe -nosound -game bshift
===============
*/
void Cmd_StuffCmds_f( void )
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
void Cmd_Wait_f( void )
{
	cmd_wait = true;
}

/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f( void )
{
	int	i;
	
	for( i = 1; i < Cmd_Argc(); i++ )
		Con_Printf( "%s", Cmd_Argv( i ));
	Con_Printf( "\n" );
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
void Cmd_Alias_f( void )
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
			Z_Free( a->value );
			break;
		}
	}

	if( !a )
	{
		cmdalias_t	*cur, *prev;

		a = Z_Malloc( sizeof( cmdalias_t ));

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
	a->value = copystring( cmd );
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
typedef struct cmd_s
{
	struct cmd_s	*next;
	char		*name;
	xcommand_t	function;
	char		*desc;
	int		flags;
} cmd_t;

static int		cmd_argc;
static char		*cmd_args = NULL;
static char		*cmd_argv[MAX_CMD_TOKENS];
static char		cmd_tokenized[MAX_CMD_BUFFER];	// will have 0 bytes inserted
static cmd_t		*cmd_functions;			// possible commands to execute

/*
============
Cmd_Argc
============
*/
int Cmd_Argc( void )
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
const char *Cmd_Argv( int arg )
{
	if((uint)arg >= cmd_argc )
		return "";
	return cmd_argv[arg];	
}

/*
============
Cmd_Args
============
*/
const char *Cmd_Args( void )
{
	return cmd_args;
}


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
char *GAME_EXPORT Cmd_GetName( cmd_t *cmd )
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
void Cmd_TokenizeString( char *text )
{
	char	cmd_token[MAX_CMD_BUFFER];
	int	i;

	// clear the args from the last string
	for( i = 0; i < cmd_argc; i++ )
		Z_Free( cmd_argv[i] );

	cmd_argc = 0; // clear previous args
	cmd_args = NULL;

	if( !text ) return;

	while( 1 )
	{
		// skip whitespace up to a /n
		while( *text && ((byte)*text) <= ' ' && *text != '\r' && *text != '\n' )
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

		host.com_ignorebracket = true;
		text = COM_ParseFile( text, cmd_token );
		host.com_ignorebracket = false;

		if( !text ) return;

		if( cmd_argc < MAX_CMD_TOKENS )
		{
			cmd_argv[cmd_argc] = copystring( cmd_token );
			cmd_argc++;
		}
	}
}

/*
============
Cmd_AddCommandEx
============
*/
static int Cmd_AddCommandEx( const char *funcname, const char *cmd_name, xcommand_t function,
	const char *cmd_desc, int iFlags )
{
	cmd_t	*cmd, *cur, *prev;

	if( !COM_CheckString( cmd_name ))
	{
		Con_Reportf( S_ERROR  "Cmd_AddCommand: NULL name\n" );
		return 0;
	}

	// fail if the command is a variable name
	if( Cvar_FindVar( cmd_name ))
	{
		Con_DPrintf( S_ERROR "Cmd_AddServerCommand: %s already defined as a var\n", cmd_name );
		return 0;
	}

	// fail if the command already exists
	if( Cmd_Exists( cmd_name ))
	{
		Con_DPrintf( S_ERROR "Cmd_AddServerCommand: %s already defined\n", cmd_name );
		return 0;
	}

	// use a small malloc to avoid zone fragmentation
	cmd = Z_Malloc( sizeof( cmd_t ) );
	cmd->name = copystring( cmd_name );
	cmd->desc = copystring( cmd_desc );
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
Cmd_AddCommand
============
*/
void Cmd_AddCommand( const char *cmd_name, xcommand_t function, const char *cmd_desc )
{
	Cmd_AddCommandEx( __FUNCTION__, cmd_name, function, cmd_desc, 0 );
}


/*
============
Cmd_AddRestrictedCommand
============
*/
void Cmd_AddRestrictedCommand( const char *cmd_name, xcommand_t function, const char *cmd_desc )
{
	Cmd_AddCommandEx( __FUNCTION__, cmd_name, function, cmd_desc, CMD_LOCALONLY );
}

/*
============
Cmd_AddServerCommand
============
*/
void Cmd_AddServerCommand( const char *cmd_name, xcommand_t function )
{
	Cmd_AddCommandEx( __FUNCTION__, cmd_name, function, "server command", CMD_SERVERDLL );
}

/*
============
Cmd_AddClientCommand
============
*/
int Cmd_AddClientCommand( const char *cmd_name, xcommand_t function )
{
	return Cmd_AddCommandEx( __FUNCTION__, cmd_name, function, "client command", CMD_CLIENTDLL );
}

/*
============
Cmd_AddGameUICommand
============
*/
int Cmd_AddGameUICommand( const char *cmd_name, xcommand_t function )
{
	return Cmd_AddCommandEx( __FUNCTION__, cmd_name, function, "gameui command", CMD_GAMEUIDLL );
}

/*
============
Cmd_AddRefCommand
============
*/
int Cmd_AddRefCommand( const char *cmd_name, xcommand_t function, const char *description )
{
	return Cmd_AddCommandEx( __FUNCTION__, cmd_name, function, description, CMD_REFDLL );
}

/*
============
Cmd_RemoveCommand
============
*/
void Cmd_RemoveCommand( const char *cmd_name )
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

			if( cmd->desc )
				Mem_Free( cmd->desc );

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
qboolean Cmd_Exists( const char *cmd_name )
{
#if defined(XASH_HASHED_VARS)
	return BaseCmd_Find( HM_CMD, cmd_name ) != NULL;
#else
	cmd_t	*cmd;

	for( cmd = cmd_functions; cmd; cmd = cmd->next )
	{
		if( !Q_strcmp( cmd_name, cmd->name ))
			return true;
	}
	return false;
#endif
}

/*
============
Cmd_If_f

Compare and et condition bit if true
============
*/
void Cmd_If_f( void )
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
void Cmd_Else_f( void )
{
	cmd_condition ^= BIT( cmd_condlevel );
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
============
*/
void Cmd_ExecuteString( char *text )
{	
	cmd_t	*cmd = NULL;
	cmdalias_t	*a = NULL;
	convar_t *cvar = NULL;
	char		command[MAX_CMD_LINE];
	char		*pcmd = command;
	int		len = 0;

	cmd_condlevel = 0;

	// cvar value substitution
	if( cmd_scripting && cmd_scripting->value )
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

				len += Q_strncpy( pcmd, Cvar_VariableString( token ), MAX_CMD_LINE - len );
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

#if defined(XASH_HASHED_VARS)
	BaseCmd_FindAll( cmd_argv[0],
		(base_command_t**)&cmd,
		(base_command_t**)&a,
		(base_command_t**)&cvar );
#endif

	if( !host.apply_game_config )
	{
		// check aliases
		if( a ) // already found in basecmd
		{
			Cbuf_InsertText( a->value );
			return;
		}

		for( a = cmd_alias; a; a = a->next )
		{
			if( !Q_stricmp( cmd_argv[0], a->name ))
			{
				Cbuf_InsertText( a->value );
				return;
			}
		}
	}

	// special mode for restore game.dll archived cvars
	if( !host.apply_game_config || !Q_strcmp( cmd_argv[0], "exec" ))
	{
		// check functions
		if( cmd && cmd->function ) // already found in basecmd
		{
			cmd->function();
			return;
		}

		for( cmd = cmd_functions; cmd; cmd = cmd->next )
		{
			if( !Q_stricmp( cmd_argv[0], cmd->name ) && cmd->function )
			{
				cmd->function();
				return;
			}
		}
	}

	// check cvars
	if( Cvar_Command( cvar )) return;

	if( host.apply_game_config )
		return; // don't send nothing to server: we is a server!

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
			Con_Printf( S_WARN "Unknown command \"%s\"\n", text );
		}
	}
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
		Q_strcat( str, Cmd_Argv( 0 ));
		Q_strcat( str, " " );
	}
	
	if( Cmd_Argc() > 1 )
		Q_strcat( str, Cmd_Args( ));
	else Q_strcat( str, "\n" );

	MSG_WriteString( &cls.netchan.message, str );
}
#endif // XASH_DEDICATED

/*
============
Cmd_List_f
============
*/
void Cmd_List_f( void )
{
	cmd_t	*cmd;
	int	i = 0;
	const char	*match;

	if( Cmd_Argc() > 1 ) match = Cmd_Argv( 1 );
	else match = NULL;

	for( cmd = cmd_functions; cmd; cmd = cmd->next )
	{
		if( cmd->name[0] == '@' )
			continue;	// never show system cmds

		if( match && !Q_stricmpext( match, cmd->name ))
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

	if( Cvar_VariableInteger( "host_gameloaded" ) && FBitSet( group, CMD_SERVERDLL ))
		return;

	if( Cvar_VariableInteger( "host_clientloaded" ) && FBitSet( group, CMD_CLIENTDLL ))
		return;

	if( Cvar_VariableInteger( "host_gameuiloaded" ) && FBitSet( group, CMD_GAMEUIDLL ))
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
		if( cmd->desc ) Mem_Free( cmd->desc );

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
	qboolean ispattern;

	if( Cmd_Argc() > 1 )
	{
		partial = Cmd_Args();
	}
	else
	{
		Msg( "apropos what?\n" );
		return;
	}

	ispattern = partial && ( Q_strchr( partial, '*' ) || Q_strchr( partial, '?' ));
	if( !ispattern )
		partial = va( "*%s*", partial );

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
============
Cmd_Init
============
*/
void Cmd_Init( void )
{
	Cbuf_Init();

	cmd_functions = NULL;
	cmd_condition = 0;
	cmd_alias = NULL;
	cmd_args = NULL;
	cmd_argc = 0;

	// register our commands
	Cmd_AddCommand( "echo", Cmd_Echo_f, "print a message to the console (useful in scripts)" );
	Cmd_AddCommand( "wait", Cmd_Wait_f, "make script execution wait for some rendered frames" );
	Cmd_AddCommand( "cmdlist", Cmd_List_f, "display all console commands beginning with the specified prefix" );
	Cmd_AddCommand( "stuffcmds", Cmd_StuffCmds_f, "execute commandline parameters (must be present in .rc script)" );
	Cmd_AddCommand( "apropos", Cmd_Apropos_f, "lists all console variables/commands/aliases containing the specified string in the name or description" );
#if !XASH_DEDICATED
	Cmd_AddCommand( "cmd", Cmd_ForwardToServer, "send a console commandline to the server" );
#endif // XASH_DEDICATED
	Cmd_AddCommand( "alias", Cmd_Alias_f, "create a script function. Without arguments show the list of all alias" );
	Cmd_AddCommand( "unalias", Cmd_UnAlias_f, "remove a script function" );
	Cmd_AddCommand( "if", Cmd_If_f, "compare and set condition bits" );
	Cmd_AddCommand( "else", Cmd_Else_f, "invert condition bit" );

#if defined(XASH_HASHED_VARS)
	Cmd_AddCommand( "basecmd_stats", BaseCmd_Stats_f, "print info about basecmd usage" );
	Cmd_AddCommand( "basecmd_test", BaseCmd_Test_f, "test basecmd" );
#endif
}
