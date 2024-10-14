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

static const char *const soundlst_groups[SoundList_Groups] =
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

static soundlst_t soundlst[SoundList_Groups];

static void SoundList_Print_f( void );
static void SoundList_Free( soundlst_t *lst )
{
	if( lst->snd )
		Mem_Free( lst->snd );

	memset( lst, 0, sizeof( *lst ));
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

	// Con_Printf( "%s: %s %d %d\n", __func__, soundlst_groups[group], count, idx );

	return SoundList_Get( group, idx );
}

static qboolean SoundList_ParseGroup( soundlst_t *lst, char **file )
{
	string token;
	int count = 0, slen = 0, i;
	char *p;

	p = *file;

	while(( p = COM_ParseFile( p, token, sizeof( token ))))
	{
		int len;

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

		len = Q_strlen( token ) + 1;
		if( slen < len )
			slen = len;

		count++;
	}

	if( count == 0 ) // deactivate group
	{
		lst->type = SoundList_None;
		lst->snd = NULL;
		lst->min = lst->max = 0;
		return true;
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
	int i = 0;

	lst->type = SoundList_Range;
	*file = COM_ParseFile( *file, snd, sizeof( snd ));

	// validate format string, count all % characters
	p = snd;
	i = 0;
	while(( p = Q_strchr( p, '%' )))
	{
		// only decimal
		if( p[1] != 'd' && p[1] != 'i' && p[1] != 'u' )
		{
			Con_Printf( "%s: invalid range string %s, only decimal numbers are allowed", __func__, snd );
			return false;
		}

		i++;
		p++;
	}

	// if more than one %, then it's an invalid format string
	if( i != 1 )
	{
		Con_Printf( "%s: invalid range string %s, only single specifier is allowed\n", __func__, snd );
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
	int i;

	while(( file = COM_ParseFile( file, token, sizeof( token ))))
	{
		soundlst_t *lst = NULL;
		char *p;

		for( i = 0; i < SoundList_Groups; i++ )
		{
			if( !Q_strcmp( token, soundlst_groups[i] ))
			{
				lst = &soundlst[i];
				break;
			}
		}

		if( !lst )
		{
			Con_Printf( "%s: unexpected token %s, must be group name\n", __func__, token );
			goto cleanup;
		}

		p = COM_ParseFile( file, token, sizeof( token ));

		// group is a range
		if( !Q_strcmp( token, "{" ))
		{
			file = p; // advance pointer

			if( !SoundList_ParseGroup( lst, &file ))
				goto cleanup;

			file = COM_ParseFile( file, token, sizeof( token ));
			if( Q_strcmp( token, "}" ))
			{
				Con_Printf( "%s: unexpected token %s, must be }\n", __func__, token );
				goto cleanup;
			}
		}
		else
		{
			// ranges are more simple, but need to rewind pointer for them
			if( !SoundList_ParseRange( lst, &file ))
				goto cleanup;
		}
	}

	if( host_developer.value >= 2 )
		SoundList_Print_f();

	return true;

cleanup:
	SoundList_Shutdown();
	return false;
}

static void SoundList_Print_f( void )
{
	int i;

	for( i = 0; i < SoundList_Groups; i++ )
	{
		soundlst_t *lst = &soundlst[i];

		switch( lst->type )
		{
		case SoundList_Range:
			Con_Reportf( "%-16s\t" S_CYAN "Range" S_DEFAULT " %s [%d; %d]\n",
				soundlst_groups[i], lst->snd, lst->min, lst->max );
			break;
		case SoundList_List:
		{
			int j;

			Con_Reportf( "%-16s\t" S_MAGENTA "List" S_DEFAULT " [", soundlst_groups[i] );
			for( j = 0; j < lst->max; j++ )
				Con_Reportf( "%s%s", &lst->snd[j * lst->min], j + 1 == lst->max ? "" : ", " );
			Con_Reportf( "]\n" );
			break;
		}
		default:
			Con_Reportf( "%-16s\t" S_RED "inactive" S_DEFAULT "\n", soundlst_groups[i] );
			break;
		}
	}
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
"Explode \"weapons/explode%d.wav\" 3 5\n"
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

static void SoundList_Reload_f( void )
{
	qboolean load_internal = false;
	char *pfile = FS_LoadFile( "scripts/sounds.lst", NULL, false );

	if( pfile )
	{
		if( !SoundList_Parse( pfile ))
		{
			Con_Printf( S_ERROR "can't parse sounds.lst file, loading internal...\n" );
			load_internal = true;
		}

		Mem_Free( pfile );
	}
	else load_internal = true;

	if( load_internal )
		SoundList_Parse( (char *)default_sounds_lst );
}

void SoundList_Init( void )
{
	Cmd_AddRestrictedCommand( "host_soundlist_print", SoundList_Print_f, "print current sound list" );
	Cmd_AddRestrictedCommand( "host_soundlist_reload", SoundList_Reload_f, "reload sound list" );

	SoundList_Reload_f ();
}
