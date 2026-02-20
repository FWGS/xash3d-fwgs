/*
net_encode.c - encode network messages
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
#include "netchan.h"
#include "xash3d_mathlib.h"
#include "net_encode.h"
#include "event_api.h"
#include "usercmd.h"
#include "pm_movevars.h"
#include "entity_state.h"
#include "weaponinfo.h"
#include "event_args.h"
#include "protocol.h"
#include "client.h"

#define DELTA_PATH		"delta.lst"

#define DT_BYTE		BIT( 0 )	// A byte
#define DT_SHORT		BIT( 1 ) 	// 2 byte field
#define DT_FLOAT		BIT( 2 )	// A floating point field
#define DT_INTEGER		BIT( 3 )	// 4 byte integer
#define DT_ANGLE		BIT( 4 )	// A floating point angle ( will get masked correctly )
#define DT_TIMEWINDOW_8	BIT( 5 )	// A floating point timestamp, relative to sv.time
#define DT_TIMEWINDOW_BIG	BIT( 6 )	// and re-encoded on the client relative to the client's clock
#define DT_STRING		BIT( 7 )	// A null terminated string, sent as 8 byte chars
#define DT_SIGNED		BIT( 8 )	// sign modificator
#define DT_SIGNED_GS	BIT( 31 ) // GoldSrc-specific sign modificator

// helper macroses
#define ENTS_DEF( x )	#x, offsetof( entity_state_t, x ), sizeof( ((entity_state_t *)0)->x )
#define UCMD_DEF( x )	#x, offsetof( usercmd_t, x ), sizeof( ((usercmd_t *)0)->x )
#define EVNT_DEF( x )	#x, offsetof( event_args_t, x ), sizeof( ((event_args_t *)0)->x )
#define PHYS_DEF( x )	#x, offsetof( movevars_t, x ), sizeof( ((movevars_t *)0)->x )
#define CLDT_DEF( x )	#x, offsetof( clientdata_t, x ), sizeof( ((clientdata_t *)0)->x )
#define WPDT_DEF( x )	#x, offsetof( weapon_data_t, x ), sizeof( ((weapon_data_t *)0)->x )
#define DESC_DEF( x )	#x, offsetof( goldsrc_delta_t, x ), sizeof( ((goldsrc_delta_t *)0)->x )

static qboolean		delta_init = false;

// list of all the struct names
static const delta_field_t cmd_fields[] =
{
{ UCMD_DEF( lerp_msec )		},
{ UCMD_DEF( msec )			},
{ UCMD_DEF( viewangles[0] )		},
{ UCMD_DEF( viewangles[1] )		},
{ UCMD_DEF( viewangles[2] )		},
{ UCMD_DEF( forwardmove )		},
{ UCMD_DEF( sidemove )		},
{ UCMD_DEF( upmove )		},
{ UCMD_DEF( lightlevel )		},
{ UCMD_DEF( buttons )		},
{ UCMD_DEF( impulse )		},
{ UCMD_DEF( weaponselect )		},
{ UCMD_DEF( impact_index )		},
{ UCMD_DEF( impact_position[0] )	},
{ UCMD_DEF( impact_position[1] )	},
{ UCMD_DEF( impact_position[2] )	},
};

static const delta_field_t pm_fields[] =
{
{ PHYS_DEF( gravity )		},
{ PHYS_DEF( stopspeed )		},
{ PHYS_DEF( maxspeed )		},
{ PHYS_DEF( spectatormaxspeed )	},
{ PHYS_DEF( accelerate )		},
{ PHYS_DEF( airaccelerate )		},
{ PHYS_DEF( wateraccelerate )		},
{ PHYS_DEF( friction )		},
{ PHYS_DEF( edgefriction )		},
{ PHYS_DEF( waterfriction )		},
{ PHYS_DEF( bounce )		},
{ PHYS_DEF( stepsize )		},
{ PHYS_DEF( maxvelocity )		},
{ PHYS_DEF( zmax )			},
{ PHYS_DEF( waveHeight )		},
{ PHYS_DEF( footsteps )		},
{ PHYS_DEF( skyName )		},
{ PHYS_DEF( rollangle )		},
{ PHYS_DEF( rollspeed )		},
{ PHYS_DEF( skycolor_r )		},
{ PHYS_DEF( skycolor_g )		},
{ PHYS_DEF( skycolor_b )		},
{ PHYS_DEF( skyvec_x )		},
{ PHYS_DEF( skyvec_y )		},
{ PHYS_DEF( skyvec_z )		},
{ PHYS_DEF( fog_settings )		},
{ PHYS_DEF( wateralpha )		},
{ PHYS_DEF( skydir_x )		},
{ PHYS_DEF( skydir_y )		},
{ PHYS_DEF( skydir_z )		},
{ PHYS_DEF( skyangle )		},
};

static const delta_field_t ev_fields[] =
{
{ EVNT_DEF( flags )		},
{ EVNT_DEF( entindex )	},
{ EVNT_DEF( origin[0] )	},
{ EVNT_DEF( origin[1] )	},
{ EVNT_DEF( origin[2] )	},
{ EVNT_DEF( angles[0] )	},
{ EVNT_DEF( angles[1] )	},
{ EVNT_DEF( angles[2] )	},
{ EVNT_DEF( velocity[0] )	},
{ EVNT_DEF( velocity[1] )	},
{ EVNT_DEF( velocity[2] )	},
{ EVNT_DEF( ducking )	},
{ EVNT_DEF( fparam1 )	},
{ EVNT_DEF( fparam2 )	},
{ EVNT_DEF( iparam1 )	},
{ EVNT_DEF( iparam2 )	},
{ EVNT_DEF( bparam1 )	},
{ EVNT_DEF( bparam2 )	},
};

static const delta_field_t wd_fields[] =
{
{ WPDT_DEF( m_iId )			},
{ WPDT_DEF( m_iClip )		},
{ WPDT_DEF( m_flNextPrimaryAttack )	},
{ WPDT_DEF( m_flNextSecondaryAttack )	},
{ WPDT_DEF( m_flTimeWeaponIdle )	},
{ WPDT_DEF( m_fInReload )		},
{ WPDT_DEF( m_fInSpecialReload )	},
{ WPDT_DEF( m_flNextReload )		},
{ WPDT_DEF( m_flPumpTime )		},
{ WPDT_DEF( m_fReloadTime )		},
{ WPDT_DEF( m_fAimedDamage )		},
{ WPDT_DEF( m_fNextAimBonus )		},
{ WPDT_DEF( m_fInZoom )		},
{ WPDT_DEF( m_iWeaponState )		},
{ WPDT_DEF( iuser1 )		},
{ WPDT_DEF( iuser2 )		},
{ WPDT_DEF( iuser3 )		},
{ WPDT_DEF( iuser4 )		},
{ WPDT_DEF( fuser1 )		},
{ WPDT_DEF( fuser2 )		},
{ WPDT_DEF( fuser3 )		},
{ WPDT_DEF( fuser4 )		},
};

static const delta_field_t cd_fields[] =
{
{ CLDT_DEF( origin[0] )	},
{ CLDT_DEF( origin[1] )	},
{ CLDT_DEF( origin[2] )	},
{ CLDT_DEF( velocity[0] )	},
{ CLDT_DEF( velocity[1] )	},
{ CLDT_DEF( velocity[2] )	},
{ CLDT_DEF( viewmodel )	},
{ CLDT_DEF( punchangle[0] )	},
{ CLDT_DEF( punchangle[1] )	},
{ CLDT_DEF( punchangle[2] )	},
{ CLDT_DEF( flags )		},
{ CLDT_DEF( waterlevel )	},
{ CLDT_DEF( watertype )	},
{ CLDT_DEF( view_ofs[0] )	},
{ CLDT_DEF( view_ofs[1] )	},
{ CLDT_DEF( view_ofs[2] )	},
{ CLDT_DEF( health )	},
{ CLDT_DEF( bInDuck )	},
{ CLDT_DEF( weapons )	},
{ CLDT_DEF( flTimeStepSound )	},
{ CLDT_DEF( flDuckTime )	},
{ CLDT_DEF( flSwimTime )	},
{ CLDT_DEF( waterjumptime )	},
{ CLDT_DEF( maxspeed )	},
{ CLDT_DEF( fov )		},
{ CLDT_DEF( weaponanim )	},
{ CLDT_DEF( m_iId )		},
{ CLDT_DEF( ammo_shells )	},
{ CLDT_DEF( ammo_nails )	},
{ CLDT_DEF( ammo_cells )	},
{ CLDT_DEF( ammo_rockets )	},
{ CLDT_DEF( m_flNextAttack )	},
{ CLDT_DEF( tfstate )	},
{ CLDT_DEF( pushmsec )	},
{ CLDT_DEF( deadflag )	},
{ CLDT_DEF( physinfo )	},
{ CLDT_DEF( iuser1 )	},
{ CLDT_DEF( iuser2 )	},
{ CLDT_DEF( iuser3 )	},
{ CLDT_DEF( iuser4 )	},
{ CLDT_DEF( fuser1 )	},
{ CLDT_DEF( fuser2 )	},
{ CLDT_DEF( fuser3 )	},
{ CLDT_DEF( fuser4 )	},
{ CLDT_DEF( vuser1[0] )	},
{ CLDT_DEF( vuser1[1] )	},
{ CLDT_DEF( vuser1[2] )	},
{ CLDT_DEF( vuser2[0] )	},
{ CLDT_DEF( vuser2[1] )	},
{ CLDT_DEF( vuser2[2] )	},
{ CLDT_DEF( vuser3[0] )	},
{ CLDT_DEF( vuser3[1] )	},
{ CLDT_DEF( vuser3[2] )	},
{ CLDT_DEF( vuser4[0] )	},
{ CLDT_DEF( vuser4[1] )	},
{ CLDT_DEF( vuser4[2] )	},
};

static const delta_field_t ent_fields[] =
{
{ ENTS_DEF( entityType )	},
{ ENTS_DEF( origin[0] )	},
{ ENTS_DEF( origin[1] )	},
{ ENTS_DEF( origin[2] )	},
{ ENTS_DEF( angles[0] )	},
{ ENTS_DEF( angles[1] )	},
{ ENTS_DEF( angles[2] )	},
{ ENTS_DEF( modelindex )	},
{ ENTS_DEF( sequence )	},
{ ENTS_DEF( frame )		},
{ ENTS_DEF( colormap )	},
{ ENTS_DEF( skin )		},
{ ENTS_DEF( solid )		},
{ ENTS_DEF( effects )	},
{ ENTS_DEF( scale )		},
{ ENTS_DEF( eflags )	},
{ ENTS_DEF( rendermode )	},
{ ENTS_DEF( renderamt )	},
{ ENTS_DEF( rendercolor.r )	},
{ ENTS_DEF( rendercolor.g )	},
{ ENTS_DEF( rendercolor.b )	},
{ ENTS_DEF( renderfx )	},
{ ENTS_DEF( movetype )	},
{ ENTS_DEF( animtime )	},
{ ENTS_DEF( framerate )	},
{ ENTS_DEF( body )		},
{ ENTS_DEF( controller[0] )	},
{ ENTS_DEF( controller[1] )	},
{ ENTS_DEF( controller[2] )	},
{ ENTS_DEF( controller[3] )	},
{ ENTS_DEF( blending[0] )	},
{ ENTS_DEF( blending[1] )	},
{ ENTS_DEF( blending[2] )	},
{ ENTS_DEF( blending[3] )	},
{ ENTS_DEF( velocity[0] )	},
{ ENTS_DEF( velocity[1] )	},
{ ENTS_DEF( velocity[2] )	},
{ ENTS_DEF( mins[0] )	},
{ ENTS_DEF( mins[1] )	},
{ ENTS_DEF( mins[2] )	},
{ ENTS_DEF( maxs[0] )	},
{ ENTS_DEF( maxs[1] )	},
{ ENTS_DEF( maxs[2] )	},
{ ENTS_DEF( aiment )	},
{ ENTS_DEF( owner )		},
{ ENTS_DEF( friction )	},
{ ENTS_DEF( gravity )	},
{ ENTS_DEF( team )		},
{ ENTS_DEF( playerclass )	},
{ ENTS_DEF( health )	},
{ ENTS_DEF( spectator )	},
{ ENTS_DEF( weaponmodel )	},
{ ENTS_DEF( gaitsequence )	},
{ ENTS_DEF( basevelocity[0] )	},
{ ENTS_DEF( basevelocity[1] )	},
{ ENTS_DEF( basevelocity[2] )	},
{ ENTS_DEF( usehull )	},
{ ENTS_DEF( oldbuttons )	},	// probably never transmitted
{ ENTS_DEF( onground )	},
{ ENTS_DEF( iStepLeft )	},
{ ENTS_DEF( flFallVelocity )	},
{ ENTS_DEF( fov )		},
{ ENTS_DEF( weaponanim )	},
{ ENTS_DEF( startpos[0] )	},
{ ENTS_DEF( startpos[1] )	},
{ ENTS_DEF( startpos[2] )	},
{ ENTS_DEF( endpos[0] )	},
{ ENTS_DEF( endpos[1] )	},
{ ENTS_DEF( endpos[2] )	},
{ ENTS_DEF( impacttime )	},
{ ENTS_DEF( starttime )	},
{ ENTS_DEF( iuser1 )	},
{ ENTS_DEF( iuser2 )	},
{ ENTS_DEF( iuser3 )	},
{ ENTS_DEF( iuser4 )	},
{ ENTS_DEF( fuser1 )	},
{ ENTS_DEF( fuser2 )	},
{ ENTS_DEF( fuser3 )	},
{ ENTS_DEF( fuser4 )	},
{ ENTS_DEF( vuser1[0] )	},
{ ENTS_DEF( vuser1[1] )	},
{ ENTS_DEF( vuser1[2] )	},
{ ENTS_DEF( vuser2[0] )	},
{ ENTS_DEF( vuser2[1] )	},
{ ENTS_DEF( vuser2[2] )	},
{ ENTS_DEF( vuser3[0] )	},
{ ENTS_DEF( vuser3[1] )	},
{ ENTS_DEF( vuser3[2] )	},
{ ENTS_DEF( vuser4[0] )	},
{ ENTS_DEF( vuser4[1] )	},
{ ENTS_DEF( vuser4[2] )	},
};

static const delta_field_t meta_fields[] =
{
{ DESC_DEF( fieldType ), },
{ DESC_DEF( fieldName ), },
{ DESC_DEF( fieldOffset ), },
{ DESC_DEF( fieldSize ), },
{ DESC_DEF( significant_bits ), },
{ DESC_DEF( premultiply ), },
{ DESC_DEF( postmultiply ), },
};

#if XASH_ENGINE_TESTS
typedef struct delta_test_struct_t
{
	char     dt_string[128];    // always signed
	float    dt_timewindow_big; // always signed
	float    dt_timewindow_8;   // always signed
	float	 dt_angle;          // always_signed
	float    dt_float_signed;
	float    dt_float_unsigned;
	int32_t  dt_integer_signed;
	uint32_t dt_integer_unsigned;
	int16_t  dt_short_signed;
	uint16_t dt_short_unsigned;
	int8_t   dt_byte_signed;
	uint8_t  dt_byte_unsigned;
} delta_test_struct_t;

#define TEST_DEF( x )	#x, offsetof( delta_test_struct_t, x ), sizeof( ((delta_test_struct_t *)0)->x )

static const delta_field_t test_fields[] =
{
{ TEST_DEF( dt_string ) },
{ TEST_DEF( dt_timewindow_big )},
{ TEST_DEF( dt_timewindow_8 )},
{ TEST_DEF( dt_angle ) },
{ TEST_DEF( dt_float_signed ) },
{ TEST_DEF( dt_float_unsigned ) },
{ TEST_DEF( dt_integer_signed ) },
{ TEST_DEF( dt_integer_unsigned ) },
{ TEST_DEF( dt_short_signed ) },
{ TEST_DEF( dt_short_unsigned ) },
{ TEST_DEF( dt_byte_signed ) },
{ TEST_DEF( dt_byte_unsigned ) },
};
#endif

static delta_info_t dt_info[] =
{
[DT_EVENT_T]               = { "event_t", ev_fields, ARRAYSIZE( ev_fields ) },
[DT_MOVEVARS_T]            = { "movevars_t", pm_fields, ARRAYSIZE( pm_fields ) },
[DT_USERCMD_T]             = { "usercmd_t", cmd_fields, ARRAYSIZE( cmd_fields ) },
[DT_CLIENTDATA_T]          = { "clientdata_t", cd_fields, ARRAYSIZE( cd_fields ) },
[DT_WEAPONDATA_T]          = { "weapon_data_t", wd_fields, ARRAYSIZE( wd_fields ) },
[DT_ENTITY_STATE_T]        = { "entity_state_t", ent_fields, ARRAYSIZE( ent_fields ) },
[DT_ENTITY_STATE_PLAYER_T] = { "entity_state_player_t", ent_fields, ARRAYSIZE( ent_fields ) },
[DT_CUSTOM_ENTITY_STATE_T] = { "custom_entity_state_t", ent_fields, ARRAYSIZE( ent_fields ) },
#if XASH_ENGINE_TESTS
[DT_DELTA_TEST_STRUCT_T]   = { "delta_test_struct_t", test_fields, ARRAYSIZE( test_fields ) },
#endif
};

// meta description is special, it cannot be overriden
static const delta_info_t dt_goldsrc_meta =
{
	.pName = "goldsrc_delta_t",
	.pInfo = meta_fields,
	.maxFields = ARRAYSIZE( meta_fields ),
	.numFields = ARRAYSIZE( meta_fields ),
	.pFields = (delta_t[ARRAYSIZE( meta_fields )])
	{
		{
			DESC_DEF( fieldType ),
			.flags = DT_INTEGER,
			.multiplier = 1.0f,
			.post_multiplier = 1.0f,
			.bits = 32,
		},
		{
			DESC_DEF( fieldName ),
			.flags = DT_STRING,
			.multiplier = 1.0f,
			.post_multiplier = 1.0f,
			.bits = 1,
		},
		{
			DESC_DEF( fieldOffset ),
			.flags = DT_INTEGER,
			.multiplier = 1.0f,
			.post_multiplier = 1.0f,
			.bits = 16,
		},
		{
			DESC_DEF( fieldSize ),
			.flags = DT_INTEGER,
			.multiplier = 1.0f,
			.post_multiplier = 1.0f,
			.bits = 8,
		},
		{
			DESC_DEF( significant_bits ),
			.flags = DT_INTEGER,
			.multiplier = 1.0f,
			.post_multiplier = 1.0f,
			.bits = 8,
		},
		{
			DESC_DEF( premultiply ),
			.flags = DT_FLOAT,
			.multiplier = 4000.0f,
			.post_multiplier = 1.0f,
			.bits = 32,
		},
		{
			DESC_DEF( postmultiply ),
			.flags = DT_FLOAT,
			.multiplier = 4000.0f,
			.post_multiplier = 1.0f,
			.bits = 32,
		},
	},
	.bInitialized = true
};

static delta_info_t *Delta_FindStruct( const char *name )
{
	int	i;

	if( COM_StringEmptyOrNULL( name ))
		return NULL;

	for( i = 0; i < ARRAYSIZE( dt_info ); i++ )
	{
		if( !Q_stricmp( dt_info[i].pName, name ))
			return &dt_info[i];
	}

	Con_DPrintf( S_WARN "Struct %s not found in delta_info\n", name );

	// found nothing
	return NULL;
}

static int Delta_NumTables( void )
{
	return ARRAYSIZE( dt_info );
}

static delta_info_t *Delta_FindStructByIndex( int index )
{
	return &dt_info[index];
}

static delta_info_t *Delta_FindStructByEncoder( const char *encoderName )
{
	int	i;

	if( COM_StringEmptyOrNULL( encoderName ) )
		return NULL;

	for( i = 0; i < ARRAYSIZE( dt_info ); i++ )
	{
		if( !Q_stricmp( dt_info[i].funcName, encoderName ))
			return &dt_info[i];
	}
	// found nothing
	return NULL;
}

static delta_info_t *Delta_FindStructByDelta( const delta_t *pFields )
{
	int	i;

	if( !pFields ) return NULL;

	for( i = 0; i < ARRAYSIZE( dt_info ); i++ )
	{
		if( dt_info[i].pFields == pFields )
			return &dt_info[i];
	}
	// found nothing
	return NULL;
}

static void Delta_CustomEncode( delta_info_t *dt, const void *from, const void *to )
{
	int	i;

	Assert( dt != NULL );

	// set all fields is active by default
	for( i = 0; i < dt->numFields; i++ )
		dt->pFields[i].bInactive = false;

	if( dt->userCallback )
		dt->userCallback( dt->pFields, from, to );
}

static const delta_field_t *Delta_FindFieldInfo( const delta_field_t *pInfo, const char *fieldName, int maxFields )
{
	int i;

	if( !fieldName || !*fieldName )
		return NULL;

	for( i = 0; i < maxFields; i++ )
	{
		if( !Q_strcmp( pInfo[i].name, fieldName ))
			return &pInfo[i];
	}

	return NULL;
}

static int Delta_IndexForFieldInfo( const delta_field_t *pInfo, const char *fieldName, int maxFields )
{
	int	i;

	if( !fieldName || !*fieldName )
		return -1;

	for( i = 0; i < maxFields; i++ )
	{
		if( !Q_strcmp( pInfo[i].name, fieldName ))
			return i;
	}
	return -1;
}

static qboolean Delta_AddField( delta_info_t *dt, const char *pName, int flags, int bits, float mul, float post_mul )
{
	const delta_field_t *pFieldInfo;
	delta_t		*pField;
	int		i;

	// check for coexisting field
	for( i = 0, pField = dt->pFields; i < dt->numFields && pField; i++, pField++ )
	{
		if( !Q_strcmp( pField->name, pName ))
		{
			// update existed field
			pField->flags = flags;
			pField->bits = bits;
			pField->multiplier = mul;
			pField->post_multiplier = post_mul;
			return true;
		}
	}

	// find field description
	pFieldInfo = Delta_FindFieldInfo( dt->pInfo, pName, dt->maxFields );
	if( !pFieldInfo )
	{
		Con_DPrintf( S_ERROR "%s: couldn't find description for %s->%s\n", __func__, dt->pName, pName );
		return false;
	}

	if( dt->numFields + 1 > dt->maxFields )
	{
		Con_DPrintf( S_WARN "%s: can't add %s->%s encoder list is full\n", __func__, dt->pName, pName );
		return false; // too many fields specified (duplicated ?)
	}

	// allocate a new one
	dt->pFields = Z_Realloc( dt->pFields, (dt->numFields + 1) * sizeof( delta_t ));
	for( i = 0, pField = dt->pFields; i < dt->numFields; i++, pField++ );

	// copy info to new field
	pField->name = pFieldInfo->name;
	pField->offset = pFieldInfo->offset;
	pField->size = pFieldInfo->size;
	pField->flags = flags;
	pField->bits = bits;
	pField->multiplier = mul;
	pField->post_multiplier = post_mul;
	dt->numFields++;

	return true;
}

static void Delta_WriteTableField( sizebuf_t *msg, int tableIndex, const delta_t *pField )
{
	int	nameIndex;
	delta_info_t *dt;

	Assert( pField != NULL );

	if( COM_StringEmptyOrNULL( pField->name ))
		return;// not initialized ?

	dt = Delta_FindStructByIndex( tableIndex );
	Assert( dt && dt->bInitialized );

	nameIndex = Delta_IndexForFieldInfo( dt->pInfo, pField->name, dt->maxFields );
	Assert( nameIndex >= 0 && nameIndex < dt->maxFields );

	MSG_BeginServerCmd( msg, svc_deltatable );
	MSG_WriteUBitLong( msg, tableIndex, 4 ); // assume we support 16 network tables
	MSG_WriteUBitLong( msg, nameIndex, 8 ); // 255 fields by struct should be enough
	MSG_WriteUBitLong( msg, pField->flags, 10 ); // flags are indicated various input types
	MSG_WriteUBitLong( msg, pField->bits - 1, 5 ); // max received value is 32 (32 bit)

	// multipliers is null-compressed
	if( !Q_equal( pField->multiplier, 1.0f ))
	{
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteFloat( msg, pField->multiplier );
	}
	else MSG_WriteOneBit( msg, 0 );

	if( !Q_equal( pField->post_multiplier, 1.0f ))
	{
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteFloat( msg, pField->post_multiplier );
	}
	else MSG_WriteOneBit( msg, 0 );
}

void Delta_ParseTableField( sizebuf_t *msg )
{
	int		tableIndex, nameIndex;
	float		mul = 1.0f, post_mul = 1.0f;
	int		flags, bits;
	const char	*pName;
	qboolean ignore = false;
	delta_info_t	*dt;

	tableIndex = MSG_ReadUBitLong( msg, 4 );
	dt = Delta_FindStructByIndex( tableIndex );
	if( !dt )
		Host_Error( "%s: not initialized", __func__ );

	nameIndex = MSG_ReadUBitLong( msg, 8 );	// read field name index
	if( ( nameIndex >= 0 && nameIndex < dt->maxFields ) )
	{
		pName = dt->pInfo[nameIndex].name;
	}
	else
	{
		ignore = true;
		Con_Reportf( "%s: wrong nameIndex %d for table %s, ignoring\n", __func__, nameIndex,  dt->pName );
	}

	flags = MSG_ReadUBitLong( msg, 10 );
	bits = MSG_ReadUBitLong( msg, 5 ) + 1;

	// read the multipliers
	if( MSG_ReadOneBit( msg ))
		mul = MSG_ReadFloat( msg );

	if( MSG_ReadOneBit( msg ))
		post_mul = MSG_ReadFloat( msg );

	if( ignore )
		return;

	// delta encoders it's already initialized on this machine (local game)
	if( delta_init )
		Delta_Shutdown();

	// add field to table
	Delta_AddField( dt, pName, flags, bits, mul, post_mul );
}

static qboolean Delta_ParseField( char **delta_script, const delta_info_t *dt, delta_t *pField, qboolean bPost )
{
	const delta_field_t *pFieldInfo;
	string		token;
	char		*oldpos;

	*delta_script = COM_ParseFile( *delta_script, token, sizeof( token ));
	if( Q_strcmp( token, "(" ))
	{
		Con_DPrintf( S_ERROR "%s: expected '(', found '%s' instead\n", __func__, token );
		return false;
	}

	// read the variable name
	if(( *delta_script = COM_ParseFile( *delta_script, token, sizeof( token ))) == NULL )
	{
		Con_DPrintf( S_ERROR "%s: missing field name\n", __func__ );
		return false;
	}

	pFieldInfo = Delta_FindFieldInfo( dt->pInfo, token, dt->maxFields );
	if( !pFieldInfo )
	{
		Con_DPrintf( S_ERROR "%s: unable to find field %s\n", __func__, token );
		return false;
	}

	*delta_script = COM_ParseFile( *delta_script, token, sizeof( token ));
	if( Q_strcmp( token, "," ))
	{
		Con_DPrintf( S_ERROR "%s: expected ',', found '%s' instead\n", __func__, token );
		return false;
	}

	// copy base info to new field
	pField->name = pFieldInfo->name;
	pField->offset = pFieldInfo->offset;
	pField->size = pFieldInfo->size;
	pField->flags = 0;

	// read delta-flags
	while(( *delta_script = COM_ParseFile( *delta_script, token, sizeof( token ))) != NULL )
	{
		if( !Q_strcmp( token, "," ))
			break;	// end of flags argument

		if( !Q_strcmp( token, "|" ))
			continue;

		if( !Q_strcmp( token, "DT_BYTE" ))
			pField->flags |= DT_BYTE;
		else if( !Q_strcmp( token, "DT_SHORT" ))
			pField->flags |= DT_SHORT;
		else if( !Q_strcmp( token, "DT_FLOAT" ))
			pField->flags |= DT_FLOAT;
		else if( !Q_strcmp( token, "DT_INTEGER" ))
			pField->flags |= DT_INTEGER;
		else if( !Q_strcmp( token, "DT_ANGLE" ))
			pField->flags |= DT_ANGLE;
		else if( !Q_strcmp( token, "DT_TIMEWINDOW_8" ))
			pField->flags |= DT_TIMEWINDOW_8;
		else if( !Q_strcmp( token, "DT_TIMEWINDOW_BIG" ))
			pField->flags |= DT_TIMEWINDOW_BIG;
		else if( !Q_strcmp( token, "DT_STRING" ))
			pField->flags |= DT_STRING;
		else if( !Q_strcmp( token, "DT_SIGNED" ))
			pField->flags |= DT_SIGNED;
	}

	if( Q_strcmp( token, "," ))
	{
		Con_DPrintf( S_ERROR "%s: expected ',', found '%s' instead\n", __func__, token );
		return false;
	}

	// read delta-bits
	if(( *delta_script = COM_ParseFile( *delta_script, token, sizeof( token ))) == NULL )
	{
		Con_DPrintf( S_ERROR "%s: %s field bits argument is missing\n", __func__, pField->name );
		return false;
	}

	pField->bits = Q_atoi( token );

	*delta_script = COM_ParseFile( *delta_script, token, sizeof( token ));
	if( Q_strcmp( token, "," ))
	{
		Con_DPrintf( S_ERROR "%s: expected ',', found '%s' instead\n", __func__, token );
		return false;
	}

	// read delta-multiplier
	if(( *delta_script = COM_ParseFile( *delta_script, token, sizeof( token ))) == NULL )
	{
		Con_DPrintf( S_ERROR "%s: %s missing 'multiplier' argument\n", __func__, pField->name );
		return false;
	}

	pField->multiplier = Q_atof( token );

	if( bPost )
	{
		*delta_script = COM_ParseFile( *delta_script, token, sizeof( token ));
		if( Q_strcmp( token, "," ))
		{
			Con_DPrintf( S_ERROR "%s: expected ',', found '%s' instead\n", __func__, token );
			return false;
		}

		// read delta-postmultiplier
		if(( *delta_script = COM_ParseFile( *delta_script, token, sizeof( token ))) == NULL )
		{
			Con_DPrintf( S_ERROR "%s: %s missing 'post_multiply' argument\n", __func__, pField->name );
			return false;
		}

		pField->post_multiplier = Q_atof( token );
	}
	else
	{
		// to avoid division by zero
		pField->post_multiplier = 1.0f;
	}

	// closing brace...
	*delta_script = COM_ParseFile( *delta_script, token, sizeof( token ));
	if( Q_strcmp( token, ")" ))
	{
		Con_DPrintf( S_ERROR "%s: expected ')', found '%s' instead\n", __func__, token );
		return false;
	}

	// ... and trying to parse optional ',' post-symbol
	oldpos = *delta_script;
	*delta_script = COM_ParseFile( *delta_script, token, sizeof( token ));
	if( token[0] != ',' ) *delta_script = oldpos; // not a ','

	return true;
}

static void Delta_ParseTable( char **delta_script, delta_info_t *dt, const char *encodeDll, const char *encodeFunc )
{
	string		token;
	delta_t		*pField;

	// allocate the delta-structures
	if( !dt->pFields ) dt->pFields = (delta_t *)Z_Calloc( dt->maxFields * sizeof( delta_t ));

	pField = dt->pFields;
	dt->numFields = 0;

	// assume we have handled '{'
	while(( *delta_script = COM_ParseFile( *delta_script, token, sizeof( token ))) != NULL )
	{
		Assert( dt->numFields <= dt->maxFields );

		if( !Q_strcmp( token, "DEFINE_DELTA" ))
		{
			if( Delta_ParseField( delta_script, dt, &pField[dt->numFields], false ))
				dt->numFields++;
		}
		else if( !Q_strcmp( token, "DEFINE_DELTA_POST" ))
		{
			if( Delta_ParseField( delta_script, dt, &pField[dt->numFields], true ))
				dt->numFields++;
		}
		else if( token[0] == '}' )
		{
			// end of the section
			break;
		}
	}

	// copy function name
	Q_strncpy( dt->funcName, encodeFunc, sizeof( dt->funcName ));

	if( !Q_stricmp( encodeDll, "none" ))
		dt->customEncode = CUSTOM_NONE;
	else if( !Q_stricmp( encodeDll, "gamedll" ))
		dt->customEncode = CUSTOM_SERVER_ENCODE;
	else if( !Q_stricmp( encodeDll, "clientdll" ))
		dt->customEncode = CUSTOM_CLIENT_ENCODE;

	// adjust to fit memory size
	if( dt->numFields < dt->maxFields )
	{
		dt->pFields = Z_Realloc( dt->pFields, dt->numFields * sizeof( delta_t ));
	}

	dt->bInitialized = true; // table is ok
}

static void Delta_InitFields( void )
{
	byte *afile;
	char *pfile;
	string		encodeDll, encodeFunc, token;
	delta_info_t	*dt;

	afile = FS_LoadFile( DELTA_PATH, NULL, false );
	if( !afile ) Sys_Error( "%s: couldn't load file %s\n", __func__, DELTA_PATH );

	pfile = (char *)afile;

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		dt = Delta_FindStruct( token );

		if( dt == NULL )
		{
			Sys_Error( "%s: unknown struct %s\n", DELTA_PATH, token );
		}

		pfile = COM_ParseFile( pfile, encodeDll, sizeof( encodeDll ));

		if( !Q_stricmp( encodeDll, "none" ))
			Q_strncpy( encodeFunc, "null", sizeof( encodeFunc ));
		else pfile = COM_ParseFile( pfile, encodeFunc, sizeof( encodeFunc ));

		// jump to '{'
		pfile = COM_ParseFile( pfile, token, sizeof( token ));

		if( token[0] != '{' )
		{
			Sys_Error( "%s: missing '{' in section %s\n", DELTA_PATH, dt->pName );
		}

		Delta_ParseTable( &pfile, dt, encodeDll, encodeFunc );
	}

	Mem_Free( afile );
}

void Delta_Init( void )
{
	delta_info_t	*dt;

	// shutdown it first
	if( delta_init ) Delta_Shutdown ();

	Delta_InitFields ();	// initialize fields
	delta_init = true;

	dt = Delta_FindStructByIndex( DT_MOVEVARS_T );

	Assert( dt != NULL );

	if( dt->bInitialized )
		return;	// "movevars_t" already specified by user

	// create movevars_t delta internal
	Delta_AddField( dt, "gravity", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "stopspeed", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "maxspeed", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "spectatormaxspeed", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "accelerate", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "airaccelerate", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "wateraccelerate", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "friction", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "edgefriction", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "waterfriction", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "bounce", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "stepsize", DT_FLOAT|DT_SIGNED, 16, 16.0f, 1.0f );
	Delta_AddField( dt, "maxvelocity", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );

	// a1ba: set zmax large enough to fit 3d skybox
	// this fixes an issue when mapper sets sv_zmax value high enough
	// to not overflow the variable but not enough to be encoded in delta,
	// thus being clamped at 16-bit signed integer max.
	// by removing signed flag (zmax is always positive) and increasing it to
	// 24 bits, we ensure that even these maps will not have problems with 3d
	// skyboxes (that virtually have no coordinates limit)
	// see comment in SV_UpdateMovevars for more details
	Delta_AddField( dt, "zmax", DT_FLOAT, 24, 1.0f, 1.0f );

	Delta_AddField( dt, "waveHeight", DT_FLOAT|DT_SIGNED, 16, 16.0f, 1.0f );
	Delta_AddField( dt, "skyName", DT_STRING, 1, 1.0f, 1.0f );
	Delta_AddField( dt, "footsteps", DT_INTEGER, 1, 1.0f, 1.0f );
	Delta_AddField( dt, "rollangle", DT_FLOAT|DT_SIGNED, 16, 32.0f, 1.0f );
	Delta_AddField( dt, "rollspeed", DT_FLOAT|DT_SIGNED, 16, 8.0f, 1.0f );
	Delta_AddField( dt, "skycolor_r", DT_FLOAT|DT_SIGNED, 16, 1.0f, 1.0f ); // 0 - 264
	Delta_AddField( dt, "skycolor_g", DT_FLOAT|DT_SIGNED, 16, 1.0f, 1.0f );
	Delta_AddField( dt, "skycolor_b", DT_FLOAT|DT_SIGNED, 16, 1.0f, 1.0f );
	Delta_AddField( dt, "skyvec_x", DT_FLOAT|DT_SIGNED, 16, 32.0f, 1.0f ); // 0 - 1
	Delta_AddField( dt, "skyvec_y", DT_FLOAT|DT_SIGNED, 16, 32.0f, 1.0f );
	Delta_AddField( dt, "skyvec_z", DT_FLOAT|DT_SIGNED, 16, 32.0f, 1.0f );
	Delta_AddField( dt, "wateralpha", DT_FLOAT|DT_SIGNED, 16, 32.0f, 1.0f );
	Delta_AddField( dt, "fog_settings", DT_INTEGER, 32, 1.0f, 1.0f );
	dt->numFields = ARRAYSIZE( pm_fields ) - 4;

	// now done
	dt->bInitialized = true;
}

void Delta_InitClient( void )
{
	int	i, numActive = 0;

	// already initalized
	if( delta_init ) return;

	for( i = 0; i < ARRAYSIZE( dt_info ); i++ )
	{
		if( dt_info[i].numFields > 0 )
		{
			dt_info[i].bInitialized = true;
			numActive++;
		}
	}

	if( numActive ) delta_init = true;
}

void Delta_Shutdown( void )
{
	int	i;

	if( !delta_init ) return;

	for( i = 0; i < ARRAYSIZE( dt_info ); i++ )
	{
		dt_info[i].numFields = 0;
		dt_info[i].customEncode = CUSTOM_NONE;
		dt_info[i].userCallback = NULL;
		dt_info[i].funcName[0] = '\0';

		if( dt_info[i].pFields )
		{
			Z_Free( dt_info[i].pFields );
			dt_info[i].pFields = NULL;
		}

		dt_info[i].bInitialized = false;
	}

	delta_init = false;
}

/*
=====================
Delta_ClampIntegerField

prevent data to out of range
=====================
*/
static int Delta_ClampIntegerField( delta_t *pField, int iValue, int signbit, int numbits )
{
#ifdef _DEBUG
	if( numbits < 32 && abs( iValue ) >= (uint)BIT( numbits ))
		Con_Reportf( S_WARN "Delta_ClampIntegerField: field %s = %d overflowed %d\n", pField->name, abs( iValue ), (uint)BIT( numbits ));
#endif
	if( numbits < 32 )
	{
		int signbits = numbits - signbit;
		int maxnum = BIT( signbits ) - 1;

		if( iValue > maxnum )
			iValue = maxnum;
		else if( signbit && iValue < -maxnum - 1 )
			iValue = -maxnum - 1;
	}

	return iValue; // clamped;
}

