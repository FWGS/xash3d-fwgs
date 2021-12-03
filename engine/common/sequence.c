/*
sequence.c - scripted sequences for CS:CZDS
Copyright (C) 2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <ctype.h>
#include "common.h"
#include "eiface.h"
#include "sequence.h"

sequenceCommandLine_s g_fileScopeDefaults;
sequenceCommandLine_s g_blockScopeDefaults;
sequenceEntry_s      *g_sequenceList      = NULL;
sentenceGroupEntry_s *g_sentenceGroupList = NULL;
qboolean              g_sequenceParseFileIsGlobal;
unsigned int          g_nonGlobalSentences = 0;
char  g_sequenceParseFileName[MAX_STRING];
int   g_lineNum    = 1;
char *g_scan     = NULL;
char *g_lineScan = NULL;

const sequenceCommandMapping_s g_sequenceCommandMappingTable[] =
{
	{SEQUENCE_COMMAND_PAUSE, "pause", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_TEXT, "text", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_SOUND, "sound", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_FIRETARGETS, "firetargets", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_KILLTARGETS, "killtargets", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_GOSUB, "gosub", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_SENTENCE, "sentence", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_REPEAT, "repeat", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_SETDEFAULTS, "setdefaults", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_MODIFIER, "modifier", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_POSTMODIFIER, "postmodifier", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_COMMAND_NOOP, "noop", SEQUENCE_TYPE_COMMAND},
	{SEQUENCE_MODIFIER_EFFECT, "effect", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_POSITION, "position", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_COLOR, "color", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_COLOR2, "color2", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_FADEIN, "fadein", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_FADEOUT, "fadeout", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_HOLDTIME, "holdtime", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_FXTIME, "fxtime", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_SPEAKER, "speaker", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_LISTENER, "listener", SEQUENCE_TYPE_MODIFIER},
	{SEQUENCE_MODIFIER_TEXTCHANNEL, "channel", SEQUENCE_TYPE_MODIFIER}
};

/*
=============
Sequence_GetCommandEnumForName

=============
*/
sequenceCommandEnum_e Sequence_GetCommandEnumForName( const char *commandName, sequenceCommandType_e type )
{
	int i;

	for( i = 0; i < ARRAYSIZE( g_sequenceCommandMappingTable ); i++ )
	{
		const sequenceCommandMapping_s *mapping = g_sequenceCommandMappingTable + i;

		if( mapping->commandType == type && !Q_stricmp( mapping->commandName, commandName ) )
			return mapping->commandEnum;
	}
	return SEQUENCE_COMMAND_ERROR;
}

/*
=============
Sequence_ResetDefaults

=============
*/
void Sequence_ResetDefaults( sequenceCommandLine_s *destination, sequenceCommandLine_s *source )
{
	if( !source )
	{
		static client_textmessage_t defaultClientMessage =
		{
			0, // effect
			255, 255, 255, 255, // rgba1
			255, 255, 255, 255, // rgba2
			0.5, 0.5, // xy
			0.2, 0.2, // fade-in/out
			1.6, // holdtime
			1.0, // fxtime
			NULL, NULL // pName, pMessage
		};

		destination->clientMessage   = defaultClientMessage;
		destination->textChannel     = 0;
		destination->delay           = 0;
		destination->repeatCount     = 0;
		destination->nextCommandLine = NULL;
		destination->soundFileName   = NULL;
		destination->speakerName     = NULL;
		destination->listenerName    = NULL;
		return;
	}

	destination->clientMessage          = source->clientMessage;
	destination->clientMessage.pName    = NULL;
	destination->clientMessage.pMessage = NULL;
	destination->textChannel            = source->textChannel;
	destination->delay                  = source->delay;
	destination->repeatCount            = source->repeatCount;
	destination->nextCommandLine        = NULL;
	destination->soundFileName          = NULL;

	Z_Free( destination->speakerName );
	destination->speakerName = copystring( source->speakerName );

	Z_Free( destination->listenerName );
	destination->listenerName = copystring( source->listenerName );
}

/*
=============
Sequence_WriteDefaults

=============
*/
void Sequence_WriteDefaults( sequenceCommandLine_s *source, sequenceCommandLine_s *destination )
{
	if( !destination )
		Con_Reportf( S_ERROR  "Attempt to bake defaults into a non-existant command." );

	if( !source )
		Con_Reportf( S_ERROR  "Attempt to bake defaults from a non-existant command." );

	if( source->modifierBitField & SEQUENCE_MODIFIER_EFFECT_BIT )
	{
		destination->clientMessage.effect = source->clientMessage.effect;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_POSITION_BIT )
	{
		destination->clientMessage.x = source->clientMessage.x;
		destination->clientMessage.y = source->clientMessage.y;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_COLOR_BIT )
	{
		destination->clientMessage.r1 = source->clientMessage.r1;
		destination->clientMessage.g1 = source->clientMessage.g1;
		destination->clientMessage.b1 = source->clientMessage.b1;
		destination->clientMessage.a1 = source->clientMessage.a1;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_COLOR2_BIT )
	{
		destination->clientMessage.r2 = source->clientMessage.r2;
		destination->clientMessage.g2 = source->clientMessage.g2;
		destination->clientMessage.b2 = source->clientMessage.b2;
		destination->clientMessage.a2 = source->clientMessage.a2;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_FADEIN_BIT )
	{
		destination->clientMessage.fadein = source->clientMessage.fadein;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_FADEOUT_BIT )
	{
		destination->clientMessage.fadeout = source->clientMessage.fadeout;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_HOLDTIME_BIT )
	{
		destination->clientMessage.holdtime = source->clientMessage.holdtime;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_FXTIME_BIT )
	{
		destination->clientMessage.fxtime = source->clientMessage.fxtime;
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_SPEAKER_BIT )
	{
		Z_Free( destination->speakerName );
		destination->speakerName = copystring( source->speakerName );
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_LISTENER_BIT )
	{
		Z_Free( destination->listenerName );
		destination->listenerName = copystring( source->listenerName );
	}

	if( source->modifierBitField & SEQUENCE_MODIFIER_TEXTCHANNEL_BIT )
	{
		destination->textChannel = source->textChannel;
	}
}

