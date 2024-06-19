/*
titles.c - implementation of titles.txt parser
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

#include "common.h"
#include "client.h"

#define MAX_MESSAGES	2048

#define MSGFILE_NAME	0
#define MSGFILE_TEXT	1

client_textmessage_t	gMessageParms;

// the string "pText" is assumed to have all whitespace from both ends cut out
static int IsComment( const char *pText )
{
	if( pText )
	{
		int length = Q_strlen( pText );

		if( length >= 2 && pText[0] == '/' && pText[1] == '/' )
			return 1;

		// no text?
		if( length > 0 )
			return 0;
	}

	// no text is a comment too
	return 1;
}

// the string "pText" is assumed to have all whitespace from both ends cut out
static int IsStartOfText( const char *pText )
{
	if( pText )
	{
		if( pText[0] == '{' )
			return 1;
	}
	return 0;
}

// the string "pText" is assumed to have all whitespace from both ends cut out
static int IsEndOfText( const char *pText )
{
	if( pText )
	{
		if( pText[0] == '}' )
			return 1;
	}
	return 0;
}

static int IsWhiteSpace( char space )
{
	if( space == ' ' || space == '\t' || space == '\r' || space == '\n' )
		return 1;
	return 0;
}

static const char *SkipSpace( const char *pText )
{
	if( pText )
	{
		int pos = 0;
		while( pText[pos] && IsWhiteSpace( pText[pos] ))
			pos++;
		return pText + pos;
	}
	return NULL;
}

static const char *SkipText( const char *pText )
{
	if( pText )
	{
		int pos = 0;
		while( pText[pos] && !IsWhiteSpace( pText[pos] ))
			pos++;
		return pText + pos;
	}
	return NULL;
}

static int ParseFloats( const char *pText, float *pFloat, int count )
{
	const char *pTemp = pText;
	int index = 0;

	while( pTemp && count > 0 )
	{
		// skip current token / float
		pTemp = SkipText( pTemp );
		// skip any whitespace in between
		pTemp = SkipSpace( pTemp );

		if( pTemp )
		{
			// parse a float
			pFloat[index] = Q_atof( pTemp );
			count--;
			index++;
		}
	}

	if( count == 0 )
		return 1;
	return 0;
}

static int IsToken( const char *pText, const char *pTokenName )
{
	if( !pText || !pTokenName )
		return 0;

	if( !Q_strnicmp( pText+1, pTokenName, Q_strlen( pTokenName )))
		return 1;

	return 0;
}

static int ParseDirective( const char *pText )
{
	if( pText && pText[0] == '$' )
	{
		float	tempFloat[8];

		if( IsToken( pText, "position" ))
		{
			if( ParseFloats( pText, tempFloat, 2 ))
			{
				gMessageParms.x = tempFloat[0];
				gMessageParms.y = tempFloat[1];
			}
		}
		else if( IsToken( pText, "effect" ))
		{
			if( ParseFloats( pText, tempFloat, 1 ))
			{
				gMessageParms.effect = (int)tempFloat[0];
			}
		}
		else if( IsToken( pText, "fxtime" ))
		{
			if( ParseFloats( pText, tempFloat, 1 ))
			{
				gMessageParms.fxtime = tempFloat[0];
			}
		}
		else if( IsToken( pText, "color2" ))
		{
			if( ParseFloats( pText, tempFloat, 3 ))
			{
				gMessageParms.r2 = (int)tempFloat[0];
				gMessageParms.g2 = (int)tempFloat[1];
				gMessageParms.b2 = (int)tempFloat[2];
			}
		}
		else if( IsToken( pText, "color" ))
		{
			if( ParseFloats( pText, tempFloat, 3 ))
			{
				gMessageParms.r1 = (int)tempFloat[0];
				gMessageParms.g1 = (int)tempFloat[1];
				gMessageParms.b1 = (int)tempFloat[2];
			}
		}
		else if( IsToken( pText, "fadein" ))
		{
			if( ParseFloats( pText, tempFloat, 1 ))
			{
				gMessageParms.fadein = tempFloat[0];
			}
		}
		else if( IsToken( pText, "fadeout" ))
		{
			if( ParseFloats( pText, tempFloat, 3 ))
			{
				gMessageParms.fadeout = tempFloat[0];
			}
		}
		else if( IsToken( pText, "holdtime" ))
		{
			if( ParseFloats( pText, tempFloat, 3 ))
			{
				gMessageParms.holdtime = tempFloat[0];
			}
		}
		else
		{
			Con_DPrintf( S_ERROR "unknown token: %s\n", pText );
		}
		return 1;
	}
	return 0;
}

void CL_TextMessageParse( byte *pMemFile, int fileSize )
{
	char			buf[512], trim[512], currentName[512];
	char			*pCurrentText = NULL, *pNameHeap;
	char			nameHeap[32768]; // g-cont. i will scale up heap to handle all TFC messages
	int			mode = MSGFILE_NAME; // searching for a message name
	int			lineNumber, filePos, lastLinePos;
	client_textmessage_t	textMessages[MAX_MESSAGES];
	int			i, nameHeapSize, textHeapSize, messageSize, nameOffset;
	int			messageCount, lastNamePos;
	size_t		textHeapSizeRemaining;

	lastNamePos = 0;
	lineNumber = 0;
	filePos = 0;
	lastLinePos = 0;
	messageCount = 0;

	while( COM_MemFgets( pMemFile, fileSize, &filePos, buf, 512 ) != NULL )
	{
		COM_TrimSpace( buf, trim );

		switch( mode )
		{
		case MSGFILE_NAME:
			// skip comment lines
			if( IsComment( trim ))
				break;

			// Is this a directive "$command"?, if so parse it and break
			if( ParseDirective( trim ))
				break;

			if( IsStartOfText( trim ))
			{
				mode = MSGFILE_TEXT;
				pCurrentText = (char*)(pMemFile + filePos);
				break;
			}

			if( IsEndOfText( trim ))
			{
				Con_Reportf( "%s: unexpected '}' found, line %d\n", __func__, lineNumber );
				return;
			}
			Q_strncpy( currentName, trim, sizeof( currentName ));
			break;
		case MSGFILE_TEXT:
			if( IsEndOfText( trim ))
			{
				int length = Q_strlen( currentName );

				// save name on name heap
				if( lastNamePos + length > 32768 )
				{
					Con_Reportf( "%s: error while parsing!\n", __func__ );
					return;
				}

				Q_strncpy( nameHeap + lastNamePos, currentName, sizeof( nameHeap ) - lastNamePos );

				// terminate text in-place in the memory file
				// (it's temporary memory that will be deleted)
				pMemFile[lastLinePos-1] = 0;

				// Save name/text on heap
				textMessages[messageCount] = gMessageParms;
				textMessages[messageCount].pName = nameHeap + lastNamePos;
				lastNamePos += length + 1;
				textMessages[messageCount].pMessage = pCurrentText;
				messageCount++;

				// reset parser to search for names
				mode = MSGFILE_NAME;
				break;
			}
			if( IsStartOfText( trim ))
			{
				Con_Reportf( "%s: unexpected '{' found, line %d\n", __func__, lineNumber );
				return;
			}
			break;
		}

		lineNumber++;
		lastLinePos = filePos;

		if( messageCount >= MAX_MESSAGES )
		{
			Con_Printf( S_WARN "Too many messages in titles.txt, max is %d\n", MAX_MESSAGES );
			break;
		}
	}

	Con_Reportf( "%s: parsed %d text messages\n", __func__, messageCount );
	nameHeapSize = lastNamePos;
	textHeapSize = 0;

	for( i = 0; i < messageCount; i++ )
		textHeapSize += Q_strlen( textMessages[i].pMessage ) + 1;
	messageSize = ( messageCount * sizeof( client_textmessage_t ));

	if(( textHeapSize + nameHeapSize + messageSize ) <= 0 )
	{
		clgame.titles = NULL;
		clgame.numTitles = 0;
		return;
	}

	// must malloc because we need to be able to clear it after initialization
	clgame.titles = (client_textmessage_t *)Mem_Calloc( cls.mempool, textHeapSize + nameHeapSize + messageSize );

	// copy table over
	memcpy( clgame.titles, textMessages, messageSize );

	// copy Name heap
	pNameHeap = ((char *)clgame.titles) + messageSize;
	memcpy( pNameHeap, nameHeap, nameHeapSize );
	//nameOffset = pNameHeap - clgame.titles[0].pName; //undefined on amd64


	// copy text & fixup pointers
	textHeapSizeRemaining = textHeapSize;
	pCurrentText = pNameHeap + nameHeapSize;

	for( i = 0; i < messageCount; i++ )
	{
		size_t currentTextSize = Q_strlen( clgame.titles[i].pMessage ) + 1;

		clgame.titles[i].pName = pNameHeap;			// adjust name pointer (parallel buffer)
		Q_strncpy( pCurrentText, clgame.titles[i].pMessage, textHeapSizeRemaining );	// copy text over
		clgame.titles[i].pMessage = pCurrentText;

		pNameHeap += Q_strlen( pNameHeap ) + 1;
		pCurrentText += currentTextSize;
		textHeapSizeRemaining -= currentTextSize;
	}

	if(( pCurrentText - (char *)clgame.titles ) != ( textHeapSize + nameHeapSize + messageSize ))
		Con_DPrintf( S_ERROR "%s: overflow text message buffer!\n", __func__ );

	clgame.numTitles = messageCount;
}
