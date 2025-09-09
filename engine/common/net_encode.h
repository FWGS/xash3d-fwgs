/*
net_encode.h - delta encode routines
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

#ifndef NET_ENCODE_H
#define NET_ENCODE_H

#include "eiface.h"

enum
{
	CUSTOM_NONE = 0,
	CUSTOM_SERVER_ENCODE,	// known as "gamedll"
	CUSTOM_CLIENT_ENCODE,	// known as "client"
};

// don't change order!
enum
{
	DELTA_ENTITY = 0,
	DELTA_PLAYER,
	DELTA_STATIC,
};

enum
{
	DT_EVENT_T = 0,
	DT_MOVEVARS_T,
	DT_USERCMD_T,
	DT_CLIENTDATA_T,
	DT_WEAPONDATA_T,
	DT_ENTITY_STATE_T,
	DT_ENTITY_STATE_PLAYER_T,
	DT_CUSTOM_ENTITY_STATE_T,
#if XASH_ENGINE_TESTS
	DT_DELTA_TEST_STRUCT_T,
#endif
};

// struct info (filled by engine)
typedef struct
{
	const char	*name;
	const int		offset;
	const int		size;
} delta_field_t;

// one field
struct delta_s
{
	const char	*name;
	int		offset;		// in bytes
	int		size;		// used for bounds checking in DT_STRING
	int		flags;		// DT_INTEGER, DT_FLOAT etc
	float		multiplier;
	float		post_multiplier;	// for DEFINE_DELTA_POST
	int		bits;		// how many bits we send\receive
	qboolean		bInactive;	// unsetted by user request
};

typedef struct goldsrc_delta_s
{
	int   fieldType;
	char  fieldName[32];
	int   fieldOffset;
	short fieldSize;
	int   significant_bits;
	float premultiply;
	float postmultiply;
} goldsrc_delta_t;

typedef void (*pfnDeltaEncode)( struct delta_s *pFields, const byte *from, const byte *to );

typedef struct
{
	const char	*pName;
	const delta_field_t	*pInfo;
	const int		maxFields;	// maximum number of fields in struct
	int		numFields;	// may be merged during initialization
	delta_t		*pFields;

	// added these for custom entity encode
	int		customEncode;
	char		funcName[32];
	pfnDeltaEncode	userCallback;
	qboolean		bInitialized;
} delta_info_t;

//
// net_encode.c
//
void Delta_Init( void );
void Delta_InitClient( void );
void Delta_Shutdown( void );
void Delta_AddEncoder( char *name, pfnDeltaEncode encodeFunc );
int Delta_FindField( delta_t *pFields, const char *fieldname );
void Delta_SetField( delta_t *pFields, const char *fieldname );
void Delta_UnsetField( delta_t *pFields, const char *fieldname );
void Delta_SetFieldByIndex( delta_t *pFields, int fieldNumber );
void Delta_UnsetFieldByIndex( delta_t *pFields, int fieldNumber );

// send table over network
void Delta_WriteDescriptionToClient( sizebuf_t *msg );
void Delta_ParseTableField( sizebuf_t *msg );
void Delta_ParseTableField_GS( sizebuf_t *msg );


// encode routines
struct entity_state_s;
struct usercmd_s;
struct event_args_s;
struct movevars_s;
struct clientdata_s;
struct weapon_data_s;
void MSG_WriteDeltaUsercmd( sizebuf_t *msg, const struct usercmd_s *from, const struct usercmd_s *to );
void MSG_ReadDeltaUsercmd( sizebuf_t *msg, const struct usercmd_s *from, struct usercmd_s *to );
void MSG_WriteDeltaEvent( sizebuf_t *msg, const struct event_args_s *from, const struct event_args_s *to );
void MSG_ReadDeltaEvent( sizebuf_t *msg, const struct event_args_s *from, struct event_args_s *to );
qboolean MSG_WriteDeltaMovevars( sizebuf_t *msg, const struct movevars_s *from, const struct movevars_s *to );
void MSG_ReadDeltaMovevars( sizebuf_t *msg, const struct movevars_s *from, struct movevars_s *to );
void MSG_WriteClientData( sizebuf_t *msg, const struct clientdata_s *from, const struct clientdata_s *to, double timebase );
void MSG_ReadClientData( sizebuf_t *msg, const struct clientdata_s *from, struct clientdata_s *to, double timebase );
void MSG_WriteWeaponData( sizebuf_t *msg, const struct weapon_data_s *from, const struct weapon_data_s *to, double timebase, int index );
void MSG_ReadWeaponData( sizebuf_t *msg, const struct weapon_data_s *from, struct weapon_data_s *to, double timebase );
void MSG_WriteDeltaEntity( const struct entity_state_s *from, const struct entity_state_s *to, sizebuf_t *msg, qboolean force, int type, double timebase, int ofs );
qboolean MSG_ReadDeltaEntity( sizebuf_t *msg, const struct entity_state_s *from, struct entity_state_s *to, int num, int type, double timebase );
int Delta_TestBaseline( const struct entity_state_s *from, const struct entity_state_s *to, qboolean player, double timebase );
void Delta_ReadGSFields( sizebuf_t *msg, int index, const void *from, void *to, double timebase );
void Delta_WriteGSFields( sizebuf_t *msg, int index, const void *from, const void *to, double timebase );

#endif//NET_ENCODE_H