/*
=====================
Delta_CompareField

compare fields by offsets
assume from and to is valid
=====================
*/
static qboolean Delta_CompareField( delta_t *pField, const void *from, const void *to )
{
	int		signbit = ( pField->flags & DT_SIGNED ) ? 1 : 0;
	float	val_a, val_b;
	int	fromF, toF;

	Assert( pField != NULL );
	Assert( from != NULL );
	Assert( to != NULL );

	if( pField->bInactive )
		return true;

	fromF = toF = 0;

	if( pField->flags & DT_BYTE )
	{
		if( signbit )
		{
			fromF = *(int8_t *)((int8_t *)from + pField->offset );
			toF = *(int8_t *)((int8_t *)to + pField->offset );
		}
		else
		{
			fromF = *(uint8_t *)((int8_t *)from + pField->offset );
			toF = *(uint8_t *)((int8_t *)to + pField->offset );
		}

		if( !Q_equal(pField->multiplier, 1.0f ))
		{
			fromF *= pField->multiplier;
			toF *= pField->multiplier;
		}

		fromF = Delta_ClampIntegerField( pField, fromF, signbit, pField->bits );
		toF = Delta_ClampIntegerField( pField, toF, signbit, pField->bits );
	}
	else if( pField->flags & DT_SHORT )
	{
		if( signbit )
		{
			fromF = *(int16_t *)((int8_t *)from + pField->offset );
			toF = *(int16_t *)((int8_t *)to + pField->offset );
		}
		else
		{
			fromF = *(uint16_t *)((int8_t *)from + pField->offset );
			toF = *(uint16_t *)((int8_t *)to + pField->offset );
		}

		if( !Q_equal(pField->multiplier, 1.0f ))
		{
			fromF *= pField->multiplier;
			toF *= pField->multiplier;
		}

		fromF = Delta_ClampIntegerField( pField, fromF, signbit, pField->bits );
		toF = Delta_ClampIntegerField( pField, toF, signbit, pField->bits );
	}
	else if( pField->flags & DT_INTEGER )
	{
		if( signbit )
		{
			fromF = *(int32_t *)((int8_t *)from + pField->offset );
			toF = *(int32_t *)((int8_t *)to + pField->offset );
		}
		else
		{
			fromF = *(uint32_t *)((int8_t *)from + pField->offset );
			toF = *(uint32_t *)((int8_t *)to + pField->offset );
		}

		if( !Q_equal(pField->multiplier, 1.0f ))
		{
			fromF *= pField->multiplier;
			toF *= pField->multiplier;
		}

		fromF = Delta_ClampIntegerField( pField, fromF, signbit, pField->bits );
		toF = Delta_ClampIntegerField( pField, toF, signbit, pField->bits );
	}
	else if( pField->flags & ( DT_ANGLE|DT_FLOAT ))
	{
		// don't convert floats to integers
		fromF = *((int *)((byte *)from + pField->offset ));
		toF = *((int *)((byte *)to + pField->offset ));
	}
	else if( pField->flags & DT_TIMEWINDOW_8 )
	{
		val_a = *(float *)((byte *)from + pField->offset );
		val_b = *(float *)((byte *)to + pField->offset );
		fromF = Q_rint( val_a * 100.0 );
		toF = Q_rint( val_b * 100.0 );
	}
	else if( pField->flags & DT_TIMEWINDOW_BIG )
	{
		val_a = *(float *)((byte *)from + pField->offset );
		val_b = *(float *)((byte *)to + pField->offset );
		fromF = Q_rint( val_a * pField->multiplier );
		toF = Q_rint( val_b * pField->multiplier );
	}
	else if( pField->flags & DT_STRING )
	{
		// compare strings
		char	*s1 = (char *)((byte *)from + pField->offset );
		char	*s2 = (char *)((byte *)to + pField->offset );

		// 0 is equal, otherwise not equal
		toF = Q_strcmp( s1, s2 );
	}

	return fromF == toF;
}

