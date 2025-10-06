/*
demo.c - Interface for working with various demoformats.
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

#include "demofile.h"
#include "crtlib.h"

static demo_handler_t *demo_handlers = NULL;
static demo_handler_t *demo_handler = NULL;


static CVAR_DEFINE_AUTO( dem_format, "xash3d", FCVAR_ARCHIVE | FCVAR_PROTECTED, "Default demo format for writting demos: [xash3d, goldsource]" );


static demo_handler_t *DEM_GetHandler( const char *format_name )
{
	demo_handler_t *current = demo_handlers;
	while( current != NULL )
	{
		if( Q_strcmp( current->name, format_name ) == 0 )
		{
			return current;
		}
		current = current->next;
	}
	return NULL;
}

void DEM_Init( void )
{
	Cvar_RegisterVariable( &dem_format );

	DEM_Xash_InitHandler();
	DEM_GS_InitHandler();
}

void DEM_RegisterHandler( demo_handler_t *handler )
{
	if( !handler )
		return;

	handler->next = demo_handlers;
	demo_handlers = handler;
}

void DEM_ResetHandler()
{
	if( demo_handler && demo_handler->funcs.ResetHandler )
	{
		demo_handler->funcs.ResetHandler();
	}
	demo_handler = NULL;
}

qboolean DEM_FindRecordHandler()
{
	// First pass, user handler from cvar
	if( !demo_handler )
	{
		demo_handler = DEM_GetHandler( dem_format.string );
	}

	// Second pass (if invalid handler in cvar string)
	if( !demo_handler )
	{
		Con_Printf( "dem_format \"%s\" is not valid, resetting to \"xash3d\"\n", dem_format.string );
		Con_Printf( "Available formats: [xash3d, goldsource]\n" );
		Cvar_DirectSet( &dem_format, "xash3d" );
		demo_handler = DEM_GetHandler( "xash3d" );
	}

	return( demo_handler != NULL );
}

demo_handler_t *DEM_FindPlaybackHandler( file_t *file )
{
	qboolean can_handle = false;
	demo_handler_t *current = demo_handlers;
	while( current != NULL )
	{
		if( current->funcs.CanHandle )
		{
			can_handle = current->funcs.CanHandle( file );
			// Reset position to start of demo
			FS_Seek( file, 0, SEEK_SET );
			if( can_handle )
			{
				return current;
			}
		}
		current = current->next;
	}

	return NULL;
}

qboolean DEM_StartRecord( file_t *file )
{
	if( cls.demorecording )
	{
		// Already recording
		return false;
	}
	if( !DEM_FindRecordHandler())
	{
		return false;
	}
	if( demo_handler && demo_handler->funcs.StartRecord )
	{
		return demo_handler->funcs.StartRecord( file );
	}

	return false;
}

qboolean DEM_StopRecord( file_t *file )
{
	if( !cls.demorecording )
	{
		return false;
	}

	if( demo_handler && demo_handler->funcs.StopRecord )
	{
		return demo_handler->funcs.StopRecord( file );
	}

	return false;
}

qboolean DEM_StartPlayback( file_t *file )
{
	if( cls.demoplayback )
	{
		return false;
	}

	demo_handler = DEM_FindPlaybackHandler( file );

	if( demo_handler && demo_handler->funcs.StartPlayback )
	{
		return demo_handler->funcs.StartPlayback( file );
	}

	return false;
}

qboolean DEM_StopPlayback( file_t *file )
{
	if( cls.demoplayback )
	{
		return false;
	}

	if( demo_handler && demo_handler->funcs.StopPlayback )
	{
		return demo_handler->funcs.StopPlayback( file );
	}

	return false;
}

qboolean DEM_ListDemo( file_t *file )
{
	demo_handler = DEM_FindPlaybackHandler( file );

	if( !demo_handler )
	{
		return false;
	}

	if( demo_handler && demo_handler->funcs.ListDemo )
	{
		return demo_handler->funcs.ListDemo( file );
	}

	return false;
}

void DEM_WriteDemoUserCmd( int cmdnumber )
{
	if( !demo_handler || !demo_handler->funcs.WriteUserCmd )
	{
		return;
	}

	demo_handler->funcs.WriteUserCmd( cmdnumber );
}

void DEM_WriteNetPacket( qboolean startup, int start, sizebuf_t *msg )
{
	if( !DEM_FindRecordHandler())
	{
		return;
	}

	if( !demo_handler || !demo_handler->funcs.WriteNetPacket )
	{
		return;
	}

	demo_handler->funcs.WriteNetPacket( startup, start, msg );
}

void DEM_WriteDemoUserMessage( int size, byte *buffer )
{
	if( !demo_handler || !demo_handler->funcs.WriteDemoMesssage )
	{
		return;
	}

	demo_handler->funcs.WriteDemoMesssage( size, buffer );
}

void DEM_WriteAnim( int anim, int body )
{
	if( !demo_handler || !demo_handler->funcs.WriteAnim )
	{
		return;
	}

	demo_handler->funcs.WriteAnim( anim, body );
}

void DEM_WriteClientData( client_data_t *cdata )
{
	if( !demo_handler || !demo_handler->funcs.WriteClientData )
	{
		return;
	}

	demo_handler->funcs.WriteClientData( cdata );
}

void DEM_WriteEvent( int flags, int idx, float delay, event_args_t *pargs )
{
	if( !demo_handler || !demo_handler->funcs.WriteEvent )
	{
		return;
	}

	demo_handler->funcs.WriteEvent( flags, idx, delay, pargs );
}

void DEM_WriteSound( int channel, const char *sample, float vol, float attenuation, int flags, int pitch )
{
	if( !demo_handler || !demo_handler->funcs.WriteSound )
	{
		return;
	}

	demo_handler->funcs.WriteSound( channel, sample, vol, attenuation, flags, pitch );
}

void DEM_WriteStringCmd( const char *cmd )
{
	if( !demo_handler || !demo_handler->funcs.WriteStringCMD )
	{
		return;
	}

	demo_handler->funcs.WriteStringCMD( cmd );
}

qboolean DEM_DemoReadMessage( byte *buffer, size_t *length )
{
	if( !demo_handler || !demo_handler->funcs.ReadDemoMessage )
	{
		return false;
	}

	return demo_handler->funcs.ReadDemoMessage( buffer, length );
}

double DEM_GetHostFPS( void )
{
	if( !demo_handler || !demo_handler->funcs.GetHostFPS )
	{
		return 0.0;
	}

	return demo_handler->funcs.GetHostFPS();
}

void DEM_DemoInterpolateAngles( void )
{
	if( !demo_handler || !demo_handler->funcs.InterpolateAngles )
	{
		return;
	}

	demo_handler->funcs.InterpolateAngles();
}
