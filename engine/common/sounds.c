/*
sounds.c - sounds.lst parser
Copyright (C) 2024 Alibek Omarov

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

enum soundlst_type_e
{
	SoundList_None,
	SoundList_Range,
	SoundList_List
};

static const char *soundlst_groups[SoundList_Groups] =
{
	"BouncePlayerShell",
	"BounceWeaponShell",
	"BounceConcrete",
	"BounceGlass",
	"BounceMetal",
	"BounceFlesh",
	"BounceWood",
	"Ricochet",
	"Explode",
	"PlayerWaterEnter",
	"PlayerWaterExit",
	"EntityWaterEnter",
	"EntityWaterExit",
};

typedef struct soundlst_s
{
	enum soundlst_type_e type;
	char *snd;
	int min; // the string length if type is group
	int max; // the string count if type is group
} soundlst_t;

soundlst_t soundlst[SoundList_Groups];

static void SoundList_Free( soundlst_t *lst )
{
	if( lst->snd )
	{
		Mem_Free( lst->snd );
		lst->snd = NULL;
	}

	lst->min = lst->max = 0;
	lst->type = SoundList_None;
}

void SoundList_Shutdown( void )
{
	int i;

	for( i = 0; i < SoundList_Groups; i++ )
		SoundList_Free( &soundlst[i] );
}

int SoundList_Count( enum soundlst_group_e group )
{
	soundlst_t *lst = &soundlst[group];

	switch( lst->type )
	{
	case SoundList_Range:
		return lst->max - lst->min + 1;
	case SoundList_List:
		return lst->max;
	}

	return 0;
}

const char *SoundList_Get( enum soundlst_group_e group, int i )
{
	static string temp;
	soundlst_t *lst = &soundlst[group];

	if( i < 0 || i >= SoundList_Count( group ))
		return NULL;

	switch( lst->type )
	{
	case SoundList_Range:
		Q_snprintf( temp, sizeof( temp ), lst->snd, lst->min + i );
		return temp;
	case SoundList_List:
		return &lst->snd[i * lst->min];
	}

	return NULL;
}

const char *SoundList_GetRandom( enum soundlst_group_e group )
{
	int count = SoundList_Count( group );
	int idx = COM_RandomLong( 0, count - 1 );

	return SoundList_Get( group, idx );
}

static qboolean SoundList_ParseGroup( soundlst_t *lst, char **file )
{
	string token;
	int count = 0, slen = 0, i;
	char *p;

	p = *file;

	for( ; p && *p; count++, p = COM_ParseFile( p, token, sizeof( token )))
	{
		int len = Q_strlen( token ) + 1;
		if( slen < len )
			slen = len;

		if( !Q_strcmp( token, "}" ))
			break;

		if( !Q_strcmp( token, "{" ))
		{
			Con_Printf( "%s: expected '}' but got '{' during group list parse\n", __func__ );
			return false;
		}
		else if( !COM_CheckStringEmpty( token ))
		{
			Con_Printf( "%s: expected '}' but got EOF during group list parse\n", __func__ );
			return false;
		}
	}

	lst->type = SoundList_List;
	lst->min = slen;
	lst->max = count;
	lst->snd = Mem_Malloc( host.mempool, count * slen ); // allocate single buffer for the whole group

	for( i = 0; i < count; i++ )
	{
		*file = COM_ParseFile( *file, token, sizeof( token ));

		Q_strncpy( &lst->snd[i * slen], token, slen );
	}

	return true;
}

static qboolean SoundList_ParseRange( soundlst_t *lst, char **file )
{
	string token, snd;
	char *p;
	int i;

	lst->type = SoundList_Range;
	*file = COM_ParseFile( *file, snd, sizeof( snd ));

	// validate format string
	for( i = 0, p = snd; p; i++, p = Q_strchr( p, '%' ));
	if( i != 1 )
	{
		Con_Printf( "%s: invalid range string %s\n", __func__, snd );
		return false;
	}

	*file = COM_ParseFile( *file, token, sizeof( token ));
	if( !Q_isdigit( token ))
	{
		Con_Printf( "%s: %s must be a digit\n", __func__, token );
		return false;
	}
	lst->min = Q_atoi( token );

	*file = COM_ParseFile( *file, token, sizeof( token ));
	if( !Q_isdigit( token ))
	{
		Con_Printf( "%s: %s must be a digit\n", __func__, token );
		return false;
	}
	lst->max = Q_atoi( token );
	lst->snd = copystring( snd );

	return true;
}

static qboolean SoundList_Parse( char *file )
{
	string token;
	string name;
	int i;

	while( file && *file )
	{
		const char *group;
		soundlst_t *lst = NULL;
		file = COM_ParseFile( file, token, sizeof( token ));

		for( i = 0; i < SoundList_Groups; i++ )
		{
			if( !Q_strcmp( token, soundlst_groups[i] ))
				lst = &soundlst[i];
		}

		if( !lst )
		{
			Con_Printf( "%s: unexpected token %s, must be group name\n", __func__, token );
			goto cleanup;
		}

		file = COM_ParseFile( file, token, sizeof( token ));

		// group is a range
		if( Q_strcmp( token, "{" ))
		{
			if( !SoundList_ParseRange( lst, &file ))
				goto cleanup;
		}
		else
		{
			if( !SoundList_ParseGroup( lst, &file ))
				goto cleanup;
		}
	}

	return true;

cleanup:
	SoundList_Shutdown();
	return false;
}

// I wish we had #embed already
static const char default_sounds_lst[] =
"BouncePlayerShell \"player/pl_shell%d.wav\" 1 3\n"
"BounceWeaponShell \"weapons/sshell%d.wav\" 1 3\n"
"BounceConcrete \"debris/concrete%d.wav\" 1 3\n"
"BounceGlass \"debris/glass%d.wav\" 1 4\n"
"BounceMetal \"debris/metal%d.wav\" 1 6\n"
"BounceFlesh \"debris/flesh%d.wav\" 1 7\n"
"BounceWood \"debris/wood%d.wav\" 1 4\n"
"Ricochet \"weapons/ric%d.wav\" 1 5\n"
"Explode \"weapons/explode%d\" 3 5\n"
"EntityWaterEnter \"player/pl_wade%d.wav\" 1 4\n"
"EntityWaterExit \"player/pl_wade%d.wav\" 1 4\n"
"PlayerWaterEnter\n"
"{\n"
"	\"player/pl_wade1.wav\"\n"
"}\n"
"\n"
"PlayerWaterExit\n"
"{\n"
"	\"player/pl_wade2.wav\"\n"
"}\n";

void SoundList_Init( void )
{
	char *pfile;

	pfile = FS_LoadFile( "scripts/sounds.lst", NULL, false );

	if( !pfile || !SoundList_Parse( pfile ))
		SoundList_Parse( (char *)default_sounds_lst );
}