/*
=============
Sequence_BakeDefaults

=============
*/
void Sequence_BakeDefaults( sequenceCommandLine_s *destination, sequenceCommandLine_s *source )
{
	char *saveName, *saveMessage;

	if( !destination )
		Con_Reportf( S_ERROR  "Attempt to bake defaults into a non-existant command." );

	if( !source )
		Con_Reportf( S_ERROR  "Attempt to bake defaults from a non-existant command." );

	saveName= destination->clientMessage.pName;
	saveMessage = destination->clientMessage.pMessage;

	destination->clientMessage = source->clientMessage;

	destination->clientMessage.pName    = saveName;
	destination->clientMessage.pMessage = saveMessage;

	destination->textChannel            = source->textChannel;

	Z_Free( destination->speakerName );
	destination->speakerName = copystring( source->speakerName );

	Z_Free( destination->listenerName );
	destination->listenerName = copystring( source->listenerName );
}

/*
=============
Sequence_SkipWhitespace

=============
*/
qboolean Sequence_SkipWhitespace( void )
{
	qboolean newLine = false;

	for( ; isspace( *g_scan ); g_scan++ )
	{
		if( *g_scan == '\n' )
		{
			g_lineScan = g_scan + 1;
			g_lineNum++;

			newLine = true;
		}
	}

	return newLine;
}

/*
=============
Sequence_IsNameValueChar

=============
*/
qboolean Sequence_IsNameValueChar( char ch )
{
	if( isalnum( ch ) )
		return true;

	switch( ch )
	{
	case '.':
	case '-':
	case '_':
	case '/':
	case '\\':
		return true;
	}

	return false;
}

/*
=============
Sequence_IsSymbol

=============
*/
qboolean Sequence_IsSymbol( char ch )
{
	switch( ch )
	{
	case '"':
	case '#':
	case '$':
	case '%':
	case ',':
	case '=':
	case '@':
	case '{':
	case '}':
		return true;
	}

	return false;
}

/*
=============
Sequence_GetNameValueString

=============
*/
size_t Sequence_GetNameValueString( char *token, size_t len )
{
	char *p;

	Sequence_SkipWhitespace( );

	if( !Sequence_IsNameValueChar( *g_scan ) )
	{
		if( *g_scan == '#' || *g_scan == '$' )
			Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: cannot have more than one '%c' per line; '%c' must be at the beginning of the line ONLY\n", g_lineNum, g_sequenceParseFileName, *g_scan, *g_scan );
		else
			Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: expected name/value, found illegal character '%c'\n", g_lineNum, g_sequenceParseFileName, *g_scan );
	}

	for( p = token; Sequence_IsNameValueChar( *g_scan ) && len;  p++, g_scan++, len-- )
	{
		*p = *g_scan;
	}

	*p = 0;

	return p - token;
}

/*
=============
Sequence_GetSymbol

=============
*/
char Sequence_GetSymbol( void )
{
	char ch;

	Sequence_SkipWhitespace( );

	ch = *g_scan;

	if( ch )
		g_scan++;

	return ch;
}

/*
=============
Sequence_ValidateNameValueString

=============
*/
void Sequence_ValidateNameValueString( char *token )
{
	char *scan;

	for( scan = token; *scan; scan++ )
	{
		if( !Sequence_IsNameValueChar( *scan ) )
			Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: name/value string \"%s\" had illegal character '%c'\n", g_lineNum, g_sequenceParseFileName, token, *scan );
	}
}

/*
=============
Sequence_GetToken

=============
*/
size_t Sequence_GetToken( char *token, size_t size )
{
	Sequence_SkipWhitespace( );

	if( Sequence_IsNameValueChar( *g_scan ) )
	{
		return Sequence_GetNameValueString( token, size );
	}

	if( !Sequence_IsSymbol( *g_scan ) )
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: expected token, found '%c' instead\n", g_lineNum, g_sequenceParseFileName, *g_scan );

	token[0] = *g_scan++;
	token[1] = 0;
	g_scan++;

	return 1; // only one symbol has copied to token
}

/*
=============
Sequence_GetLine

=============
*/
size_t Sequence_GetLine( char *line, int lineMaxLen )
{
	int lineLen;
	char *read;
	char *write = line;

	Sequence_SkipWhitespace( );

	read = Q_strchr( g_scan, '\n' );

	if( !read )
		Con_Reportf( S_ERROR  "Syntax Error on line %d of %s.seq: expected sentence definition or '}', found End-Of-File!\n", g_lineNum, g_sequenceParseFileName );

	lineLen = read - g_scan;

	if( lineLen >= lineMaxLen )
		Con_Reportf( S_ERROR  "Syntax Error on line %d of %s.seq: line was too long (was %d chars; max is %d chars)\n", g_lineNum, g_sequenceParseFileName, lineLen, lineMaxLen - 1 );

	Q_strncpy( write, g_scan, lineLen );
	write[lineLen] = 0;
	g_scan = read;

	return lineLen;
}

/*
=============
Sequence_StripComments

=============
*/
void Sequence_StripComments( char *buffer, int *pBufSize )
{
	char *eof   = buffer + *pBufSize;
	char *read  = buffer;
	char *write = buffer;

	for( ; read < eof; )
	{
		if( !*read )
			break;

		if( *read == '/' )
		{
			// skip one line comments //
			if( read[1] == '/' )
			{
				read += 2;

				while( *read )
				{
					if( *read == '\n' )
						break;

					if( *read == '\r' )
						break;

					read++;
				}

				continue;
			}

			// skip multiline /* */
			if( read[1] == '*' )
			{
				read += 2;

				while( *read && read[1] )
				{
					if( *read == '*' && read[1] == '/' )
					{
						read += 2;
						break;
					}

					if( *read == '\n' || *read == '\r' )
						*write++ = *read;

					read++;
				}

				continue;
			}
		}

		*write++ = *read++;
	}

	*write = 0;
}

