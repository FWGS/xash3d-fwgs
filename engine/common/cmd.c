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
#define MAX_FILTERED_CMDS 256

typedef struct
{
	byte data[MAX_CMD_BUFFER];
	int  cursize;
} cmdbuf_t;

static char *filtered_commands[MAX_FILTERED_CMDS];
static int num_filtered_commands = 0;
static qboolean cmd_filter_initialized = false;
static int cmd_wait;
static cmdbuf_t cmd_text;
static cmdbuf_t filteredcmd_text;
static cmdalias_t *cmd_alias;
static uint cmd_condition;
static int  cmd_condlevel;
static qboolean cmd_currentCommandIsPrivileged;
static poolhandle_t cmd_pool;
static void Cmd_LoadFilterConfig( void );
static qboolean Cmd_IsFiltered( const char *cmd );
static void Cmd_FilterCommands( cmdbuf_t *buf );
static void Cmd_ReloadFilter_f( void );
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
	memset( &cmd_text, 0, sizeof( cmd_text ));
	memset( &filteredcmd_text, 0, sizeof( filteredcmd_text ));
	cmd_wait = 0;
}

/*
============
Cbuf_GetSpace
============
*/
static void *Cbuf_GetSpace( cmdbuf_t *buf, int length )
{
	void *data;

	if(( buf->cursize + length ) >= sizeof( buf->data ))
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

	if(( buf->cursize + l ) >= sizeof( buf->data ))
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
	if(( buf->cursize + requested_len ) >= sizeof( buf->data ))
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

static void Cbuf_InsertTextLen( const char *text, size_t len, size_t requested_len )
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

	if( !isPrivileged && buf == &filteredcmd_text )
	{
		Cmd_FilterCommands( buf );
	}

	while( buf->cursize )
	{
		if( cmd_wait > 0 )
		{
			cmd_wait--;
			break;
		}
		
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
	int frame = 1;
	if( Cmd_Argc() > 1 )
	{
		frame = Q_atoi( Cmd_Argv( 1 ) );
		frame = Q_max( 1, frame );
	}
	cmd_wait = frame;
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

		BaseCmd_Insert( HM_CMDALIAS, a, a->name );
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
				BaseCmd_Remove( HM_CMDALIAS, a->name );
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
============
Cmd_LoadFilterConfig

Load commands to filter from cmdfilter.ini
============
*/
static void Cmd_LoadFilterConfig( void )
{
	char filename[256];
	file_t *f;
	char *data, *p;
	char line[MAX_CMD_LINE];
	int len;
	char *line_start;
	int line_len;
	
	if( cmd_filter_initialized )
		return;
	
	num_filtered_commands = 0;

	Q_snprintf( filename, sizeof( filename ), "cmdfilter.ini" );
	f = FS_Open( filename, "r", false );
	
	if( !f )
	{
		static qboolean file_not_found_printed = false;
		if( !file_not_found_printed )
		{
			Con_DPrintf( "Cmd_LoadFilterConfig: cmdfilter.ini not found\n" );
			file_not_found_printed = true;
		}
		cmd_filter_initialized = true;
		return;
	}
	
	len = FS_FileLength( f );
	data = Mem_Malloc( cmd_pool, len + 1 );
	FS_Read( f, data, len );
	data[len] = 0;
	FS_Close( f );
	
	p = data;
	while( p && *p )
	{
		while( *p && (byte)*p <= ' ' )
			p++;
			
		if( !*p ) break;

		if( *p == '/' && *(p+1) == '/' )
		{
			while( *p && *p != '\n' )
				p++;
			continue;
		}
		
		if( *p == ';' )
		{
			while( *p && *p != '\n' )
				p++;
			continue;
		}

		line_start = p;
		while( *p && *p != '\n' && *p != '\r' )
			p++;
			
		line_len = p - line_start;
		if( line_len >= sizeof( line ) )
			line_len = sizeof( line ) - 1;
			
		memcpy( line, line_start, line_len );
		line[line_len] = 0;
	
		while( line_len > 0 && (byte)line[line_len-1] <= ' ' )
			line[--line_len] = 0;
		
		if( line_len > 0 && num_filtered_commands < MAX_FILTERED_CMDS )
		{
			filtered_commands[num_filtered_commands] = copystringpool( cmd_pool, line );
			num_filtered_commands++;
			Con_DPrintf( "Cmd_LoadFilterConfig: added filter for '%s'\n", line );
		}
		else if( num_filtered_commands >= MAX_FILTERED_CMDS )
		{
			Con_Printf( S_ERROR "Cmd_LoadFilterConfig: too many filtered commands, max is %d\n", MAX_FILTERED_CMDS );
			break;
		}
		while( *p && ( *p == '\n' || *p == '\r' ) )
			p++;
	}
	
	Mem_Free( data );
	cmd_filter_initialized = true;
	
	if( num_filtered_commands > 0 )
	{
		Con_Printf( "Cmd_LoadFilterConfig: loaded %d filtered commands\n", num_filtered_commands );
	}
}

/*
============
Cmd_IsFiltered

Check if a command should be filtered
============
*/
static qboolean Cmd_IsFiltered( const char *cmd )
{
	int i;
	
	if( !cmd_filter_initialized )
		Cmd_LoadFilterConfig();

	if( num_filtered_commands == 0 )
		return false;
	
	for( i = 0; i < num_filtered_commands; i++ )
	{
		if( !Q_stricmp( cmd, filtered_commands[i] ) )
			return true;
	}
	
	return false;
}

/*
============
Cmd_FilterCommands

Filter commands from text buffer
============
*/
static void Cmd_FilterCommands( cmdbuf_t *buf )
{
	char *text, *filtered_text;
	char line[MAX_CMD_LINE];
	int i, j, quotes, new_size;
	qboolean skip_line, comment;
	char *comment_pos;
	char *cmd_start;
	char command[64];
	int k;
	int line_end;
	int copy_len;

	if( !cmd_filter_initialized )
		Cmd_LoadFilterConfig();
	if( num_filtered_commands == 0 || buf->cursize == 0 )
		return;

	filtered_text = Mem_Malloc( cmd_pool, sizeof( buf->data ) );
	new_size = 0;
	text = (char *)buf->data;

	for( i = 0; i < buf->cursize; )
	{
		quotes = false;
		comment = false;
		comment_pos = NULL;
		skip_line = false;

		for( j = 0; i + j < buf->cursize; j++ )
		{
			if( !comment )
			{
				if( text[i + j] == '"' )
					quotes = !quotes;

				if( quotes )
				{
					if( i + j < buf->cursize - 1 && 
						text[i + j] == '\\' && 
						(text[i + j + 1] == '"' || text[i + j + 1] == '\\') )
						j++;
				}
				else
				{
					if( i + j < buf->cursize - 1 &&
						text[i + j] == '/' && text[i + j + 1] == '/' &&
						( j == 0 || (byte)text[i + j - 1] <= ' ' ) )
					{
						comment = true;
						comment_pos = &text[i + j];
					}
					if( text[i + j] == ';' )
						break;
				}
			}
			if( text[i + j] == '\n' || text[i + j] == '\r' )
				break;
		}
		
		line_end = (i + j < buf->cursize) ? j : buf->cursize - i;

		if( line_end >= sizeof(line) )
			line_end = sizeof(line) - 1;
			
		memcpy( line, &text[i], comment_pos ? (comment_pos - &text[i]) : line_end );
		line[comment_pos ? (comment_pos - &text[i]) : line_end] = 0;

		if( !comment_pos )
		{
			cmd_start = line;

			while( *cmd_start && (byte)*cmd_start <= ' ' )
				cmd_start++;

			if( *cmd_start == '+' || *cmd_start == '-' || 
				(*cmd_start >= 'a' && *cmd_start <= 'z') ||
				(*cmd_start >= 'A' && *cmd_start <= 'Z') ||
				*cmd_start == '_' )
			{
				k = 0;
				while( *cmd_start && 
					   (*cmd_start == '+' || *cmd_start == '-' ||
					   (*cmd_start >= 'a' && *cmd_start <= 'z') ||
					   (*cmd_start >= 'A' && *cmd_start <= 'Z') ||
					   (*cmd_start >= '0' && *cmd_start <= '9') ||
					   *cmd_start == '_') &&
					   k < sizeof(command) - 1 )
				{
					command[k++] = *cmd_start++;
				}
				command[k] = 0;
				
				if( command[0] && Cmd_IsFiltered( command ) )
				{
					skip_line = true;
					Con_DPrintf( "Cmd_FilterCommands: filtered '%s'\n", command );
				}
			}
		}
		
		if( !skip_line )
		{
			copy_len = (i + j < buf->cursize) ? j + 1 : buf->cursize - i;
			memcpy( &filtered_text[new_size], &text[i], copy_len );
			new_size += copy_len;
		}
		
		if( i + j < buf->cursize )
			i += j + 1;
		else
			break;
	}

	memcpy( buf->data, filtered_text, new_size );
	buf->cursize = new_size;

	Mem_Free( filtered_text );
}

/*
============
Cmd_ReloadFilter_f

Reload command filter configuration
============
*/
static void Cmd_ReloadFilter_f( void )
{
	int i;
	for( i = 0; i < num_filtered_commands; i++ )
	{
		if( filtered_commands[i] )
			Mem_Free( filtered_commands[i] );
	}
	num_filtered_commands = 0;

	Cmd_LoadFilterConfig();
	Con_Printf( "Command filter configuration reloaded\n" );
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
	convar_t *cvar;
	cmdalias_t *alias;

	if( COM_StringEmptyOrNULL( cmd_name ))
	{
		Con_Reportf( S_ERROR "%s: NULL name\n", funcname );
		return 0;
	}

	// fail if the command is a variable name
	BaseCmd_FindAll( cmd_name, &cmd, &alias, &cvar );
	if( cvar )
	{
		Con_DPrintf( S_ERROR "%s: %s already defined as a var\n", funcname, cmd_name );
		return 0;
	}

	// fail if the command already exists and cannot be overriden
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

	BaseCmd_Insert( HM_CMD, cmd, cmd->name );

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
			BaseCmd_Remove( HM_CMD, cmd->name );

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
	return BaseCmd_Find( HM_CMD, cmd_name );
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

	BaseCmd_FindAll( cmd_argv[0], &cmd, &a, &cvar );

	// check aliases
	if( a )
	{
		size_t len = Q_strlen( a->value );
		Cbuf_InsertTextToBuffer( isPrivileged ? &cmd_text : &filteredcmd_text, a->value, len, len );
		return;
	}

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

	// check cvars
	if( Cvar_CommandWithPrivilegeCheck( cvar, isPrivileged ))
		return;

	// forward the command line to the server, so the entity DLL can parse it
#if !XASH_DEDICATED
	if( cls.state >= ca_connected )
	{
		Cmd_ForwardToServer();
	}
	else
#endif // XASH_DEDICATED
	{
		Con_Printf( S_WARN "Unknown command \"%s\"\n", Cmd_Argv( 0 ) );
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
things like nodmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
#if !XASH_DEDICATED
void Cmd_ForwardToServer( void )
{
	char str[MAX_CMD_BUFFER];

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

		BaseCmd_Remove( HM_CMD, cmd->name );

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

	BaseCmd_FindAll( s, &cmd, &alias, &cv );

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
===============
Cmd_ExecScript
===============
*/
static void Cmd_ExecScript( const char *filename, qboolean privileged )
{
	byte *f;
	fs_offset_t len;

	f = FS_LoadFile( filename, &len, false );
	if( !f )
	{
		Con_Reportf( "couldn't exec %s\n", filename );
		return;
	}

	// len is fs_offset_t, which can be larger than size_t
	if( len >= SIZE_MAX )
	{
		Con_Reportf( "%s: %s is too long\n", __func__, filename );
		Mem_Free( f );
		return;
	}

	if( f[len - 1] != '\n' )
	{
		Cbuf_InsertTextLen( f, len, len + 1 );
		Cbuf_InsertTextLen( "\n", 1, 1 );
	}
	else Cbuf_InsertTextLen( f, len, len );

	Mem_Free( f );
}

/*
===============
Cmd_Exec_f
===============
*/
static void Cmd_Exec_f( void )
{
	string cfgpath;
	search_t *search = NULL;
	int i;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "exec <PATTERN>\n"
		"PATTERN single file or wildcard pattern to match\n"
		"Wildcards: * matches any characters, ? matches single character\n"
		"Example: file.cfg    - single file.cfg\n"
		"\tdirectory/*        - all .cfg files in directory\n"
		"\tdirectory/???.cfg  - all .cfg files in directory with 3 character names\n" );
		return;
	}

	Q_strncpy( cfgpath, Cmd_Argv( 1 ), sizeof( cfgpath ));
	COM_DefaultExtension( cfgpath, ".cfg", sizeof( cfgpath ));

	if( Q_strpbrk( cfgpath, "*?" ))
	{
		search = FS_Search( cfgpath, true, false );
		if( !search || !search->numfilenames )
		{
			Con_Printf( "couldn't exec %s\n", Cmd_Argv( 1 ));
			if( search ) Mem_Free( search );
			return;
		}

		Con_Printf( "execing %d file(s) - " S_GREEN "%s" S_DEFAULT "\n",
			search->numfilenames, Cmd_Argv( 1 ));

		for( i = 0; i < search->numfilenames; i++ )
		{
			Cmd_ExecScript( search->filenames[i], false );
		}

		Mem_Free( search );
		return;
	}

#ifndef XASH_DEDICATED
	if( !Cmd_CurrentCommandIsPrivileged() && !Q_stricmp( GI->gamefolder, "tfc" ))
	{
		const char *const unprivileged_whitelist[] =
		{
			"civilian.cfg", "demoman.cfg", "engineer.cfg",
			"hwguy.cfg", "mapdefault.cfg", "medic.cfg", "pyro.cfg",
			"scout.cfg", "sniper.cfg", "soldier.cfg", "spy.cfg",
		};
		char mapcfg[MAX_VA_STRING];
		qboolean allow = false;

		Q_snprintf( mapcfg, sizeof( mapcfg ), "%s.cfg", clgame.mapname );

		if( !Q_stricmp( mapcfg, cfgpath ))
		{
			allow = true;
		}
		else for( i = 0; i < ARRAYSIZE( unprivileged_whitelist ); i++ )
		{
			if( !Q_strcmp( cfgpath, unprivileged_whitelist[i] ))
			{
				allow = true;
				break;
			}
		}

		if( !allow )
		{
			Con_Printf( "exec %s: not privileged or in whitelist\n", cfgpath );
			return;
		}
	}
#endif // XASH_DEDICATED

	// don't execute game.cfg in singleplayer
	if( SV_GetMaxClients() == 1 && !Q_stricmp( "game.cfg", cfgpath ))
		return;

	if( !Q_stricmp( "config.cfg", cfgpath ))
		host.config_executed = true;

	Con_Printf( "execing " S_GREEN "%s" S_DEFAULT "\n", Cmd_Argv( 1 ));

	Cmd_ExecScript( cfgpath, true );
}

/*
=================
Cmd_Userconfigd_f
=================
*/
static void Cmd_Userconfigd_f( void )
{
	search_t *t;
	int i;

	t = FS_Search( "userconfig.d/*.cfg", true, false );
	if( !t )
		return;

	for( i = 0; i < t->numfilenames; i++ )
	{
		Cbuf_AddTextf( "exec %s\n", t->filenames[i] );
	}

	Mem_Free( t );
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
	cmd_wait = 0;
	cmd_alias = NULL;
	cmd_args = NULL;
	cmd_argc = 0;

	// register our commands
	Cmd_AddCommand( "echo", Cmd_Echo_f, "print a message to the console (useful in scripts)" );
	Cmd_AddRestrictedCommand( "wait", Cmd_Wait_f, "make script execution wait for some rendered frames" );
	Cmd_AddRestrictedCommand( "cmdlist", Cmd_List_f, "display all console commands beginning with the specified prefix" );
	Cmd_AddRestrictedCommand( "stuffcmds", Cmd_StuffCmds_f, "execute commandline parameters (must be present in .rc script)" );
	Cmd_AddRestrictedCommand( "apropos", Cmd_Apropos_f, "lists all console variables/commands/aliases containing the specified string in the name or description" );
#if !XASH_DEDICATED
	Cmd_AddCommand( "cmd", Cmd_ForwardToServer, "send a console commandline to the server" );
#endif // XASH_DEDICATED
	Cmd_AddRestrictedCommand( "alias", Cmd_Alias_f, "create a script function. Without arguments show the list of all alias" );
	Cmd_AddRestrictedCommand( "unalias", Cmd_UnAlias_f, "remove a script function" );
	Cmd_AddRestrictedCommand( "if", Cmd_If_f, "compare and set condition bits" );
	Cmd_AddRestrictedCommand( "else", Cmd_Else_f, "invert condition bit" );
	Cmd_AddCommand( "reload_filter", Cmd_ReloadFilter_f, "reload cmdfilter config from cmdfilter.ini" );
	Cmd_AddRestrictedCommand( "make_privileged", Cmd_MakePrivileged_f, "makes command or variable privileged (protected from access attempts from server)" );

	Cmd_AddRestrictedCommand( "basecmd_stats", BaseCmd_Stats_f, "print info about basecmd usage" );
	Cmd_AddRestrictedCommand( "basecmd_test", BaseCmd_Test_f, "test basecmd" );
	Cmd_AddCommand( "exec", Cmd_Exec_f, "execute a script file" );
	Cmd_AddRestrictedCommand( "userconfigd", Cmd_Userconfigd_f, "execute all scripts from userconfig.d" );
}

void Cmd_Shutdown( void )
{
	int i;
	for( i = 0; i < num_filtered_commands; i++ )
	{
		if( filtered_commands[i] )
			Mem_Free( filtered_commands[i] );
	}
	num_filtered_commands = 0;

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