/*
=====================
Delta_TestBaseline

compare baselines to find optimal
=====================
*/
int Delta_TestBaseline( const entity_state_t *from, const entity_state_t *to, qboolean player, double timebase )
{
	delta_info_t	*dt = NULL;
	delta_t		*pField;
	int		i, countBits;

	countBits = MAX_ENTITY_BITS + 2;

	if( to == NULL )
	{
		if( from == NULL ) return 0;
		return countBits;
	}

	if( FBitSet( to->entityType, ENTITY_BEAM ))
		dt = Delta_FindStructByIndex( DT_CUSTOM_ENTITY_STATE_T );
	else if( player )
		dt = Delta_FindStructByIndex( DT_ENTITY_STATE_PLAYER_T );
	else dt = Delta_FindStructByIndex( DT_ENTITY_STATE_T );

	Assert( dt && dt->bInitialized );

	countBits++; // entityType flag

	pField = dt->pFields;
	Assert( pField != NULL );

	// activate fields and call custom encode func
	Delta_CustomEncode( dt, from, to );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		// flag about field change (sets always)
		countBits++;

		if( !Delta_CompareField( pField, from, to ))
		{
			// strings are handled differently
			if( FBitSet( pField->flags, DT_STRING ))
				countBits += Q_strlen((char *)((byte *)to + pField->offset )) * 8;
			else countBits += pField->bits;
		}
	}

	// g-cont. compare bitcount directly no reason to call BitByte here
	return countBits;
}