/*
=============
Sequence_ReadInt

=============
*/
int Sequence_ReadInt( void )
{
	char str[MAX_STRING];

	Sequence_SkipWhitespace( );
	Sequence_GetNameValueString( str, MAX_STRING );

	return Q_atoi( str );
}

/*
=============
Sequence_ReadFloat

=============
*/
float Sequence_ReadFloat( void )
{
	char str[MAX_STRING];

	Sequence_SkipWhitespace( );
	Sequence_GetNameValueString( str, MAX_STRING );

	return Q_atof( str );
}

/*
=============
Sequence_ReadFloat

=============
*/
void Sequence_ReadString( char **dest, char *string, size_t len )
{
	Sequence_SkipWhitespace( );
	Sequence_GetNameValueString( string, len );

	if( dest ) *dest = copystring( string );
}

/*
=============
Sequence_ReadQuotedString

=============
*/
void Sequence_ReadQuotedString( char **dest, char *str, size_t len )
{
	char *write, ch;

	Sequence_SkipWhitespace( );

	ch = Sequence_GetSymbol( );
	if( ch != '\"' )
		Con_Reportf( S_ERROR  "Parsing error on or before line %d of %s.seq: expected quote (\"), found '%c' instead\n", g_lineNum, g_sequenceParseFileName, ch );

	for( write = str; *g_scan && len; write++, g_scan++, len-- )
	{
		if( *g_scan == '\"' )
			break;

		if( *g_scan == '\n' )
			g_lineNum++;

		*write = *g_scan;
	}

	*write = 0;
	g_scan++;

	if( dest ) *dest = copystring( str );
}

/*
=============
Sequence_ConfirmCarriageReturnOrSymbol

=============
*/
qboolean Sequence_ConfirmCarriageReturnOrSymbol( char symbol )
{
	if( Sequence_SkipWhitespace( ) )
		return true;
	return *g_scan == symbol;
}


/*
=============
Sequence_IsCommandAModifier

=============
*/
qboolean Sequence_IsCommandAModifier( sequenceCommandEnum_e commandEnum )
{
	int i;

	for( i = 0; i < ARRAYSIZE( g_sequenceCommandMappingTable ); i++ )
	{
		if( g_sequenceCommandMappingTable[i].commandEnum == commandEnum )
			return ( g_sequenceCommandMappingTable[i].commandType == SEQUENCE_TYPE_MODIFIER );
	}

	Con_Reportf( S_ERROR  "Internal error caused by line %d of %s.seq: unknown command enum = %d\n", g_lineNum, g_sequenceParseFileName, commandEnum );
	return false;
}

/*
=============
Sequence_ReadCommandData

=============
*/
void Sequence_ReadCommandData( sequenceCommandEnum_e commandEnum, sequenceCommandLine_s *defaults )
{
	char temp[1024];

	if( commandEnum >= SEQUENCE_MODIFIER_EFFECT && commandEnum <= SEQUENCE_MODIFIER_TEXTCHANNEL )
		defaults->modifierBitField |= BIT( SEQUENCE_MODIFIER_EFFECT - SEQUENCE_COMMAND_NOOP );

	switch( commandEnum )
	{
	case SEQUENCE_COMMAND_PAUSE:
		defaults->delay = Sequence_ReadFloat( );
		break;

	case SEQUENCE_COMMAND_FIRETARGETS:
		Sequence_ReadQuotedString( &defaults->fireTargetNames, temp, sizeof( temp ) );
		break;

	case SEQUENCE_COMMAND_KILLTARGETS:
		Sequence_ReadQuotedString( &defaults->killTargetNames, temp, sizeof( temp ) );
		break;

	case SEQUENCE_COMMAND_TEXT:
		Sequence_ReadQuotedString( &defaults->clientMessage.pMessage, temp, sizeof( temp ) );
		break;

	case SEQUENCE_COMMAND_SOUND:
		Sequence_ReadString( &defaults->soundFileName, temp, sizeof( temp ) );
		break;

	case SEQUENCE_COMMAND_GOSUB:
		Sequence_ReadString( &defaults->clientMessage.pName, temp, sizeof( temp ) );
		break;

	case SEQUENCE_COMMAND_SENTENCE:
		Sequence_ReadString( &defaults->sentenceName, temp, sizeof( temp ) );
		break;

	case SEQUENCE_COMMAND_REPEAT:
		defaults->repeatCount = Sequence_ReadInt( );
		break;

	case SEQUENCE_MODIFIER_EFFECT:
		defaults->clientMessage.effect = Sequence_ReadInt( );
		break;

	case SEQUENCE_MODIFIER_POSITION:
		defaults->clientMessage.x = Sequence_ReadFloat( );
		defaults->clientMessage.y = Sequence_ReadFloat( );
		break;

	case SEQUENCE_MODIFIER_COLOR:
		defaults->clientMessage.r1 = Sequence_ReadInt( );
		defaults->clientMessage.g1 = Sequence_ReadInt( );
		defaults->clientMessage.b1 = Sequence_ReadInt( );
		defaults->clientMessage.a1 = 255;
		break;

	case SEQUENCE_MODIFIER_COLOR2:
		defaults->clientMessage.r2 = Sequence_ReadInt( );
		defaults->clientMessage.g2 = Sequence_ReadInt( );
		defaults->clientMessage.b2 = Sequence_ReadInt( );
		defaults->clientMessage.a2 = 255;
		break;

	case SEQUENCE_MODIFIER_FADEIN:
		defaults->clientMessage.fadein = Sequence_ReadFloat( );
		break;

	case SEQUENCE_MODIFIER_FADEOUT:
		defaults->clientMessage.fadeout = Sequence_ReadFloat( );
		break;

	case SEQUENCE_MODIFIER_HOLDTIME:
		defaults->clientMessage.holdtime = Sequence_ReadFloat( );
		break;

	case SEQUENCE_MODIFIER_FXTIME:
		defaults->clientMessage.fxtime = Sequence_ReadFloat( );
		break;

	case SEQUENCE_MODIFIER_SPEAKER:
		Sequence_ReadString( &defaults->speakerName, temp, sizeof( temp ) );
		break;

	case SEQUENCE_MODIFIER_LISTENER:
		Sequence_ReadString( &defaults->listenerName, temp, sizeof( temp ) );
		break;

	case SEQUENCE_MODIFIER_TEXTCHANNEL:
		defaults->textChannel = Sequence_ReadInt( );
		break;

	default:
		Con_Reportf( S_ERROR  "Internal error caused by line %d of %s.seq: unknown command enum = %d\n", g_lineNum, g_sequenceParseFileName, commandEnum );
	}
}

