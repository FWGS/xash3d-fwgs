/*
sys_sailfish.c - Sailfish OS system integration
Copyright (C) 2026 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <gio/gio.h>
#include <audioresource.h>
#include <SDL.h>
#include "common.h"
#include "client.h"
#include "input.h"
#include "platform/platform.h"

// MCE forgets the request after 60 seconds
#define BLANKING_PAUSE_INTERVAL 45.0

static struct
{
	GDBusConnection *system_bus;
	audioresource_t *audio;
	qboolean active;
	qboolean audio_lost;
	qboolean gestures_overridden;
	double next_blanking_pause;
} s_sailfish;

static void Sailfish_RequestMCE( const char *method )
{
	if( !s_sailfish.system_bus )
		return;

	// no callback means no reply is expected
	g_dbus_connection_call( s_sailfish.system_bus, "com.nokia.mce", "/com/nokia/mce/request", "com.nokia.mce.request", method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL );
}

static void Sailfish_UpdateSystemGestures( void )
{
	// edge swipes are the only way to leave the app, so only block them while playing with touch controls
	qboolean overridden = cls.key_dest == key_game && cls.state == ca_active && touch_enable.value;

	if( s_sailfish.gestures_overridden == overridden )
		return;

	s_sailfish.gestures_overridden = overridden;

	// SDL watches this hint and updates the window flags on the fly
	SDL_SetHint( SDL_HINT_QTWAYLAND_WINDOW_FLAGS, overridden ? "OverridesSystemGestures" : "" );
}

static void Sailfish_AudioResourceCallback( audioresource_t *audio, bool acquired, void *user_data )
{
	Con_Reportf( "%s: audio resource %s\n", __func__, acquired ? "acquired" : "lost" );

	// writes to the stream block while the audio policy doesn't let us play
	s_sailfish.audio_lost = !acquired;
	SNDDMA_Activate( acquired );
}

/*
========================
Sailfish_SetActive

========================
*/
void Sailfish_SetActive( qboolean active )
{
	if( s_sailfish.active == active )
		return;

	s_sailfish.active = active;
	Sailfish_SuspendTextInput( !active );

	if( active )
		s_sailfish.next_blanking_pause = 0.0;
	else
		Sailfish_RequestMCE( "req_display_cancel_blanking_pause" );
}

/*
========================
Sailfish_Init

========================
*/
void Sailfish_Init( void )
{
	GError *error = NULL;

	s_sailfish.system_bus = g_bus_get_sync( G_BUS_TYPE_SYSTEM, NULL, &error );

	if( !s_sailfish.system_bus )
	{
		Con_Printf( S_ERROR "%s: can't connect to the system bus: %s\n", __func__, error->message );
		g_error_free( error );
	}

	s_sailfish.audio = audioresource_init( AUDIO_RESOURCE_GAME, Sailfish_AudioResourceCallback, NULL );

	if( s_sailfish.audio )
		audioresource_acquire( s_sailfish.audio );
}

/*
========================
Sailfish_Shutdown

========================
*/
void Sailfish_Shutdown( void )
{
	Sailfish_SetActive( false );
	Sailfish_ShutdownTextInput();

	if( s_sailfish.audio )
	{
		audioresource_release( s_sailfish.audio );
		audioresource_free( s_sailfish.audio );
		s_sailfish.audio = NULL;
	}

	if( s_sailfish.system_bus )
	{
		g_dbus_connection_flush_sync( s_sailfish.system_bus, NULL, NULL );
		g_object_unref( s_sailfish.system_bus );
		s_sailfish.system_bus = NULL;
	}
}

/*
========================
Sailfish_RunEvents

========================
*/
void Sailfish_RunEvents( void )
{
	// libaudioresource callbacks are dispatched by the default GLib main context
	while( g_main_context_iteration( NULL, FALSE ))
		;

	Sailfish_UpdateSystemGestures();

	if( s_sailfish.active && host.realtime >= s_sailfish.next_blanking_pause )
	{
		Sailfish_RequestMCE( "req_display_blanking_pause" );
		s_sailfish.next_blanking_pause = host.realtime + BLANKING_PAUSE_INTERVAL;
	}
}

#if XASH_MESSAGEBOX == MSGBOX_SAILFISH
/*
========================
Platform_MessageBox

========================
*/
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	extern char **environ;
	static const char engine_env[] = "XASH3D_SAILFISH_ENGINE=";
	char exe[MAX_OSPATH];
	char *argv[] = { exe, (char *)title, (char *)message, NULL };
	ssize_t len = readlink( "/proc/self/exe", exe, sizeof( exe ) - 1 );

	if( len < 0 )
		return;

	exe[len] = 0;

	// the launcher shows it from a separate process, it doesn't depend on our renderer or EGL state
	int count = 0;

	while( environ[count] )
		count++;

	char **envp = malloc(( count + 2 ) * sizeof( *envp ));
	int envc = 0;

	if( !envp )
		return;

	for( int i = 0; i < count; i++ )
	{
		if( Q_strncmp( environ[i], engine_env, sizeof( engine_env ) - 1 ))
			envp[envc++] = environ[i];
	}

	envp[envc++] = (char *)"XASH3D_SAILFISH_MESSAGEBOX=1";
	envp[envc] = NULL;

	pid_t pid;
	int ret = posix_spawn( &pid, exe, NULL, NULL, argv, envp );

	free( envp );

	if( ret != 0 )
	{
		fprintf( stderr, "%s: can't start %s: %s\n", __func__, exe, strerror( ret ));
		return;
	}

	// the audio thread would loop the last buffer while we wait
	SNDDMA_Activate( false );
	Sailfish_SuspendTextInput( true );

	while( waitpid( pid, NULL, 0 ) < 0 && errno == EINTR )
		;

	// the engine quits after a fatal error, the keyboard would flash up before that
	if( host.status != HOST_ERR_FATAL )
		Sailfish_SuspendTextInput( false );

	SNDDMA_Activate( !s_sailfish.audio_lost );
}
#endif // XASH_MESSAGEBOX == MSGBOX_SAILFISH
