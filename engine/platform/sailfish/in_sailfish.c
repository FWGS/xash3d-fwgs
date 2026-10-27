/*
in_sailfish.c - Sailfish OS on-screen keyboard
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
#include <gio/gio.h>
#include <SDL.h>
#include "common.h"
#include "client.h"
#include "platform/platform.h"

// Lipstick has no Wayland text input protocol, so SDL can't show the keyboard. maliit, the keyboard
// server, is reached over its peer-to-peer D-Bus protocol instead, the same way its Qt input context does
#define MALIIT_SERVER_PATH  "/com/meego/inputmethod/uiserver1"
#define MALIIT_SERVER_IFACE "com.meego.inputmethod.uiserver1"
#define MALIIT_CONTEXT_PATH "/com/meego/inputmethod/inputcontext"

// QEvent::Type
#define QT_KEY_PRESS 6

// Maliit::EventRequestType
#define MALIIT_EVENT_REQUEST_SIGNAL_ONLY 1

// Qt::InputMethodHints
#define QT_IMH_NO_AUTO_UPPERCASE 0x2
#define QT_IMH_NO_PREDICTIVE_TEXT 0x40

// maliit-framework dbus_interfaces/minputmethodcontext1interface.xml
static const char s_context_xml[] =
"<node>"
"<interface name='com.meego.inputmethod.inputcontext1'>"
"<method name='activationLostEvent'/>"
"<method name='imInitiatedHide'/>"
"<method name='commitString'><arg type='s'/><arg type='i'/><arg type='i'/><arg type='i'/></method>"
"<method name='updatePreedit'><arg type='s'/><arg type='a(iii)'/><arg type='i'/><arg type='i'/><arg type='i'/></method>"
"<method name='keyEvent'><arg type='i'/><arg type='i'/><arg type='i'/><arg type='s'/><arg type='b'/><arg type='i'/><arg type='y'/></method>"
"<method name='updateInputMethodArea'><arg type='i'/><arg type='i'/><arg type='i'/><arg type='i'/></method>"
"<method name='setGlobalCorrectionEnabled'><arg type='b'/></method>"
"<method name='preeditRectangle'><arg type='b' direction='out'/><arg type='i' direction='out'/><arg type='i' direction='out'/><arg type='i' direction='out'/><arg type='i' direction='out'/></method>"
"<method name='setRedirectKeys'><arg type='b'/></method>"
"<method name='setDetectableAutoRepeat'><arg type='b'/></method>"
"<method name='setSelection'><arg type='i'/><arg type='i'/></method>"
"<method name='selection'><arg type='b' direction='out'/><arg type='s' direction='out'/></method>"
"<method name='setLanguage'><arg type='s'/></method>"
"<method name='notifyExtendedAttributeChanged'><arg type='i'/><arg type='s'/><arg type='s'/><arg type='s'/><arg type='v'/></method>"
"<method name='pluginSettingsLoaded'><arg type='a(sssia(ssibva{sv}))'/></method>"
"</interface>"
"</node>";

static struct
{
	GDBusConnection *connection;
	GDBusNodeInfo *node;
	guint registration;
	qboolean activated;
	qboolean enabled;
} s_maliit;

static void Maliit_PushText( const char *text )
{
	SDL_Event event = { .type = SDL_TEXTINPUT };

	event.text.windowID = host.hWnd ? SDL_GetWindowID( host.hWnd ) : 0;

	// SDL text events are short, split the string on UTF-8 sequence boundaries
	while( *text )
	{
		size_t len = 0;

		while( text[len] )
		{
			size_t next = len + 1;

			while(( text[next] & 0xC0 ) == 0x80 )
				next++;

			if( next >= sizeof( event.text.text ))
				break;

			len = next;
		}

		if( !len )
			break;

		memcpy( event.text.text, text, len );
		event.text.text[len] = 0;
		SDL_PushEvent( &event );
		text += len;
	}
}

static void Maliit_PushKey( int type, int key, const char *text )
{
	// Qt::Key values the keyboard sends as key events
	static const struct
	{
		int key;
		SDL_Scancode scancode;
	} keys[] =
	{
		{ 0x01000000, SDL_SCANCODE_ESCAPE },
		{ 0x01000001, SDL_SCANCODE_TAB },
		{ 0x01000003, SDL_SCANCODE_BACKSPACE },
		{ 0x01000004, SDL_SCANCODE_RETURN },
		{ 0x01000005, SDL_SCANCODE_RETURN },
		{ 0x01000007, SDL_SCANCODE_DELETE },
		{ 0x01000010, SDL_SCANCODE_HOME },
		{ 0x01000011, SDL_SCANCODE_END },
		{ 0x01000012, SDL_SCANCODE_LEFT },
		{ 0x01000013, SDL_SCANCODE_UP },
		{ 0x01000014, SDL_SCANCODE_RIGHT },
		{ 0x01000015, SDL_SCANCODE_DOWN },
		{ 0x01000016, SDL_SCANCODE_PAGEUP },
		{ 0x01000017, SDL_SCANCODE_PAGEDOWN },
	};

	for( int i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( keys[i].key != key )
			continue;

		SDL_Event event = { .type = type == QT_KEY_PRESS ? SDL_KEYDOWN : SDL_KEYUP };

		event.key.windowID = host.hWnd ? SDL_GetWindowID( host.hWnd ) : 0;
		event.key.state = type == QT_KEY_PRESS ? SDL_PRESSED : SDL_RELEASED;
		event.key.keysym.scancode = keys[i].scancode;
		event.key.keysym.sym = SDL_GetKeyFromScancode( keys[i].scancode );
		SDL_PushEvent( &event );
		return;
	}

	// characters normally come through commitString
	if( type == QT_KEY_PRESS && !COM_StringEmpty( text ))
		Maliit_PushText( text );
}

static void Maliit_MethodCall( GDBusConnection *connection, const char *sender, const char *path, const char *iface, const char *method, GVariant *params, GDBusMethodInvocation *invocation, gpointer user_data )
{
	// called from Sailfish_RunEvents, which iterates the main context on the engine thread
	if( !Q_strcmp( method, "commitString" ))
	{
		const char *text;

		g_variant_get( params, "(&siii)", &text, NULL, NULL, NULL );
		Maliit_PushText( text );
	}
	else if( !Q_strcmp( method, "keyEvent" ))
	{
		int type, key;
		const char *text;
		guchar request;

		g_variant_get( params, "(iii&sbiy)", &type, &key, NULL, &text, NULL, NULL, &request );

		if( request != MALIIT_EVENT_REQUEST_SIGNAL_ONLY )
			Maliit_PushKey( type, key, text );
	}
	else if( !Q_strcmp( method, "updateInputMethodArea" ))
	{
		int x, y, w, h;

		g_variant_get( params, "(iiii)", &x, &y, &w, &h );
		Con_Reportf( "%s: keyboard area %d %d %dx%d\n", __func__, x, y, w, h );
	}
	else if( !Q_strcmp( method, "activationLostEvent" ))
	{
		s_maliit.activated = false;
	}
	else if( !Q_strcmp( method, "preeditRectangle" ))
	{
		g_dbus_method_invocation_return_value( invocation, g_variant_new( "(biiii)", FALSE, 0, 0, 0, 0 ));
		return;
	}
	else if( !Q_strcmp( method, "selection" ))
	{
		g_dbus_method_invocation_return_value( invocation, g_variant_new( "(bs)", FALSE, "" ));
		return;
	}

	g_dbus_method_invocation_return_value( invocation, NULL );
}

static const GDBusInterfaceVTable s_context_vtable =
{
	.method_call = Maliit_MethodCall,
};

static void Maliit_Disconnect( void )
{
	if( !s_maliit.connection )
		return;

	if( s_maliit.registration )
		g_dbus_connection_unregister_object( s_maliit.connection, s_maliit.registration );

	if( !g_dbus_connection_is_closed( s_maliit.connection ))
		g_dbus_connection_close_sync( s_maliit.connection, NULL, NULL );

	g_object_unref( s_maliit.connection );
	s_maliit.connection = NULL;
	s_maliit.registration = 0;
	s_maliit.activated = false;
}

static qboolean Maliit_Connect( void )
{
	GError *error = NULL;

	// maliit-server restarts with the keyboard settings
	if( s_maliit.connection && g_dbus_connection_is_closed( s_maliit.connection ))
		Maliit_Disconnect();

	if( s_maliit.connection )
		return true;

	GDBusConnection *session = g_bus_get_sync( G_BUS_TYPE_SESSION, NULL, &error );

	if( !session )
	{
		Con_Printf( S_ERROR "%s: can't connect to the session bus: %s\n", __func__, error->message );
		g_error_free( error );
		return false;
	}

	// the shared session bus connection would exit the process when it's closed
	g_dbus_connection_set_exit_on_close( session, FALSE );

	GVariant *reply = g_dbus_connection_call_sync( session, "org.maliit.server", "/org/maliit/server/address", "org.freedesktop.DBus.Properties", "Get",
		g_variant_new( "(ss)", "org.maliit.Server.Address", "address" ), G_VARIANT_TYPE( "(v)" ), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error );

	g_object_unref( session );

	if( !reply )
	{
		Con_Printf( S_ERROR "%s: can't get the keyboard server address: %s\n", __func__, error->message );
		g_error_free( error );
		return false;
	}

	GVariant *address;

	g_variant_get( reply, "(v)", &address );

	if( g_variant_is_of_type( address, G_VARIANT_TYPE_STRING ))
		s_maliit.connection = g_dbus_connection_new_for_address_sync( g_variant_get_string( address, NULL ), G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL, &error );

	g_variant_unref( address );
	g_variant_unref( reply );

	if( !s_maliit.connection )
	{
		Con_Printf( S_ERROR "%s: can't connect to the keyboard server: %s\n", __func__, error ? error->message : "bad address" );

		if( error )
			g_error_free( error );

		return false;
	}

	if( !s_maliit.node )
		s_maliit.node = g_dbus_node_info_new_for_xml( s_context_xml, NULL );

	s_maliit.registration = g_dbus_connection_register_object( s_maliit.connection, MALIIT_CONTEXT_PATH, s_maliit.node->interfaces[0], &s_context_vtable, NULL, NULL, &error );

	if( !s_maliit.registration )
	{
		Con_Printf( S_ERROR "%s: can't register the input context: %s\n", __func__, error->message );
		g_error_free( error );
		Maliit_Disconnect();
		return false;
	}

	return true;
}

static void Maliit_Call( const char *method, GVariant *params )
{
	// no callback, so no reply is expected
	g_dbus_connection_call( s_maliit.connection, NULL, MALIIT_SERVER_PATH, MALIIT_SERVER_IFACE, method, params, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL );
}

static GVariant *Maliit_WidgetInformation( qboolean focus )
{
	GVariantBuilder info;

	g_variant_builder_init( &info, G_VARIANT_TYPE_VARDICT );
	g_variant_builder_add( &info, "{sv}", "focusState", g_variant_new_boolean( focus ));

	if( focus )
	{
		const char *text = Key_GetTextInputBuffer();
		int len = text ? g_utf8_strlen( text, -1 ) : 0;

		// console commands and player names, not prose
		g_variant_builder_add( &info, "{sv}", "contentType", g_variant_new_int32( 0 ));
		g_variant_builder_add( &info, "{sv}", "autocapitalizationEnabled", g_variant_new_boolean( FALSE ));
		g_variant_builder_add( &info, "{sv}", "predictionEnabled", g_variant_new_boolean( FALSE ));
		g_variant_builder_add( &info, "{sv}", "hiddenText", g_variant_new_boolean( FALSE ));
		g_variant_builder_add( &info, "{sv}", "maliit-inputmethod-hints", g_variant_new_int32( QT_IMH_NO_AUTO_UPPERCASE | QT_IMH_NO_PREDICTIVE_TEXT ));
		g_variant_builder_add( &info, "{sv}", "enterKeyType", g_variant_new_int32( 0 ));
		g_variant_builder_add( &info, "{sv}", "surroundingText", g_variant_new_string( text ? text : "" ));
		g_variant_builder_add( &info, "{sv}", "cursorPosition", g_variant_new_int32( len ));
		g_variant_builder_add( &info, "{sv}", "anchorPosition", g_variant_new_int32( len ));
		g_variant_builder_add( &info, "{sv}", "hasSelection", g_variant_new_boolean( FALSE ));
		g_variant_builder_add( &info, "{sv}", "toolbarId", g_variant_new_int32( 0 ));
	}

	return g_variant_new( "(a{sv}b)", &info, TRUE );
}

static void Maliit_Hide( void )
{
	if( !s_maliit.connection || g_dbus_connection_is_closed( s_maliit.connection ))
		return;

	Maliit_Call( "updateWidgetInformation", Maliit_WidgetInformation( false ));
	Maliit_Call( "hideInputMethod", NULL );
}

static void Maliit_Show( void )
{
	if( !Maliit_Connect( ))
		return;

	if( !s_maliit.activated )
	{
		Maliit_Call( "activateContext", NULL );
		s_maliit.activated = true;
	}

	Maliit_Call( "appOrientationChanged", g_variant_new( "(i)", Sailfish_OrientationAngle( )));
	Maliit_Call( "updateWidgetInformation", Maliit_WidgetInformation( true ));
	Maliit_Call( "showInputMethod", NULL );
}

/*
========================
Sailfish_EnableTextInput

========================
*/
void Sailfish_EnableTextInput( qboolean enable, int x, int y, int w, int h )
{
	s_maliit.enabled = enable;
	Sailfish_SetTextInputArea( enable ? y : 0, enable ? h : 0 );

	if( enable )
		Maliit_Show();
	else
		Maliit_Hide();
}

/*
========================
Sailfish_SuspendTextInput

========================
*/
void Sailfish_SuspendTextInput( qboolean suspend )
{
	if( !s_maliit.enabled )
		return;

	if( suspend )
	{
		// the keyboard stays on top of other windows otherwise
		Maliit_Hide();

		// and another window may take the input context over meanwhile
		s_maliit.activated = false;
	}
	else
	{
		Maliit_Show();
	}
}

/*
========================
Sailfish_ShutdownTextInput

========================
*/
void Sailfish_ShutdownTextInput( void )
{
	Maliit_Disconnect();

	if( s_maliit.node )
	{
		g_dbus_node_info_unref( s_maliit.node );
		s_maliit.node = NULL;
	}
}