/*
=============
Sequence_ParseModifier

=============
*/
char Sequence_ParseModifier( sequenceCommandLine_s *defaults )
{
	char modifierName[MAX_STRING];
	char delimiter;
	sequenceCommandEnum_e modifierEnum;

	Sequence_GetNameValueString( modifierName, MAX_STRING );
	modifierEnum = Sequence_GetCommandEnumForName( modifierName, SEQUENCE_TYPE_MODIFIER );

	if( modifierEnum == SEQUENCE_COMMAND_ERROR )
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: unknown modifier \"%s\"\n", g_lineNum, g_sequenceParseFileName, modifierName );

	if( !Sequence_IsCommandAModifier( modifierEnum ) )
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: \"%s\" is a #command, not a $modifier\n", g_lineNum, g_sequenceParseFileName, modifierName );

	delimiter = Sequence_GetSymbol( );

	if( delimiter != '=' )
		Con_Reportf( S_ERROR  "Parsing error on or after line %d of %s.seq: after modifier \"%s\", expected '=', found '%c'\n", g_lineNum, g_sequenceParseFileName, modifierName, delimiter );

	Sequence_ReadCommandData( modifierEnum, defaults );

	if( !Sequence_ConfirmCarriageReturnOrSymbol( ',' ) )
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: after value(s) for modifier \"%s\", expected ',' or End-Of-Line; found '%c'\n", g_lineNum, g_sequenceParseFileName, modifierName, *g_scan );

	return Sequence_GetSymbol( );
}

/*
=============
Sequence_AddCommandLineToEntry

=============
*/
void Sequence_AddCommandLineToEntry( sequenceCommandLine_s *commandLine, sequenceEntry_s *entry )
{
	sequenceCommandLine_s *scan;

	if( entry->firstCommand )
	{
		for( scan = entry->firstCommand; scan->nextCommandLine; scan = scan->nextCommandLine );
		scan->nextCommandLine = commandLine;
	}
	else entry->firstCommand = commandLine;

	commandLine->nextCommandLine = NULL;
}

/*
=============
Sequence_ParseModifierLine

=============
*/
char Sequence_ParseModifierLine( sequenceEntry_s *entry, sequenceCommandType_e modifierType )
{
	sequenceCommandLine_s *newCommandLine;
	char delimiter = ',';

	while( delimiter == ',' )
	{
		switch( modifierType )
		{
		case SEQUENCE_TYPE_COMMAND:
			newCommandLine = Z_Malloc( sizeof( sequenceCommandLine_s ) );
			memset( newCommandLine, 0, sizeof( sequenceCommandLine_s ) );
			newCommandLine->commandType = SEQUENCE_COMMAND_MODIFIER;
			Sequence_AddCommandLineToEntry( newCommandLine, entry );
			delimiter = Sequence_ParseModifier( newCommandLine );
			break;

		case SEQUENCE_TYPE_MODIFIER:
			delimiter = Sequence_ParseModifier( &g_fileScopeDefaults );
			break;
		}
	}

	return delimiter;
}

/*
=============
Sequence_ParseCommand

=============
*/
char Sequence_ParseCommand( sequenceCommandLine_s *newCommandLine )
{
	char commandName[MAX_STRING], ch;
	sequenceCommandEnum_e commandEnum;
	sequenceCommandLine_s *modifierCommandLine;

	Sequence_GetNameValueString( commandName, MAX_STRING );
	commandEnum = Sequence_GetCommandEnumForName( commandName, SEQUENCE_TYPE_COMMAND );

	if( commandEnum == SEQUENCE_COMMAND_ERROR )
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: unknown command \"%s\"\n", g_lineNum, g_sequenceParseFileName, commandName );

	if( Sequence_IsCommandAModifier( commandEnum ) )
	{
		modifierCommandLine = Z_Malloc( sizeof( sequenceCommandLine_s ) );
		memset( modifierCommandLine, 0, sizeof( sequenceCommandLine_s ) );
		modifierCommandLine->commandType = SEQUENCE_COMMAND_POSTMODIFIER;

		for( ; newCommandLine->nextCommandLine; newCommandLine = newCommandLine->nextCommandLine );

		newCommandLine->nextCommandLine = modifierCommandLine;
		newCommandLine = modifierCommandLine;
	}

	ch = Sequence_GetSymbol( );
	if( ch != '=' )
		Con_Reportf( S_ERROR  "Parsing error on or before line %d of %s.seq: after command \"%s\", expected '=', found '%c'\n",
				   g_lineNum, g_sequenceParseFileName, commandName, ch );

	Sequence_ReadCommandData( commandEnum, newCommandLine );
	return Sequence_GetSymbol( );
}

/*
=============
Sequence_ParseCommandLine

=============
*/
char Sequence_ParseCommandLine( sequenceEntry_s *entry )
{
	char symbol;
	sequenceCommandLine_s *newCommandLine;

	newCommandLine = Z_Malloc( sizeof( sequenceCommandLine_s ) );
	memset( newCommandLine, 0, sizeof( sequenceCommandLine_s ) );

	Sequence_ResetDefaults( newCommandLine, &g_blockScopeDefaults );
	Sequence_AddCommandLineToEntry( newCommandLine, entry );

	symbol = Sequence_ParseCommand( newCommandLine );

	while( symbol == ',' )
	{
		symbol = Sequence_ParseCommand( newCommandLine );
	}

	return symbol;
}

