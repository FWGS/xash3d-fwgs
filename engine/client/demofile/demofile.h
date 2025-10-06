/*
demo.h - Interface for working with various demoformats.
Copyright (C) 2025 Garey

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/


#ifndef DEMOINTERFACE_H
#define DEMOINTERFACE_H

#include "common.h"
#include "client.h"
#include "cdll_int.h"

typedef qboolean (*DEM_RECORD_START)( file_t *file );
typedef qboolean (*DEM_RECORD_STOP)( file_t *file );
typedef qboolean (*DEM_PLAY_START)( file_t *file );
typedef qboolean (*DEM_PLAY_STOP)( file_t *file );
typedef qboolean (*DEM_LIST_DEMO)( file_t *file );
typedef qboolean (*DEM_CAN_HANDLE)( file_t *file );

typedef void (*DEM_WRITE_USERCMD)( int cmdnumber );
typedef void (*DEM_WRITE_CLIENTDATA)( client_data_t *cdata );
typedef void (*DEM_WRITE_NETPACKET)( qboolean startup, int start, sizebuf_t *msg );
typedef void (*DEM_WRITE_COMMAND)( const char *cmdname );
typedef void (*DEM_WRITE_SOUND)( int channel, const char *sample, float vol, float attenuation, int flags, int pitch );
typedef void (*DEM_WRITE_ANIM)( int anim, int body );
typedef void (*DEM_WRITE_EVENT)( int flags, int idx, float delay, event_args_t *pargs );
typedef void (*DEM_WRITE_DEMOMESSAGE)( int size, byte *buffer );
typedef void (*DEM_INTERP_VIEWANGLES)( void );

typedef qboolean (*DEM_READ_DEMOMESSAGE)( byte *buffer, size_t *length );

typedef void (*DEM_RESET_HANDLER)( );

typedef double (*DEM_GET_HOSTFPS)( void );

typedef struct
{
	float  starttime;
	vec3_t viewangles;
} demoangle_t;

typedef demoangle_t demoangle_backup_t[ANGLE_BACKUP];

typedef struct demo_interface_s
{
	DEM_RECORD_START      StartRecord;
	DEM_RECORD_START      StopRecord;
	DEM_RECORD_START      StartPlayback;
	DEM_RECORD_START      StopPlayback;
	DEM_LIST_DEMO         ListDemo;
	DEM_CAN_HANDLE        CanHandle;
	DEM_RESET_HANDLER     ResetHandler;
	DEM_WRITE_ANIM        WriteAnim;
	DEM_WRITE_CLIENTDATA  WriteClientData;
	DEM_WRITE_EVENT       WriteEvent;
	DEM_WRITE_NETPACKET   WriteNetPacket;
	DEM_WRITE_SOUND       WriteSound;
	DEM_WRITE_COMMAND     WriteStringCMD;
	DEM_WRITE_USERCMD     WriteUserCmd;
	DEM_WRITE_DEMOMESSAGE WriteDemoMesssage;
	DEM_READ_DEMOMESSAGE  ReadDemoMessage;
	DEM_GET_HOSTFPS       GetHostFPS;
	DEM_INTERP_VIEWANGLES InterpolateAngles;
} demo_interface_t;

typedef struct demo_handler_s
{
	const char *name;
	demo_interface_t funcs;
	struct demo_handler_s *next;
} demo_handler_t;

void DEM_Init( );
void DEM_RegisterHandler( demo_handler_t *handler );
void DEM_ResetHandler( );

qboolean DEM_StartRecord( file_t *file );
qboolean DEM_StopRecord( file_t *file );
qboolean DEM_StartPlayback( file_t *file );
qboolean DEM_StopPlayback( file_t *file );
qboolean DEM_ListDemo( file_t *file );

// Write/Record specific functions
void DEM_WriteDemoUserCmd( int cmdnumber );
void DEM_WriteNetPacket( qboolean startup, int start, sizebuf_t *msg );
void DEM_WriteDemoUserMessage( int size, byte *buffer );
void DEM_WriteAnim( int anim, int body );
void DEM_WriteClientData( client_data_t *cdata );
void DEM_WriteEvent( int flags, int idx, float delay, event_args_t *pargs );
void DEM_WriteSound( int channel, const char *sample, float vol, float attenuation, int flags, int pitch );
void DEM_WriteStringCmd( const char *cmd );

// Read/Playback specific functions
qboolean DEM_DemoReadMessage( byte *buffer, size_t *length );

double DEM_GetHostFPS( );

// Handler public init funcs
void DEM_GS_InitHandler( void );
void DEM_Xash_InitHandler( void );

void DEM_DemoInterpolateAngles( void );

void CL_DemoStartPlayback( int mode );
void CL_DemoAborted( void );



#endif // DEMOINTERFACE_H