/*
=====================
Delta_WriteField

write fields by offsets
assume from and to is valid
=====================
*/
static void Delta_WriteField_( sizebuf_t *msg, delta_t *pField, const void *from, const void *to, double timebase )
{
	int		signbit = FBitSet( pField->flags, DT_SIGNED ) ? 1 : 0;
	float		flValue, flAngle;
	uint		iValue;
	int dt;
	const char	*pStr;

	if( pField->flags & DT_BYTE )
	{
		if( signbit )
			iValue = *(int8_t *)((int8_t *)to + pField->offset );
		else
			iValue = *(uint8_t *)((int8_t *)to + pField->offset );

		if( !Q_equal( pField->multiplier, 1.0 ))
			iValue *= pField->multiplier;

		iValue = Delta_ClampIntegerField( pField, iValue, signbit, pField->bits );
		MSG_WriteBitLong( msg, iValue, pField->bits, signbit );
	}
	else if( pField->flags & DT_SHORT )
	{
		if( signbit )
			iValue = *(int16_t *)((int8_t *)to + pField->offset );
		else
			iValue = *(uint16_t *)((int8_t *)to + pField->offset );

		if( !Q_equal( pField->multiplier, 1.0 ))
			iValue *= pField->multiplier;

		iValue = Delta_ClampIntegerField( pField, iValue, signbit, pField->bits );
		MSG_WriteBitLong( msg, iValue, pField->bits, signbit );
	}
	else if( pField->flags & DT_INTEGER )
	{
		if( signbit )
			iValue = *(int32_t *)((int8_t *)to + pField->offset );
		else
			iValue = *(uint32_t *)((int8_t *)to + pField->offset );

		if( !Q_equal( pField->multiplier, 1.0 ))
			iValue *= pField->multiplier;

		iValue = Delta_ClampIntegerField( pField, iValue, signbit, pField->bits );
		MSG_WriteBitLong( msg, iValue, pField->bits, signbit );
	}
	else if( pField->flags & DT_FLOAT )
	{
		flValue = *(float *)((byte *)to + pField->offset );
		iValue = (int)((double)flValue * pField->multiplier);
		iValue = Delta_ClampIntegerField( pField, iValue, signbit, pField->bits );
		MSG_WriteBitLong( msg, iValue, pField->bits, signbit );
	}
	else if( pField->flags & DT_ANGLE )
	{
		flAngle = *(float *)((byte *)to + pField->offset );

		// NOTE: never applies multipliers to angle because
		// result may be wrong on client-side
		MSG_WriteBitAngle( msg, flAngle, pField->bits );
	}
	else if( pField->flags & DT_TIMEWINDOW_8 )
	{
		flValue = *(float *)((byte *)to + pField->offset );
		dt = Q_rint(( timebase - flValue ) * 100.0 );
		dt = Delta_ClampIntegerField( pField, dt, 1, pField->bits );
		MSG_WriteSBitLong( msg, dt, pField->bits );
	}
	else if( pField->flags & DT_TIMEWINDOW_BIG )
	{
		flValue = *(float *)((byte *)to + pField->offset );
		dt = Q_rint(( timebase - flValue ) * pField->multiplier );
		dt = Delta_ClampIntegerField( pField, dt, 1, pField->bits );
		MSG_WriteSBitLong( msg, dt, pField->bits );
	}
	else if( pField->flags & DT_STRING )
	{
		pStr = (char *)((byte *)to + pField->offset );
		MSG_WriteString( msg, pStr );
	}
}