/*
=============
Sequence_ParseMacro

=============
*/
char Sequence_ParseMacro( sequenceEntry_s *entry )
{
	char symbol;
	sequenceCommandLine_s *newCommandLine;

	newCommandLine = Z_Malloc( sizeof( sequenceCommandLine_s ) );
	memset( newCommandLine, 0, sizeof( sequenceCommandLine_s ) );

	Sequence_ResetDefaults( newCommandLine, &g_blockScopeDefaults );
	Sequence_AddCommandLineToEntry( newCommandLine, entry );
	Sequence_ReadCommandData( SEQUENCE_COMMAND_GOSUB, newCommandLine );

	symbol = Sequence_GetSymbol( );

	while( symbol == ',' )
	{
		symbol = Sequence_ParseCommand( newCommandLine );
	}

	return symbol;
}

/*
=============
Sequence_ParseLine

=============
*/
char Sequence_ParseLine( char start, sequenceEntry_s *entry )
{
	char end = '\0';

	switch( start )
	{
	case '#':
		end = Sequence_ParseCommandLine( entry );
		break;

	case '$':
		end = Sequence_ParseModifierLine( entry, SEQUENCE_TYPE_MODIFIER );
		break;

	case '@':
		end = Sequence_ParseMacro( entry );
		break;

	default:
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: line must begin with either '#' (command) or '$' (modifier); found '%c'\n", g_lineNum, g_sequenceParseFileName, start );
	}

	return end;
}

/*
=============
Sequence_CalcEntryDuration

=============
*/
float Sequence_CalcEntryDuration( sequenceEntry_s *entry )
{
	float duration;
	sequenceCommandLine_s *cmd;

	duration = 0;

	for( cmd = entry->firstCommand; cmd; cmd = cmd->nextCommandLine )
		duration += cmd->delay;

	return duration;
}

/*
=============
Sequence_DoesEntryContainInfiniteLoop

=============
*/
qboolean Sequence_DoesEntryContainInfiniteLoop( sequenceEntry_s *entry )
{
	sequenceCommandLine_s *cmd;

	for( cmd = entry->firstCommand; cmd; cmd = cmd->nextCommandLine )
	{
		if( cmd->repeatCount < 0 )
			return true;
	}

	return false;
}

/*
=============
Sequence_IsEntrySafe

=============
*/
qboolean Sequence_IsEntrySafe( sequenceEntry_s *entry )
{
	float duration;
	sequenceCommandLine_s *cmd;

	duration = 0;

	for( cmd = entry->firstCommand; cmd; cmd = cmd->nextCommandLine )
	{
		duration += cmd->delay;

		if( cmd->repeatCount < 0 )
		{
			if( duration <= 0 )
				return false;
		}
	}

	return true;
}

/*
=============
Sequence_CreateDefaultsCommand

=============
*/
void Sequence_CreateDefaultsCommand( sequenceEntry_s *entry )
{
	sequenceCommandLine_s *cmd;

	cmd = Z_Malloc( sizeof( sequenceCommandLine_s ) );
	memset( cmd, 0, sizeof( sequenceCommandLine_s ) );

	Sequence_ResetDefaults( cmd, &g_fileScopeDefaults );
	cmd->commandType      = SEQUENCE_COMMAND_SETDEFAULTS;
	cmd->modifierBitField = SEQUENCE_MODIFIER_EFFECT_BIT   |
							SEQUENCE_MODIFIER_POSITION_BIT |
							SEQUENCE_MODIFIER_COLOR_BIT    |
							SEQUENCE_MODIFIER_COLOR2_BIT   |
							SEQUENCE_MODIFIER_FADEIN_BIT   |
							SEQUENCE_MODIFIER_FADEOUT_BIT  |
							SEQUENCE_MODIFIER_HOLDTIME_BIT |
							SEQUENCE_MODIFIER_FXTIME_BIT;

	Sequence_AddCommandLineToEntry( cmd, entry );
}


/*
=============
Sequence_ParseEntry

=============
*/
char Sequence_ParseEntry( void )
{
	char symbol;
	char token[MAX_STRING];
	sequenceEntry_s *entry;

	Sequence_GetNameValueString( token, MAX_STRING );
	symbol = Sequence_GetSymbol( );

	if( symbol != '{' )
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: expected '{' to start a\n new entry block; found '%c' instead!", g_lineNum, g_sequenceParseFileName, symbol );

	entry = Z_Malloc( sizeof( sequenceEntry_s ) );
	Sequence_ResetDefaults( &g_blockScopeDefaults, &g_fileScopeDefaults );
	entry->entryName    = copystring( token );
	entry->fileName     = copystring( g_sequenceParseFileName );
	entry->isGlobal     = g_sequenceParseFileIsGlobal;
	entry->firstCommand = NULL;
	Sequence_CreateDefaultsCommand( entry );

	symbol = Sequence_GetSymbol( );

	while( symbol != '}' )
	{
		symbol = Sequence_ParseLine( symbol, entry );
	}

	if( !Sequence_IsEntrySafe( entry ) )
		Con_Reportf( S_ERROR  "Logic error in file %s.seq before line %d: execution of entry \"%%%s\" would cause an infinite loop!", g_sequenceParseFileName, g_lineNum, entry->entryName );

	entry->nextEntry = g_sequenceList;
	g_sequenceList   = entry;

	return Sequence_GetSymbol( );
}

/*
=============
Sequence_FindSentenceGroup

=============
*/
sentenceGroupEntry_s *Sequence_FindSentenceGroup( const char *groupName )
{
	sentenceGroupEntry_s *groupEntry;

	for( groupEntry = g_sentenceGroupList; groupEntry; groupEntry = groupEntry->nextEntry )
	{
		if( !Q_stricmp( groupEntry->groupName, groupName ) )
			return groupEntry;
	}

	return NULL;
}

