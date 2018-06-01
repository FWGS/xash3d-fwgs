/*
base_cmd.c - command & cvar hashmap. Insipred by Doom III
Copyright (C) 2016 a1batross

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
#include "base_cmd.h"

// TODO: use another hash function, as COM_HashKey depends on string length
#define HASH_SIZE 128 // 128 * 4 * 4 == 2048 bytes
static base_command_hashmap_t *hashed_cmds[HASH_SIZE];

/*
============
BaseCmd_FindInBucket

Find base command in bucket
============
*/
base_command_hashmap_t *BaseCmd_FindInBucket( base_command_hashmap_t *bucket, base_command_type_e type, const char *name )
{
	base_command_hashmap_t *i = bucket;
	for( ; i && ( i->type != type || Q_stricmp( name, i->name ) ); // filter out
		 i = i->next );

	return i;
}

/*
============
BaseCmd_GetBucket

Get bucket which contain basecmd by given name
============
*/
base_command_hashmap_t *BaseCmd_GetBucket( const char *name )
{
	return hashed_cmds[ COM_HashKey( name, HASH_SIZE ) ];
}

/*
============
BaseCmd_Find

Find base command in hashmap
============
*/
base_command_t *BaseCmd_Find( base_command_type_e type, const char *name )
{
	base_command_hashmap_t *base = BaseCmd_GetBucket( name );
	base_command_hashmap_t *found = BaseCmd_FindInBucket( base, type, name );

	if( found )
		return found->basecmd;
	return NULL;
}

/*
============
BaseCmd_Find

Find every type of base command and write into arguments
============
*/
void BaseCmd_FindAll(const char *name, base_command_t **cmd, base_command_t **alias, base_command_t **cvar)
{
	base_command_hashmap_t *base = BaseCmd_GetBucket( name );
	base_command_hashmap_t *i = base;

	ASSERT( cmd && alias && cvar );

	*cmd = *alias = *cvar = NULL;

	for( ; i; i = i->next )
	{
		if( !Q_stricmp( i->name, name ) )
		{
			switch( i->type )
			{
			case HM_CMD:
				*cmd = i->basecmd;
				break;
			case HM_CMDALIAS:
				*alias = i->basecmd;
				break;
			case HM_CVAR:
				*cvar = i->basecmd;
				break;
			default: break;
			}
		}
	}

}

/*
============
BaseCmd_Insert

Add new typed base command to hashmap
============
*/
void BaseCmd_Insert( base_command_type_e type, base_command_t *basecmd, const char *name )
{
	uint hash = COM_HashKey( name, HASH_SIZE );
	base_command_hashmap_t *elem;

	elem = Z_Malloc( sizeof( base_command_hashmap_t ) );
	elem->basecmd = basecmd;
	elem->type = type;
	elem->name = name;
	elem->next = hashed_cmds[hash];
	hashed_cmds[hash] = elem;
}

/*
============
BaseCmd_Replace

Used in case, when basecmd has been registered, but gamedll wants to register it's own
============
*/
qboolean BaseCmd_Replace( base_command_type_e type, base_command_t *basecmd, const char *name )
{
	base_command_hashmap_t *i = BaseCmd_GetBucket( name );

	for( ; i && ( i->type != type || Q_stricmp( name, i->name ) ) ; // filter out
		 i = i->next );

	if( !i )
	{
		MsgDev( D_ERROR, "BaseCmd_Replace: couldn't find %s\n", name);
		return false;
	}

	i->basecmd = basecmd;
	i->name = name; // may be freed after

	return true;
}

/*
============
BaseCmd_Remove

Remove base command from hashmap
============
*/
void BaseCmd_Remove( base_command_type_e type, const char *name )
{
	uint hash = COM_HashKey( name, HASH_SIZE );
	base_command_hashmap_t *i, *prev;

	for( prev = NULL, i = hashed_cmds[hash]; i &&
		 ( Q_strcmp( i->name, name ) || i->type != type); // filter out
		 prev = i, i = i->next );

	if( !i )
	{
		MsgDev( D_ERROR, "Couldn't find %s in buckets\n", name );
		return;
	}

	if( prev )
		prev->next = i->next;
	else
		hashed_cmds[hash] = i->next;

	Z_Free( i );
}

/*
============
BaseCmd_Init

initialize base command hashmap system
============
*/
void BaseCmd_Init( void )
{
	memset( hashed_cmds, 0, sizeof( hashed_cmds ) );
}

/*
============
BaseCmd_Stats_f

============
*/
void BaseCmd_Stats_f( void )
{
	int i, minsize = 99999, maxsize = -1, empty = 0;

	for( i = 0; i < HASH_SIZE; i++ )
	{
		base_command_hashmap_t *hm;
		int len = 0;

		// count bucket length
		for( hm = hashed_cmds[i]; hm; hm = hm->next, len++ );

		if( len == 0 )
		{
			empty++;
			continue;
		}

		if( len < minsize )
			minsize = len;

		if( len > maxsize )
			maxsize = len;
	}

	Con_Printf( "Base command stats:\n");
	Con_Printf( "Bucket minimal length: %d\n", minsize );
	Con_Printf( "Bucket maximum length: %d\n", maxsize );
	Con_Printf( "Empty buckets: %d\n", empty );
}