static qboolean Delta_WriteField( sizebuf_t *msg, delta_t *pField, const void *from, const void *to, double timebase )
{
	if( Delta_CompareField( pField, from, to ))
	{
		MSG_WriteOneBit( msg, 0 );	// unchanged
		return false;
	}

	MSG_WriteOneBit( msg, 1 );	// changed

	Delta_WriteField_( msg, pField, from, to, timebase );

	return true;
}

/*
====================
Delta_CopyField

====================
*/
static void Delta_CopyField( delta_t *pField, const void *from, void *to, double timebase )
{
	qboolean bSigned = FBitSet( pField->flags, DT_SIGNED );
	uint8_t *to_field = (uint8_t *)to + pField->offset;
	uint8_t *from_field = (uint8_t *)from + pField->offset;

	if( FBitSet( pField->flags, DT_BYTE ))
	{
		if( bSigned )
			*(int8_t *)( to_field ) = *(int8_t *)( from_field );
		else
			*(uint8_t *)( to_field ) = *(uint8_t *)( from_field );
	}
	else if( FBitSet( pField->flags, DT_SHORT ))
	{
		if( bSigned )
			*(int16_t *)( to_field ) = *(int16_t *)( from_field );
		else
			*(uint16_t *)( to_field ) = *(uint16_t *)( from_field );
	}
	else if( FBitSet( pField->flags, DT_INTEGER ))
	{
		if( bSigned )
			*(int32_t *)( to_field ) = *(int32_t *)( from_field );
		else
			*(uint32_t *)( to_field ) = *(uint32_t *)( from_field );
	}
	else if( FBitSet( pField->flags, DT_FLOAT|DT_ANGLE|DT_TIMEWINDOW_8|DT_TIMEWINDOW_BIG ))
	{
		*(float *)( to_field ) = *(float *)( from_field );
	}
	else if( FBitSet( pField->flags, DT_STRING ))
	{
		Q_strncpy( to_field, from_field, pField->size );
	}
	else
	{
		Assert( 0 );
	}
}