/*
=============
Sequence_GetSentenceByIndex

=============
*/
sentenceEntry_s *Sequence_GetSentenceByIndex( unsigned int index )
{
	sentenceEntry_s *sentenceEntry;
	sentenceGroupEntry_s *groupEntry;
	unsigned int sentenceCount=0;

	for( groupEntry = g_sentenceGroupList; groupEntry; groupEntry = groupEntry->nextEntry )
	{
		sentenceCount += groupEntry->numSentences;

		if( index < sentenceCount )
		{
			for( sentenceEntry = groupEntry->firstSentence; sentenceEntry;  sentenceEntry = sentenceEntry->nextEntry )
			{
				if( sentenceEntry->index == index )
					return sentenceEntry;
			}
		}
	}

	return NULL;
}


/*
=============
Sequence_PickSentence

=============
*/
sentenceEntry_s *Sequence_PickSentence( const char *groupName, int pickMethod, int *picked )
{
	sentenceEntry_s *sentenceEntry;
	sentenceGroupEntry_s *groupEntry;
	unsigned int pickedIdx;
	unsigned int entryIdx;

	groupEntry = Sequence_FindSentenceGroup( groupName );

	if( groupEntry )
	{
		pickedIdx     = COM_RandomLong( 0, groupEntry->numSentences - 1 );
		sentenceEntry = groupEntry->firstSentence;

		for( entryIdx = pickedIdx; entryIdx; entryIdx-- )
			sentenceEntry = sentenceEntry->nextEntry;
	}
	else
	{
		pickedIdx   = 0;
		sentenceEntry = NULL;
	}

	if( picked )
		*picked = pickedIdx;

	return sentenceEntry;
}

/*
=============
Sequence_AddSentenceGroup

=============
*/
sentenceGroupEntry_s *Sequence_AddSentenceGroup( char *groupName )
{
	sentenceGroupEntry_s *entry, *last;

	entry                = Z_Malloc( sizeof( sentenceGroupEntry_s ) );
	entry->numSentences  = 0;
	entry->firstSentence = NULL;
	entry->nextEntry     = NULL;
	entry->groupName     = copystring( groupName );

	if( g_sentenceGroupList )
	{
		for( last = g_sentenceGroupList; last->nextEntry; last = last->nextEntry );
		last->nextEntry = entry;
	}
	else
	{
		g_sentenceGroupList = entry;
	}

	return entry;
}

/*
=============
Sequence_AddSentenceToGroup

=============
*/
void Sequence_AddSentenceToGroup( char *groupName, char *data )
{
	sentenceEntry_s *entry, *last;
	sentenceGroupEntry_s *group;

	group = Sequence_FindSentenceGroup( groupName );

	if( !group )
	{
		group = Sequence_AddSentenceGroup( groupName );

		if( !group )
			Con_Reportf( S_ERROR  "Unable to allocate sentence group %s at line %d in file %s.seq", groupName, g_lineNum, g_sequenceParseFileName );
	}

	entry            = Z_Malloc( sizeof( sentenceEntry_s ) );
	entry->nextEntry = NULL;
	entry->data      = copystring( data );
	entry->index     = g_nonGlobalSentences;
	entry->isGlobal  = g_sequenceParseFileIsGlobal;

	group->numSentences++;
	g_nonGlobalSentences++;

	if( group->firstSentence )
	{
		for( last = group->firstSentence; last->nextEntry; last = last->nextEntry );

		last->nextEntry = entry;
	}
	else
	{
		group->firstSentence = entry;
	}
}

/*
=============
Sequence_ParseSentenceLine

=============
*/
qboolean Sequence_ParseSentenceLine( void )
{
	char data[1024];
	char fullgroup[64];
	char groupName[64];
	char *c;
	int lastCharacterPos;
	size_t len;

	len = Sequence_GetToken( fullgroup, sizeof( fullgroup ) );

	if( *fullgroup == '}' )
		return true;

	c = fullgroup + len;

	while( !isalpha( *c ) && *c != '_' )
		c--;

	c += 1;

	if( *c )
		*c = 0;

	Q_strncpy( groupName, fullgroup, sizeof( groupName ));

	len = Sequence_GetLine( data, sizeof( data ) );
	lastCharacterPos = len - 1;

	if( data[lastCharacterPos] == '\n' || data[lastCharacterPos] == '\r' )
		data[lastCharacterPos] = 0;

	Sequence_AddSentenceToGroup( groupName, data );
	return false;
}

/*
==============
Sequence_ParseSentenceBlock

==============
*/
char Sequence_ParseSentenceBlock( void )
{
	qboolean end = false;
	char ch = Sequence_GetSymbol( );
	if( ch != '{' )
		Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: expected '{' to start a\n new sentence block; found '%c' instead!", g_lineNum, g_sequenceParseFileName, ch );

	while( !end )
	{
		end = Sequence_ParseSentenceLine( );
	}

	return Sequence_GetSymbol( );
}

/*
==============
Sequence_ParseGlobalDataBlock

==============
*/
char Sequence_ParseGlobalDataBlock( void )
{
	char token[MAX_STRING];

	Sequence_GetNameValueString( token, MAX_STRING );

	if( Q_stricmp( token, "Sentences" ) )
		Con_Reportf( S_ERROR  "Syntax error in file %s.seq on line %d: found global data block symbol '!' with unknown data type \"%s\"", g_sequenceParseFileName, g_lineNum, token );

	return Sequence_ParseSentenceBlock( );
}

/*
==============
Sequence_GetEntryForName

==============
*/
sequenceEntry_s *Sequence_GetEntryForName( const char *entryName )
{
	sequenceEntry_s *scan;

	for( scan = g_sequenceList; scan; scan = scan->nextEntry )
	{
		if( !Q_stricmp( entryName, scan->entryName ) )
			return scan;
	}

	return NULL;
}