/*
=====================
Delta_ReadField

read fields by offsets
assume 'from' and 'to' is valid
=====================
*/
static void Delta_ReadField_( sizebuf_t *msg, delta_t *pField, void *to, double timebase )
{
	qboolean		bSigned = ( pField->flags & DT_SIGNED ) ? true : false;
	float		flValue, flAngle, flTime;
	uint		iValue;
	const char	*pStr;
	char		*pOut;

	Assert( pField->multiplier != 0.0f );

	if( pField->flags & DT_BYTE )
	{
		iValue = MSG_ReadBitLong( msg, pField->bits, bSigned );
		if( !Q_equal( pField->multiplier, 1.0 ))
			iValue /= pField->multiplier;

		if( !Q_equal( pField->post_multiplier, 1.0 ))
			iValue *= pField->post_multiplier;

		if( bSigned )
			*(int8_t *)((uint8_t *)to + pField->offset ) = iValue;
		else
			*(uint8_t *)((uint8_t *)to + pField->offset ) = iValue;
	}
	else if( pField->flags & DT_SHORT )
	{
		iValue = MSG_ReadBitLong( msg, pField->bits, bSigned );
		if( !Q_equal( pField->multiplier, 1.0 ))
			iValue /= pField->multiplier;

		if( !Q_equal( pField->post_multiplier, 1.0 ))
			iValue *= pField->post_multiplier;

		if( bSigned )
			*(int16_t *)((uint8_t *)to + pField->offset ) = iValue;
		else
			*(uint16_t *)((uint8_t *)to + pField->offset ) = iValue;
	}
	else if( pField->flags & DT_INTEGER )
	{
		iValue = MSG_ReadBitLong( msg, pField->bits, bSigned );
		if( !Q_equal( pField->multiplier, 1.0 ))
			iValue /= pField->multiplier;

		if( !Q_equal( pField->post_multiplier, 1.0 ))
			iValue *= pField->post_multiplier;

		if( bSigned )
			*(int32_t *)((uint8_t *)to + pField->offset ) = iValue;
		else
			*(uint32_t *)((uint8_t *)to + pField->offset ) = iValue;
	}
	else if( pField->flags & DT_FLOAT )
	{
		iValue = MSG_ReadBitLong( msg, pField->bits, bSigned );
		if( bSigned )
			flValue = (int)iValue;
		else
			flValue = iValue;

		if( !Q_equal( pField->multiplier, 1.0 ))
			flValue /= pField->multiplier;

		if( !Q_equal( pField->post_multiplier, 1.0 ))
			flValue *= pField->post_multiplier;

		*(float *)((byte *)to + pField->offset ) = flValue;
	}
	else if( pField->flags & DT_ANGLE )
	{
		flAngle = MSG_ReadBitAngle( msg, pField->bits );
		*(float *)((byte *)to + pField->offset ) = flAngle;
	}
	else if( pField->flags & DT_TIMEWINDOW_8 )
	{
		iValue = MSG_ReadSBitLong( msg, pField->bits );
		flTime = ( timebase * 100.0 - (int)iValue ) / 100.0;
		*(float *)((byte *)to + pField->offset ) = flTime;
	}
	else if( pField->flags & DT_TIMEWINDOW_BIG )
	{
		iValue = MSG_ReadSBitLong( msg, pField->bits );
		flTime = ( timebase * pField->multiplier - (int)iValue ) / pField->multiplier;
		*(float *)((byte *)to + pField->offset ) = flTime;
	}
	else if( pField->flags & DT_STRING )
	{
		pStr = MSG_ReadString( msg );
		pOut = (char *)((byte *)to + pField->offset );
		Q_strncpy( pOut, pStr, pField->size );
	}
}

static qboolean Delta_ReadField( sizebuf_t *msg, delta_t *pField, const void *from, void *to, double timebase )
{
	if( !MSG_ReadOneBit( msg ))
	{
		Delta_CopyField( pField, from, to, timebase );
		return false;
	}

	Delta_ReadField_( msg, pField, to, timebase );
	return true;
}

static void Delta_ParseGSFields( sizebuf_t *msg, const delta_info_t *dt, const void *from, void *to, double timebase )
{
	uint8_t bits[8] = { 0 };
	delta_t *pField;
	byte c;
	int i;

	c = MSG_ReadUBitLong( msg, 3 );

	for( i = 0; i < c; i++ )
		bits[i] = MSG_ReadByte( msg );

	for( i = 0, pField = dt->pFields; i < dt->numFields; i++, pField++ )
	{
		int b = i >> 3;
		int n = 1 << ( i & 7 );

		if( FBitSet( bits[b], n ))
			Delta_ReadField_( msg, pField, to, timebase );
		else Delta_CopyField( pField, from, to, timebase );
	}
}

void Delta_ReadGSFields( sizebuf_t *msg, int index, const void *from, void *to, double timebase )
{
	const delta_info_t *dt = Delta_FindStructByIndex( index );
	Delta_ParseGSFields( msg, dt, from, to, timebase );
}

void Delta_WriteGSFields( sizebuf_t *msg, int index, const void *from, const void *to, double timebase )
{
	delta_info_t *dt = Delta_FindStructByIndex( index );
	delta_t *pField;
	uint8_t bits[8] = { 0 };
	uint c = 0;
	int i;

	Delta_CustomEncode( dt, from, to );

	for( i = 0, pField = dt->pFields; i < dt->numFields; i++, pField++ )
	{
		if( !Delta_CompareField( pField, from, to ))
		{
			int b = i >> 3;
			int n = 1 << ( i & 7 );

			SetBits( bits[b], n );
			c = b + 1;
		}
	}

	MSG_WriteUBitLong( msg, c, 3 );
	for( i = 0; i < c; i++ )
		MSG_WriteByte( msg, bits[i] );

	for( i = 0, pField = dt->pFields; i < dt->numFields; i++, pField++ )
	{
		int b = i >> 3;
		int n = 1 << ( i & 7 );

		if( FBitSet( bits[b], n ))
			Delta_WriteField_( msg, pField, from, to, timebase );
	}
}

/*
=============================================================================

usercmd_t communication

=============================================================================
*/
/*
=====================
MSG_WriteDeltaUsercmd
=====================
*/
void MSG_WriteDeltaUsercmd( sizebuf_t *msg, const usercmd_t *from, const usercmd_t *to )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i;

	dt = Delta_FindStructByIndex( DT_USERCMD_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	// activate fields and call custom encode func
	Delta_CustomEncode( dt, from, to );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		Delta_WriteField( msg, pField, from, to, 0.0f );
	}
}

/*
=====================
MSG_ReadDeltaUsercmd
=====================
*/
void MSG_ReadDeltaUsercmd( sizebuf_t *msg, const usercmd_t *from, usercmd_t *to )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i;

	dt = Delta_FindStructByIndex( DT_USERCMD_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	*to = *from;

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		Delta_ReadField( msg, pField, from, to, 0.0f );
	}

	COM_NormalizeAngles( to->viewangles );
}

/*
============================================================================

event_args_t communication

============================================================================
*/
/*
=====================
MSG_WriteDeltaEvent
=====================
*/
void MSG_WriteDeltaEvent( sizebuf_t *msg, const event_args_t *from, const event_args_t *to )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i;

	dt = Delta_FindStructByIndex( DT_EVENT_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	// activate fields and call custom encode func
	Delta_CustomEncode( dt, from, to );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		Delta_WriteField( msg, pField, from, to, 0.0f );
	}
}

/*
=====================
MSG_ReadDeltaEvent
=====================
*/
void MSG_ReadDeltaEvent( sizebuf_t *msg, const event_args_t *from, event_args_t *to )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i;

	dt = Delta_FindStructByIndex( DT_EVENT_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	*to = *from;

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		Delta_ReadField( msg, pField, from, to, 0.0f );
	}
}

/*
=============================================================================

movevars_t communication

=============================================================================
*/
qboolean MSG_WriteDeltaMovevars( sizebuf_t *msg, const movevars_t *from, const movevars_t *to )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i, startBit;
	int		numChanges = 0;

	dt = Delta_FindStructByIndex( DT_MOVEVARS_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	startBit = msg->iCurBit;

	// activate fields and call custom encode func
	Delta_CustomEncode( dt, from, to );

	MSG_BeginServerCmd( msg, svc_deltamovevars );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		if( Delta_WriteField( msg, pField, from, to, 0.0f ))
			numChanges++;
	}

	// if we have no changes - kill the message
	if( !numChanges )
	{
		MSG_SeekToBit( msg, startBit, SEEK_SET );
		return false;
	}
	return true;
}

void MSG_ReadDeltaMovevars( sizebuf_t *msg, const movevars_t *from, movevars_t *to )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i;

	dt = Delta_FindStructByIndex( DT_MOVEVARS_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	*to = *from;

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		Delta_ReadField( msg, pField, from, to, 0.0f );
	}
}

/*
=============================================================================

clientdata_t communication

=============================================================================
*/
/*
==================
MSG_WriteClientData

Writes current client data only for local client
Other clients can grab the client state from entity_state_t
==================
*/
void MSG_WriteClientData( sizebuf_t *msg, const clientdata_t *from, const clientdata_t *to, double timebase )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i, startBit;
	int		numChanges = 0;

	dt = Delta_FindStructByIndex( DT_CLIENTDATA_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	startBit = msg->iCurBit;

	MSG_WriteOneBit( msg, 1 ); // have clientdata

	// activate fields and call custom encode func
	Delta_CustomEncode( dt, from, to );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		if( Delta_WriteField( msg, pField, from, to, timebase ))
			numChanges++;
	}

	if( numChanges ) return; // we have updates

	MSG_SeekToBit( msg, startBit, SEEK_SET );
	MSG_WriteOneBit( msg, 0 ); // no changes
}

/*
==================
MSG_ReadClientData

Read the clientdata
==================
*/
void MSG_ReadClientData( sizebuf_t *msg, const clientdata_t *from, clientdata_t *to, double timebase )
{
#if !XASH_DEDICATED
	delta_t		*pField;
	delta_info_t	*dt;
	int		i;
	qboolean noChanges;

	dt = Delta_FindStructByIndex( DT_CLIENTDATA_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	noChanges = !MSG_ReadOneBit( msg );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		if( noChanges )
			Delta_CopyField( pField, from, to, timebase );
		else Delta_ReadField( msg, pField, from, to, timebase );
	}
#endif
}

/*
=============================================================================

weapon_data_t communication

=============================================================================
*/
/*
==================
MSG_WriteWeaponData

Writes current client data only for local client
Other clients can grab the client state from entity_state_t
==================
*/
void MSG_WriteWeaponData( sizebuf_t *msg, const weapon_data_t *from, const weapon_data_t *to, double timebase, int index )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i, startBit;
	int		numChanges = 0;

	dt = Delta_FindStructByIndex( DT_WEAPONDATA_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	// activate fields and call custom encode func
	Delta_CustomEncode( dt, from, to );

	startBit = msg->iCurBit;

	MSG_WriteOneBit( msg, 1 );
	MSG_WriteUBitLong( msg, index, MAX_WEAPON_BITS );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		if( Delta_WriteField( msg, pField, from, to, timebase ))
			numChanges++;
	}

	// if we have no changes - kill the message
	if( !numChanges ) MSG_SeekToBit( msg, startBit, SEEK_SET );
}

/*
==================
MSG_ReadWeaponData

Read the clientdata
==================
*/
void MSG_ReadWeaponData( sizebuf_t *msg, const weapon_data_t *from, weapon_data_t *to, double timebase )
{
	delta_t		*pField;
	delta_info_t	*dt;
	int		i;

	dt = Delta_FindStructByIndex( DT_WEAPONDATA_T );
	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		Delta_ReadField( msg, pField, from, to, timebase );
	}
}

/*
=============================================================================

entity_state_t communication

=============================================================================
*/
/*
==================
MSG_WriteDeltaEntity

Writes part of a packetentities message, including the entity number.
Can delta from either a baseline or a previous packet_entity
If to is NULL, a remove entity update will be sent
If force is not set, then nothing at all will be generated if the entity is
identical, under the assumption that the in-order delta code will catch it.
==================
*/
void MSG_WriteDeltaEntity( const entity_state_t *from, const entity_state_t *to, sizebuf_t *msg, qboolean force, int delta_type, double timebase, int baseline )
{
	delta_info_t	*dt = NULL;
	delta_t		*pField;
	int		i, startBit;
	int		numChanges = 0;

	if( to == NULL )
	{
		int	fRemoveType;

		if( from == NULL ) return;

		// a NULL to is a delta remove message
		MSG_WriteUBitLong( msg, from->number, MAX_ENTITY_BITS );

		// fRemoveType:
		// 0 - keep alive, has delta-update
		// 1 - remove from delta message (but keep states)
		// 2 - completely remove from server
		if( force ) fRemoveType = 2;
		else fRemoveType = 1;

		MSG_WriteUBitLong( msg, fRemoveType, 2 );
		return;
	}

	startBit = msg->iCurBit;

	if( to->number < 0 || to->number >= GI->max_edicts )
		Host_Error( "%s: Bad entity number: %i\n", __func__, to->number );

	MSG_WriteUBitLong( msg, to->number, MAX_ENTITY_BITS );
	MSG_WriteUBitLong( msg, 0, 2 ); // alive

	if( baseline != 0 )
	{
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteSBitLong( msg, baseline, 7 );
	}
	else MSG_WriteOneBit( msg, 0 );

	if( force || ( to->entityType != from->entityType ))
	{
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteUBitLong( msg, to->entityType, 2 );
		numChanges++;
	}
	else MSG_WriteOneBit( msg, 0 );

	if( FBitSet( to->entityType, ENTITY_BEAM ))
	{
		dt = Delta_FindStructByIndex( DT_CUSTOM_ENTITY_STATE_T );
	}
	else if( delta_type == DELTA_PLAYER )
	{
		dt = Delta_FindStructByIndex( DT_ENTITY_STATE_PLAYER_T );
	}
	else
	{
		dt = Delta_FindStructByIndex( DT_ENTITY_STATE_T );
	}

	Assert( dt && dt->bInitialized );

	pField = dt->pFields;
	Assert( pField != NULL );

	if( delta_type == DELTA_STATIC )
	{
		// static entities won't to be custom encoded
		for( i = 0; i < dt->numFields; i++ )
			dt->pFields[i].bInactive = false;
	}
	else
	{
		// activate fields and call custom encode func
		Delta_CustomEncode( dt, from, to );
	}

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		if( Delta_WriteField( msg, pField, from, to, timebase ))
			numChanges++;
	}

	// if we have no changes - kill the message
	if( !numChanges && !force ) MSG_SeekToBit( msg, startBit, SEEK_SET );
}

/*
==================
MSG_ReadDeltaEntity

The entity number has already been read from the message, which
is how the from state is identified.

If the delta removes the entity, entity_state_t->number will be set to MAX_EDICTS
Can go from either a baseline or a previous packet_entity
==================
*/
qboolean MSG_ReadDeltaEntity( sizebuf_t *msg, const entity_state_t *from, entity_state_t *to, int number, int delta_type, double timebase )
{
#if !XASH_DEDICATED
	delta_info_t	*dt = NULL;
	delta_t		*pField;
	int		i, fRemoveType;
	int		baseline_offset = 0;

	if( number < 0 || number >= clgame.maxEntities )
	{
		Con_Printf( S_ERROR "%s: bad delta entity number: %i\n", __func__, number );
		return false;
	}

	fRemoveType = MSG_ReadUBitLong( msg, 2 );

	if( fRemoveType )
	{
		// check for a remove
		memset( to, 0, sizeof( *to ));

		if( fRemoveType & 1 )
		{
			// removed from delta-message
			return false;
		}

		if( fRemoveType & 2 )
		{
			// entity was removed from server
			to->number = -1;
			return false;
		}

		Con_Printf( S_ERROR "%s: unknown update type %i\n", __func__, fRemoveType );
		return false;
	}

	if( MSG_ReadOneBit( msg ))
		baseline_offset = MSG_ReadSBitLong( msg, 7 );

	if( baseline_offset != 0 )
	{
		if( delta_type == DELTA_STATIC )
		{
			int backup = Q_max( 0, clgame.numStatics - abs( baseline_offset ));
			from = &clgame.static_entities[backup].baseline;
		}
		else if( baseline_offset > 0 )
		{
			int backup = cls.next_client_entities - baseline_offset;
			from = &cls.packet_entities[backup % cls.num_client_entities];
		}
		else
		{
			baseline_offset = abs( baseline_offset + 1 );
			if( baseline_offset < cl.instanced_baseline_count )
				from = &cl.instanced_baseline[baseline_offset];
		}
	}

	// g-cont. probably is redundant
	*to = *from;

	if( MSG_ReadOneBit( msg ))
		to->entityType = MSG_ReadUBitLong( msg, 2 );
	to->number = number;

	if( FBitSet( to->entityType, ENTITY_BEAM ))
	{
		dt = Delta_FindStructByIndex( DT_CUSTOM_ENTITY_STATE_T );
	}
	else if( delta_type == DELTA_PLAYER )
	{
		dt = Delta_FindStructByIndex( DT_ENTITY_STATE_PLAYER_T );
	}
	else
	{
		dt = Delta_FindStructByIndex( DT_ENTITY_STATE_T );
	}

	if( !dt || !dt->bInitialized )
	{
		Con_Printf( S_ERROR "%s: broken delta\n", __func__ );
		return true;
	}

	pField = dt->pFields;
	Assert( pField != NULL );

	// process fields
	for( i = 0; i < dt->numFields; i++, pField++ )
	{
		Delta_ReadField( msg, pField, from, to, timebase );
	}
#endif // XASH_DEDICATED
	// message parsed
	return true;
}

void Delta_ParseTableField_GS( sizebuf_t *msg )
{
	const char *s = MSG_ReadString( msg );
	delta_info_t *dt = Delta_FindStruct( s );
	goldsrc_delta_t null = { 0 };
	int i, num_fields;

	// delta encoders it's already initialized on this machine (local game)
	if( delta_init )
		Delta_Shutdown();

	if( !dt )
		Host_Error( "%s: not initialized", __func__ );

	num_fields = MSG_ReadShort( msg );
	if( num_fields > dt->maxFields )
		Host_Error( "%s: numFields > maxFields", __func__ );

	MSG_StartBitWriting( msg );

	for( i = 0; i < num_fields; i++ )
	{
		goldsrc_delta_t to;

		Delta_ParseGSFields( msg, &dt_goldsrc_meta, &null, &to, 0.0f );

		// patch our DT_SIGNED flag
		if( FBitSet( to.fieldType, DT_SIGNED_GS ))
		{
			ClearBits( to.fieldType, DT_SIGNED_GS );
			SetBits( to.fieldType, DT_SIGNED );
		}
		Delta_AddField( dt, to.fieldName, to.fieldType, to.significant_bits, to.premultiply, to.postmultiply );
	}

	MSG_EndBitWriting( msg );
}