/*
==============
Sequence_CopyCommand

==============
*/
sequenceCommandLine_s *Sequence_CopyCommand( sequenceCommandLine_s *commandOrig )
{
	sequenceCommandLine_s *commandCopy;

	commandCopy = Z_Malloc( sizeof( sequenceCommandLine_s ) );

	commandCopy->commandType      = commandOrig->commandType;
	commandCopy->clientMessage    = commandOrig->clientMessage;
	commandCopy->clientMessage.pMessage = copystring( commandOrig->clientMessage.pMessage );
	commandCopy->clientMessage.pName    = copystring( commandOrig->clientMessage.pName );
	commandCopy->speakerName      = copystring( commandOrig->speakerName );
	commandCopy->listenerName     = copystring( commandOrig->listenerName );
	commandCopy->soundFileName    = copystring( commandOrig->soundFileName );
	commandCopy->sentenceName     = copystring( commandOrig->sentenceName );
	commandCopy->fireTargetNames  = copystring( commandOrig->fireTargetNames );
	commandCopy->killTargetNames  = copystring( commandOrig->killTargetNames );
	commandCopy->delay            = commandOrig->delay;
	commandCopy->repeatCount      = commandOrig->repeatCount;
	commandCopy->textChannel      = commandOrig->textChannel;
	commandCopy->modifierBitField = commandOrig->modifierBitField;
	commandCopy->nextCommandLine  = NULL;

	return commandCopy;
}

/*
==============
Sequence_CopyCommandList

==============
*/
sequenceCommandLine_s *Sequence_CopyCommandList( sequenceCommandLine_s *list )
{
	sequenceCommandLine_s *scan, *copy, *new, *prev;

	copy = NULL;
	prev = NULL;

	for( scan = list; scan; scan = scan->nextCommandLine )
	{
		if( scan->commandType != SEQUENCE_COMMAND_SETDEFAULTS )
		{
			new = Sequence_CopyCommand( scan );

			if( prev )
			{
				prev->nextCommandLine = new;
				prev                  = new;
			}
			else
			{
				prev = new;
				copy = new;
			}
		}
	}

	return copy;
}

/*
==============
Sequence_ExpandGosubsForEntry

==============
*/
qboolean Sequence_ExpandGosubsForEntry( sequenceEntry_s *entry )
{
	sequenceCommandLine_s *cmd, *copyList, *scan;
	sequenceEntry_s *gosubEntry;
	qboolean foundGosubs = false;

	for( cmd = entry->firstCommand; cmd; cmd = cmd->nextCommandLine )
	{
		if( !cmd->clientMessage.pName )
			continue;

		if( !Q_stricmp( cmd->clientMessage.pName, entry->entryName ) )
			Con_Reportf( S_ERROR  "Error in %s.seq: entry \"%s\" gosubs itself!\n", entry->fileName, entry->entryName );

		gosubEntry = Sequence_GetEntryForName( cmd->clientMessage.pName );

		if( !gosubEntry )
			Con_Reportf( S_ERROR  "Error in %s.seq: Gosub in entry \"%s\" specified unknown entry \"%s\"\n", entry->fileName, entry->entryName, cmd->clientMessage.pName );

		foundGosubs = true;
		copyList    = Sequence_CopyCommandList( gosubEntry->firstCommand );

		if( copyList )
		{
			for( scan = copyList->nextCommandLine; scan; scan = scan->nextCommandLine );

			scan->nextCommandLine = cmd->nextCommandLine;

			Z_Free( cmd->clientMessage.pName );
			cmd->clientMessage.pName = NULL;
			cmd                      = scan;
		}
		else
		{
			Z_Free( cmd->clientMessage.pName );
			cmd->clientMessage.pName = NULL;
		}
	}

	return !foundGosubs;
}

/*
==============
Sequence_ExpandAllGosubs

==============
*/
void Sequence_ExpandAllGosubs( void )
{
	sequenceEntry_s *scan;
	qboolean isComplete = true;

	while( !isComplete )
	{
		for( scan = g_sequenceList; scan; scan = scan->nextEntry )
		{
			isComplete = Sequence_ExpandGosubsForEntry( scan );
		}
	}
}

/*
==============
Sequence_FlattenEntry

==============
*/
void Sequence_FlattenEntry( sequenceEntry_s *entry )
{
	sequenceCommandLine_s *cmd, *last = NULL;

	for( cmd = entry->firstCommand; cmd; cmd = cmd->nextCommandLine )
	{
		switch( cmd->commandType )
		{
		case SEQUENCE_COMMAND_SETDEFAULTS:
			Sequence_WriteDefaults( cmd, &g_blockScopeDefaults );
			cmd->commandType = SEQUENCE_COMMAND_NOOP;
			break;

		case SEQUENCE_COMMAND_MODIFIER:
			Sequence_WriteDefaults( cmd, &g_blockScopeDefaults );
			break;

		case SEQUENCE_COMMAND_POSTMODIFIER:
			Sequence_WriteDefaults( cmd, last );
			break;

		default:
			Sequence_BakeDefaults( cmd, &g_blockScopeDefaults );
			last = cmd;
		}
	}
}

/*
==============
Sequence_FlattenAllEntries

==============
*/
void Sequence_FlattenAllEntries( void )
{
	sequenceEntry_s *entry;

	for( entry = g_sequenceList; entry; entry = entry->nextEntry )
		Sequence_FlattenEntry( entry );
}

/*
==============
Sequence_ParseBuffer

==============
*/
static void Sequence_ParseBuffer( char *buffer, int bufferSize )
{
	char symbol;

	g_lineNum  = 1;
	g_scan     = buffer;
	g_lineScan = g_scan;

	Sequence_StripComments( buffer, &bufferSize );
	Sequence_ResetDefaults( &g_fileScopeDefaults, NULL );

	symbol = Sequence_GetSymbol( );

	while( symbol )
	{
		switch( symbol )
		{
		case '$':
			do
				symbol = Sequence_ParseModifier( &g_fileScopeDefaults );
			while( symbol == ',' );
			break;

		case '%':
			symbol = Sequence_ParseEntry( );
			break;

		case '!':
			symbol = Sequence_ParseGlobalDataBlock( );
			break;

		default:
			Con_Reportf( S_ERROR  "Parsing error on line %d of %s.seq: At file scope, lines must begin with '$' (modifier) or '%%' (entry block) or '!' (sentence / global data block); found '%c'\n", g_lineNum, g_sequenceParseFileName, symbol );
		}
	}

	Sequence_ExpandAllGosubs( );
	Sequence_FlattenAllEntries( );
}