/*
==================
Delta_WriteDescriptionToClient

send delta communication encoding
==================
*/
void Delta_WriteDescriptionToClient( sizebuf_t *msg )
{
	int	tableIndex;
	int	fieldIndex;

	for( tableIndex = 0; tableIndex < Delta_NumTables(); tableIndex++ )
	{
		delta_info_t	*dt = Delta_FindStructByIndex( tableIndex );

		for( fieldIndex = 0; fieldIndex < dt->numFields; fieldIndex++ )
			Delta_WriteTableField( msg, tableIndex, &dt->pFields[fieldIndex] );
	}
}

/*
=============================================================================

	game.dll interface

=============================================================================
*/
void GAME_EXPORT Delta_AddEncoder( char *name, pfnDeltaEncode encodeFunc )
{
	delta_info_t	*dt;

	dt = Delta_FindStructByEncoder( name );

	if( !dt || !dt->bInitialized )
	{
		Con_DPrintf( S_ERROR "%s: couldn't find delta with specified custom encode %s\n", __func__, name );
		return;
	}

	if( dt->customEncode == CUSTOM_NONE )
	{
		Con_DPrintf( S_ERROR "%s: %s not supposed for custom encoding\n", __func__, dt->pName );
		return;
	}

	// register new encode func
	dt->userCallback = encodeFunc;
}

int GAME_EXPORT Delta_FindField( delta_t *pFields, const char *fieldname )
{
	delta_info_t	*dt;
	delta_t		*pField;
	int		i;

	dt = Delta_FindStructByDelta( pFields );
	if( dt == NULL || !fieldname || !fieldname[0] )
		return -1;

	for( i = 0, pField = dt->pFields; i < dt->numFields; i++, pField++ )
	{
		if( !Q_strcmp( pField->name, fieldname ))
			return i;
	}
	return -1;
}

void GAME_EXPORT Delta_SetField( delta_t *pFields, const char *fieldname )
{
	delta_info_t	*dt;
	delta_t		*pField;
	int		i;

	dt = Delta_FindStructByDelta( pFields );
	if( dt == NULL || !fieldname || !fieldname[0] )
		return;

	for( i = 0, pField = dt->pFields; i < dt->numFields; i++, pField++ )
	{
		if( !Q_strcmp( pField->name, fieldname ))
		{
			pField->bInactive = false;
			return;
		}
	}
}

void GAME_EXPORT Delta_UnsetField( delta_t *pFields, const char *fieldname )
{
	delta_info_t	*dt;
	delta_t		*pField;
	int		i;

	dt = Delta_FindStructByDelta( pFields );
	if( dt == NULL || !fieldname || !fieldname[0] )
		return;

	for( i = 0, pField = dt->pFields; i < dt->numFields; i++, pField++ )
	{
		if( !Q_strcmp( pField->name, fieldname ))
		{
			pField->bInactive = true;
			return;
		}
	}
}

void GAME_EXPORT Delta_SetFieldByIndex( delta_t *pFields, int fieldNumber )
{
	delta_info_t	*dt;

	dt = Delta_FindStructByDelta( pFields );
	if( dt == NULL || fieldNumber < 0 || fieldNumber >= dt->numFields )
		return;

	dt->pFields[fieldNumber].bInactive = false;
}

void GAME_EXPORT Delta_UnsetFieldByIndex( delta_t *pFields, int fieldNumber )
{
	delta_info_t	*dt;

	dt = Delta_FindStructByDelta( pFields );
	if( dt == NULL || fieldNumber < 0 || fieldNumber >= dt->numFields )
		return;

	dt->pFields[fieldNumber].bInactive = true;
}

#if XASH_ENGINE_TESTS
#include "tests.h"

void Test_RunDelta( void )
{
	delta_info_t *dt = &dt_info[DT_DELTA_TEST_STRUCT_T];
	delta_test_struct_t from, to = { 0 };
	delta_test_struct_t null = { 0 };
	sizebuf_t msg;
	int i;
	char buffer[4096] = { 0 };
	const double timebase = 123.123;

	Delta_AddField( dt, "dt_string", DT_STRING, 1, 1.0f, 1.0f );
	Delta_AddField( dt, "dt_timewindow_big", DT_TIMEWINDOW_BIG, 24, 1000.f, 1.0f );
	Delta_AddField( dt, "dt_timewindow_8", DT_TIMEWINDOW_8, 8, 1.0f, 1.0f );
	Delta_AddField( dt, "dt_angle", DT_ANGLE, 16, 1.0f, 1.0f );
	Delta_AddField( dt, "dt_float_signed", DT_FLOAT | DT_SIGNED, 22, 100.0f, 1.0f );
	Delta_AddField( dt, "dt_float_unsigned", DT_FLOAT, 24, 10000.0f, 0.1f );
	Delta_AddField( dt, "dt_integer_signed", DT_INTEGER | DT_SIGNED, 24, 1.0f, 1.0f );
	Delta_AddField( dt, "dt_integer_unsigned", DT_INTEGER, 24, 1.0f, 1.0f );
	Delta_AddField( dt, "dt_short_signed", DT_SHORT | DT_SIGNED, 16, 1.0f, 1.0f );
	Delta_AddField( dt, "dt_short_unsigned", DT_SHORT, 15, 0.125f, 1.0f );
	Delta_AddField( dt, "dt_byte_signed", DT_BYTE | DT_SIGNED, 6, 1.0f, 1.0f );
	Delta_AddField( dt, "dt_byte_unsigned", DT_BYTE, 8, 1.0f, 1.0f );

	Q_strncpy( from.dt_string, "test data check it's the same", sizeof( from.dt_string ));
	from.dt_timewindow_big = timebase + 2.3456;
	from.dt_timewindow_8 = timebase + 0.0234;
	from.dt_angle = 160.245f;
	from.dt_float_signed = -15.123f;
	from.dt_float_unsigned = 1235.321f;
	from.dt_integer_signed = -412784;
	from.dt_integer_unsigned = 123453;
	from.dt_short_signed = -12343;
	from.dt_short_unsigned = 32131;
	from.dt_byte_signed = 16;
	from.dt_byte_unsigned = 218;

	MSG_Init( &msg, "test message", buffer, sizeof( buffer ));

	for( i = 0; i < dt->numFields; i++ )
		Delta_WriteField( &msg, &dt->pFields[i], &null, &from, timebase );

	MSG_SeekToBit( &msg, 0, SEEK_SET );

	for( i = 0; i < dt->numFields; i++ )
		Delta_ReadField( &msg, &dt->pFields[i], &null, &to, timebase );

	Con_Printf( "struct as encoded to delta:\n" );
	TASSERT_STR( from.dt_string, to.dt_string );

	// the epsilon value is derived from multiplier value
	TASSERT( Q_equal_e( from.dt_timewindow_big, to.dt_timewindow_big, 0.001f ));

	// dt_timewindow_8 type has multiplier locked at 100.0f
	TASSERT( Q_equal_e( from.dt_timewindow_8, to.dt_timewindow_8, 0.01f ));
	TASSERT( Q_equal_e( from.dt_angle, to.dt_angle, 0.1f ));
	TASSERT( Q_equal_e( from.dt_float_signed, to.dt_float_signed, 0.01f ));

	// dt_float_unsigned has post-multiplier that doesn't affect network data
	// and therefore should be reverted back when comparing
	TASSERT( Q_equal_e( from.dt_float_unsigned, to.dt_float_unsigned * 10.f , 0.01f ));

	TASSERT_EQi( from.dt_integer_signed, to.dt_integer_signed );
	TASSERT_EQi( from.dt_integer_unsigned, to.dt_integer_unsigned );
	TASSERT_EQi( from.dt_short_signed, to.dt_short_signed );
	TASSERT(( from.dt_short_unsigned & ( 0xffff << 3 )) == to.dt_short_unsigned );
	TASSERT_EQi( from.dt_byte_signed, to.dt_byte_signed );
	TASSERT_EQi( from.dt_byte_unsigned, to.dt_byte_unsigned );

	Con_Printf( "from.dt_timewindow_big = %f\n", from.dt_timewindow_big );
	Con_Printf( "to.dt_timewindow_big   = %f\n", to.dt_timewindow_big );
	Con_Printf( "from.dt_timewindow_8 = %f\n", from.dt_timewindow_8 );
	Con_Printf( "to.dt_timewindow_8   = %f\n", to.dt_timewindow_8 );
	Con_Printf( "from.dt_angle = %f\n", from.dt_angle );
	Con_Printf( "to.dt_angle   = %f\n", to.dt_angle );
	Con_Printf( "from.dt_float_signed = %f\n", from.dt_float_signed );
	Con_Printf( "to.dt_float_signed   = %f\n", to.dt_float_signed );
	Con_Printf( "from.dt_float_unsigned = %f\n", from.dt_float_unsigned );
	Con_Printf( "to.dt_float_unsigned   = %f\n", to.dt_float_unsigned );
	Con_Printf( "from.dt_integer_signed = %i\n", from.dt_integer_signed );
	Con_Printf( "to.dt_integer_signed   = %i\n", to.dt_integer_signed );
	Con_Printf( "from.dt_integer_unsigned = %i\n", from.dt_integer_unsigned );
	Con_Printf( "to.dt_integer_unsigned   = %i\n", to.dt_integer_unsigned );
	Con_Printf( "from.dt_short_signed = %i\n", from.dt_short_signed );
	Con_Printf( "to.dt_short_signed   = %i\n", to.dt_short_signed );
	Con_Printf( "from.dt_short_unsigned = %i\n", from.dt_short_unsigned );
	Con_Printf( "to.dt_short_unsigned   = %i\n", to.dt_short_unsigned );
	Con_Printf( "from.dt_byte_signed = %i\n", from.dt_byte_signed );
	Con_Printf( "to.dt_byte_signed   = %i\n", to.dt_byte_signed );
	Con_Printf( "from.dt_byte_unsigned = %i\n", from.dt_byte_unsigned );
	Con_Printf( "to.dt_byte_unsigned   = %i\n", to.dt_byte_unsigned );
}
#endif // XASH_ENGINE_TESTS