/*
==============
Sequence_ParseFile

==============
*/
void Sequence_ParseFile( const char *fileName, qboolean isGlobal )
{
	byte *buffer;
	fs_offset_t bufSize = 0;

	Q_strcpy( g_sequenceParseFileName, fileName );
	g_sequenceParseFileIsGlobal = isGlobal;

	buffer = FS_LoadFile( va("sequences/%s.seq", fileName ), &bufSize, true );

	if( !buffer )
		return;

	Con_Reportf( "reading sequence file: %s\n", fileName );

	Sequence_ParseBuffer( (char *)buffer, bufSize );

	Mem_Free( buffer );
}

/*
==============
Sequence_Init

==============
*/
void Sequence_Init( void )
{
	Sequence_ParseFile( "global", true );
}

/*
==============
SequenceGet

==============
*/
sequenceEntry_s *Sequence_Get( const char *fileName, const char *entryName )
{
	sequenceEntry_s *scan;

	for( scan = g_sequenceList; scan; scan = scan->nextEntry )
	{
		if( ( !fileName || !Q_stricmp( fileName, scan->fileName ) ) && // a1ba: add filename check, even if originally it is ignored
			!Q_stricmp( entryName, scan->entryName ) )
			return scan;
	}

	return NULL;
}

/*
==============
Sequence_FreeCommand

==============
*/
void Sequence_FreeCommand( sequenceCommandLine_s *kill )
{
	Z_Free( kill->fireTargetNames );
	Z_Free( kill->speakerName );
	Z_Free( kill->listenerName );
	Z_Free( kill->soundFileName );
	Z_Free( kill->sentenceName );
	Z_Free( kill->clientMessage.pName );
	Z_Free( kill->clientMessage.pMessage );
}

/*
==============
Sequence_FreeEntry

==============
*/
void Sequence_FreeEntry( sequenceEntry_s *kill )
{
	sequenceCommandLine_s *dead;

	Z_Free( kill->entryName );
	Z_Free( kill->fileName );

	for( dead = kill->firstCommand; dead; dead = dead->nextCommandLine )
	{
		kill->firstCommand = dead->nextCommandLine;
		Sequence_FreeCommand( dead );
	}

	Z_Free( kill );
}

/*
==============
Sequence_FreeSentence

==============
*/
void Sequence_FreeSentence( sentenceEntry_s *sentenceEntry )
{
	Z_Free( sentenceEntry->data );
	Z_Free( sentenceEntry );
}

/*
==============
Sequence_FreeSentenceGroup

==============
*/
void Sequence_FreeSentenceGroup( sentenceGroupEntry_s *groupEntry )
{
	Z_Free( groupEntry->groupName );
	Z_Free( groupEntry );
}

/*
==============
Sequence_FreeSentenceGroupEntries

==============
*/
void Sequence_FreeSentenceGroupEntries( sentenceGroupEntry_s *groupEntry, qboolean purgeGlobals )
{
	sentenceEntry_s *sentenceEntry;
	sentenceEntry_s *deadSentence;
	sentenceEntry_s *prevSentence;

	sentenceEntry = groupEntry->firstSentence;
	prevSentence  = NULL;

	while( sentenceEntry )
	{
		if( !sentenceEntry->isGlobal || purgeGlobals )
		{
			if( prevSentence )
				prevSentence->nextEntry = sentenceEntry->nextEntry;
			else
				groupEntry->firstSentence = sentenceEntry->nextEntry;

			groupEntry->numSentences--;
			g_nonGlobalSentences--;

			deadSentence  = sentenceEntry;
			sentenceEntry = sentenceEntry->nextEntry;

			Sequence_FreeSentence( deadSentence );
		}
		else
		{
			prevSentence  = sentenceEntry;
			sentenceEntry = sentenceEntry->nextEntry;
		}
	}
}

/*
==============
Sequence_PurgeEntries

==============
*/
void Sequence_PurgeEntries( qboolean purgeGlobals )
{
	sequenceEntry_s *scan;
	sequenceEntry_s *dead;
	sequenceEntry_s *prev;
	sentenceGroupEntry_s *groupEntry;
	sentenceGroupEntry_s *deadGroup;
	sentenceGroupEntry_s *prevGroup;

	dead = NULL;
	prev = NULL;

	for( scan = g_sequenceList; scan; )
	{
		if( !scan->isGlobal || purgeGlobals )
		{
			if( prev )
				prev->nextEntry = scan->nextEntry;
			else
				g_sequenceList = scan->nextEntry;

			dead = scan;
			scan = scan->nextEntry;
			Sequence_FreeEntry( dead );
		}
		else
		{
			prev = scan;
			scan = scan->nextEntry;
		}
	}

	groupEntry = g_sentenceGroupList;
	prevGroup  = NULL;

	while( groupEntry )
	{
		Sequence_FreeSentenceGroupEntries( groupEntry, purgeGlobals );

		if( groupEntry->numSentences )
		{
			prevGroup  = groupEntry;
			groupEntry = groupEntry->nextEntry;
		}
		else
		{
			if( prevGroup )
				prevGroup->nextEntry = groupEntry->nextEntry;
			else
				g_sentenceGroupList = groupEntry->nextEntry;

			deadGroup  = groupEntry;
			groupEntry = groupEntry->nextEntry;
			Sequence_FreeSentenceGroup( deadGroup );
		}
	}
}

/*
==============
Sequence_OnLevelLoad

==============
*/
void Sequence_OnLevelLoad( const char *mapName )
{
	Sequence_PurgeEntries( false );
	Sequence_ParseFile( mapName, false );
}
