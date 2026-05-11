#include "extdll_menu.h"
#include "keydefs.h"

#include <cstdio>
#include <string>
#include <vector>
#include <map>

namespace
{
enum WidgetType
{
	WIDGET_PANEL,
	WIDGET_LABEL,
	WIDGET_BUTTON,
	WIDGET_IMAGE,
	WIDGET_TEXTBOX
};

struct Color4
{
	int r, g, b, a;
};

struct Rect
{
	int x, y, w, h;
};

static Rect MakeRect( int x, int y, int w, int h )
{
	Rect rect = { x, y, w, h };
	return rect;
}

static Color4 MakeColor( int r, int g, int b, int a )
{
	Color4 color = { r, g, b, a };
	return color;
}

struct ResourceNode
{
	std::string name;
	std::map<std::string, std::string> props;
	std::vector<ResourceNode> children;
};

struct Widget
{
	WidgetType type;
	std::string name;
	std::string text;
	std::string command;
	std::string image;
	std::string xpos;
	std::string ypos;
	std::string wide;
	std::string tall;
	Color4 fg;
	Color4 bg;
	Color4 armed;
	Rect rect;
	bool visible;
	bool fill;
	bool border;
	bool centered;
	int texture;
	std::vector<Widget> children;
};

struct UiState
{
	enum Page
	{
		PAGE_MAIN,
		PAGE_SERVERS
	};

	ui_enginefuncs_t engfuncs;
	ui_extendedfuncs_t extfuncs;
	ui_globalvars_t *globals;
	std::vector<Widget> root;
	bool initialized;
	bool visible;
	bool mouseDown;
	bool mouseCaptured;
	int mouseX;
	int mouseY;
	int hoveredButton;
	int lastHoveredButton;
	int activeButton;
	int screenW;
	int screenH;
	int selectedServer;
	int serverTab;
	Page page;
	int serverWindowX;
	int serverWindowY;
	int motdWindowX;
	int motdWindowY;
	int dragOffsetX;
	int dragOffsetY;
	int dragTarget;
	std::string motdText;
	std::string motdSource;
	bool motdVisible;
} g_ui = {};

enum
{
	SERVERTAB_DIRECT,
	SERVERTAB_FAVORITES,
	SERVERTAB_HISTORY,
	SERVERTAB_LAN,
	SERVERTAB_NAT,
	SERVERTAB_COUNT
};

enum
{
	DRAG_NONE,
	DRAG_SERVER,
	DRAG_MOTD
};

struct ServerEntry
{
	std::string name;
	std::string map;
	std::string players;
	std::string ping;
	std::string address;
	std::string rawInfo;
	bool favorite;
	bool history;
	bool lan;
	bool nat;
};

static std::vector<ServerEntry> g_servers;
static std::vector<std::string> g_favorites;
static std::vector<std::string> g_history;

static void AddOrUpdateServer( struct netadr_s adr, const char *info );

static int Clamp255( int value )
{
	if( value < 0 ) return 0;
	if( value > 255 ) return 255;
	return value;
}

static void PlayUISound( const char *sound )
{
	if( sound && sound[0] )
		g_ui.engfuncs.pfnPlayLocalSound( sound );
}

static void PlayHoverSound( void )
{
	PlayUISound( "media/launch_glow1.wav" );
}

static void PlayClickSound( void )
{
	PlayUISound( "media/launch_select2.wav" );
}

static Color4 ParseColor( const std::string &text, int ar, int ag, int ab, int aa )
{
	Color4 out = { ar, ag, ab, aa };
	int r, g, b, a;

	if( sscanf( text.c_str(), "%d %d %d %d", &r, &g, &b, &a ) == 4 )
	{
		out.r = Clamp255( r );
		out.g = Clamp255( g );
		out.b = Clamp255( b );
		out.a = Clamp255( a );
	}
	else if( sscanf( text.c_str(), "%d %d %d", &r, &g, &b ) == 3 )
	{
		out.r = Clamp255( r );
		out.g = Clamp255( g );
		out.b = Clamp255( b );
	}

	return out;
}

static bool IsIdentChar( char ch )
{
	return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '#' || ch == '.' || ch == '/' || ch == '\\' || ch == '%';
}

static bool NextToken( const char *&data, std::string &out )
{
	out.clear();

	while( *data && isspace((unsigned char)*data ))
		data++;

	if( !*data )
		return false;

	if( data[0] == '/' && data[1] == '/' )
	{
		while( *data && *data != '\n' )
			data++;
		return NextToken( data, out );
	}

	if( *data == '"' )
	{
		data++;

		while( *data && *data != '"' )
		{
			if( *data == '\\' && data[1] )
				data++;

			out.push_back( *data++ );
		}

		if( *data == '"' )
			data++;

		return true;
	}

	if( *data == '{' || *data == '}' )
	{
		out.assign( 1, *data++ );
		return true;
	}

	while( *data && IsIdentChar( *data ))
		out.push_back( *data++ );

	return !out.empty();
}

static bool ParseResourceBlock( const char *&data, ResourceNode &node )
{
	std::string token;

	while( NextToken( data, token ))
	{
		if( token == "}" )
			return true;

		std::string value;
		if( !NextToken( data, value ))
			return false;

		if( value == "{" )
		{
			ResourceNode child;
			child.name = token;

			if( !ParseResourceBlock( data, child ))
				return false;

			node.children.push_back( child );
		}
		else
		{
			node.props[token] = value;
		}
	}

	return true;
}

static bool ParseResourceFile( const char *filename, ResourceNode &out )
{
	int length = 0;
	byte *raw = g_ui.engfuncs.COM_LoadFile( filename, &length );

	if( !raw || length <= 0 )
		return false;

	std::string token;
	const char *data = (const char *)raw;

	if( !NextToken( data, token ))
	{
		g_ui.engfuncs.COM_FreeFile( raw );
		return false;
	}

	out.name = token;

	if( !NextToken( data, token ) || token != "{" )
	{
		g_ui.engfuncs.COM_FreeFile( raw );
		return false;
	}

	const bool parsed = ParseResourceBlock( data, out );
	g_ui.engfuncs.COM_FreeFile( raw );
	return parsed;
}

static std::string Trim( const std::string &value )
{
	size_t start = 0;
	size_t end = value.size();

	while( start < value.size() && isspace((unsigned char)value[start] ))
		start++;
	while( end > start && isspace((unsigned char)value[end - 1] ))
		end--;

	return value.substr( start, end - start );
}

static const char *InfoValueForKey( const char *info, const char *key )
{
	static char value[256];
	const char *p = info;

	value[0] = '\0';

	while( p && *p )
	{
		const char *kstart;
		const char *vstart;
		size_t klen;
		size_t vlen;

		if( *p == '\\' )
			p++;

		kstart = p;
		while( *p && *p != '\\' )
			p++;
		klen = p - kstart;

		if( *p == '\\' )
			p++;

		vstart = p;
		while( *p && *p != '\\' )
			p++;
		vlen = p - vstart;

		if( strlen( key ) == klen && !strncmp( kstart, key, klen ) )
		{
			if( vlen >= sizeof( value ))
				vlen = sizeof( value ) - 1;
			memcpy( value, vstart, vlen );
			value[vlen] = '\0';
			return value;
		}
	}

	return "";
}

static std::string GetProp( const ResourceNode &node, const char *name, const char *fallback )
{
	std::map<std::string, std::string>::const_iterator it = node.props.find( name );
	if( it != node.props.end() )
		return it->second;
	return fallback ? fallback : "";
}

static WidgetType ParseWidgetType( const std::string &value )
{
	if( !stricmp( value.c_str(), "Label" )) return WIDGET_LABEL;
	if( !stricmp( value.c_str(), "Button" )) return WIDGET_BUTTON;
	if( !stricmp( value.c_str(), "ImagePanel" )) return WIDGET_IMAGE;
	if( !stricmp( value.c_str(), "RichText" ) || !stricmp( value.c_str(), "TextEntry" )) return WIDGET_TEXTBOX;
	return WIDGET_PANEL;
}

static Widget BuildWidget( const ResourceNode &node )
{
	Widget widget;
	const std::string labelText = GetProp( node, "labelText", "" );
	const std::string textValue = GetProp( node, "text", node.name.c_str() );
	const std::string imageValue = GetProp( node, "image", GetProp( node, "imageName", "" ).c_str() );

	widget.type = ParseWidgetType( GetProp( node, "ControlName", "Panel" ));
	widget.name = node.name;
	widget.text = labelText.empty() ? textValue : labelText;
	widget.command = GetProp( node, "command", "" );
	widget.image = imageValue;
	widget.xpos = GetProp( node, "xpos", "0" );
	widget.ypos = GetProp( node, "ypos", "0" );
	widget.wide = GetProp( node, "wide", "0" );
	widget.tall = GetProp( node, "tall", "0" );
	widget.fg = ParseColor( GetProp( node, "fgcolor", "255 220 160 255" ), 255, 220, 160, 255 );
	widget.bg = ParseColor( GetProp( node, "bgcolor", "18 22 26 196" ), 18, 22, 26, 196 );
	widget.armed = ParseColor( GetProp( node, "armedfgcolor", "255 255 255 255" ), 255, 255, 255, 255 );
	widget.visible = stricmp( GetProp( node, "visible", "1" ).c_str(), "0" ) != 0;
	widget.fill = stricmp( GetProp( node, "paintbackground", "1" ).c_str(), "0" ) != 0;
	widget.border = stricmp( GetProp( node, "paintborder", "1" ).c_str(), "0" ) != 0;
	widget.centered = stricmp( GetProp( node, "textAlignment", "west" ).c_str(), "center" ) == 0;
	widget.texture = 0;
	widget.rect.x = widget.rect.y = widget.rect.w = widget.rect.h = 0;

	for( size_t i = 0; i < node.children.size(); ++i )
		widget.children.push_back( BuildWidget( node.children[i] ));

	return widget;
}

static int ParseMetric( const std::string &value, int parentSize, int selfSize, bool forPos )
{
	if( value.empty() )
		return 0;

	if( value[0] == 'c' && forPos )
	{
		int offset = 0;
		if( value.size() > 1 )
			offset = atoi( value.c_str() + 1 );
		return ( parentSize - selfSize ) / 2 + offset;
	}

	if(( value[0] == 'r' || value[0] == 'b' ) && forPos )
		return parentSize - selfSize - atoi( value.c_str() + 1 );

	return atoi( value.c_str() );
}

static int ScaleX( int value )
{
	return value * g_ui.screenW / 640;
}

static int ScaleY( int value )
{
	return value * g_ui.screenH / 480;
}

static void LayoutWidget( Widget &widget, const Rect &parent )
{
	const int wide = ScaleX( atoi( widget.wide.c_str() ));
	const int tall = ScaleY( atoi( widget.tall.c_str() ));

	widget.rect.w = wide;
	widget.rect.h = tall;
	widget.rect.x = parent.x + ScaleX( ParseMetric( widget.xpos, 640, atoi( widget.wide.c_str() ), true ));
	widget.rect.y = parent.y + ScaleY( ParseMetric( widget.ypos, 480, atoi( widget.tall.c_str() ), true ));

	for( size_t i = 0; i < widget.children.size(); ++i )
		LayoutWidget( widget.children[i], widget.rect );
}

static const char *LocalizeText( const std::string &text )
{
	if( text.empty() )
		return "";

	if( text[0] == '#' )
		return text.c_str() + 1;

	return text.c_str();
}

static bool PointInRect( int x, int y, const Rect &rect )
{
	return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

static void DrawTextInRect( const Rect &rect, Color4 color, const char *text, bool centered )
{
	int width = 0, height = 0;
	int x = rect.x + ScaleX( 8 );
	int y = rect.y + ( rect.h - 8 ) / 2;

	g_ui.engfuncs.pfnDrawConsoleStringLen( text, &width, &height );

	if( centered )
		x = rect.x + ( rect.w - width ) / 2;

	g_ui.engfuncs.pfnDrawSetTextColor( color.r, color.g, color.b, color.a );
	g_ui.engfuncs.pfnDrawConsoleString( x, y, text );
}

static void SaveAddressList( const char *filename, const std::vector<std::string> &list )
{
	std::string text;

	for( size_t i = 0; i < list.size(); ++i )
	{
		text += list[i];
		text += "\n";
	}

	g_ui.engfuncs.COM_SaveFile( filename, text.c_str(), (int)text.size() );
}

static void LoadAddressList( const char *filename, std::vector<std::string> &list )
{
	int length = 0;
	byte *raw = g_ui.engfuncs.COM_LoadFile( filename, &length );

	list.clear();

	if( !raw || length <= 0 )
		return;

	std::string text((const char *)raw, length );
	g_ui.engfuncs.COM_FreeFile( raw );

	size_t start = 0;
	while( start < text.size() )
	{
		size_t end = text.find( '\n', start );
		std::string row = Trim( text.substr( start, end == std::string::npos ? std::string::npos : end - start ));
		if( !row.empty() )
			list.push_back( row );
		if( end == std::string::npos )
			break;
		start = end + 1;
	}
}

static bool AddressInList( const std::vector<std::string> &list, const std::string &address )
{
	for( size_t i = 0; i < list.size(); ++i )
	{
		if( !stricmp( list[i].c_str(), address.c_str() ))
			return true;
	}

	return false;
}

static std::vector<ServerEntry*> CollectVisibleServers( void )
{
	std::vector<ServerEntry*> out;

	for( size_t i = 0; i < g_servers.size(); ++i )
	{
		ServerEntry &server = g_servers[i];
		bool visible = false;

		switch( g_ui.serverTab )
		{
		case SERVERTAB_DIRECT: visible = !server.lan && !server.nat; break;
		case SERVERTAB_FAVORITES: visible = server.favorite; break;
		case SERVERTAB_HISTORY: visible = server.history; break;
		case SERVERTAB_LAN: visible = server.lan; break;
		case SERVERTAB_NAT: visible = server.nat; break;
		default: break;
		}

		if( visible )
			out.push_back( &server );
	}

	return out;
}

static void AddToHistory( const std::string &address )
{
	for( size_t i = 0; i < g_history.size(); ++i )
	{
		if( !stricmp( g_history[i].c_str(), address.c_str() ))
		{
			g_history.erase( g_history.begin() + i );
			break;
		}
	}

	g_history.push_back( address );
	if( g_history.size() > 20 )
		g_history.erase( g_history.begin() );

	SaveAddressList( "history_servers.lst", g_history );
}

static void RefreshServers( void )
{
	g_servers.clear();
	g_ui.selectedServer = 0;

	switch( g_ui.serverTab )
	{
	case SERVERTAB_DIRECT:
		g_ui.engfuncs.pfnClientCmd( 0, "internetservers\n" );
		break;
	case SERVERTAB_LAN:
		g_ui.engfuncs.pfnClientCmd( 0, "localservers\n" );
		break;
	case SERVERTAB_NAT:
		g_ui.engfuncs.pfnClientCmd( 0, "internetservers\n" );
		break;
	default:
		break;
	}
}

static void OpenServersPage( int tab )
{
	g_ui.page = UiState::PAGE_SERVERS;
	g_ui.visible = true;
	g_ui.serverTab = tab;
	if( g_ui.serverWindowX <= 0 )
	{
		g_ui.serverWindowX = ScaleX( 24 );
		g_ui.serverWindowY = ScaleY( 40 );
	}
	RefreshServers();
}

static void DrawServerBrowser( void )
{
	static const char *tabs[SERVERTAB_COUNT] = { "Direct", "Favorites", "History", "LAN", "NAT" };
	const int x = g_ui.serverWindowX > 0 ? g_ui.serverWindowX : ScaleX( 24 );
	const int y = g_ui.serverWindowY > 0 ? g_ui.serverWindowY : ScaleY( 40 );
	const int w = ScaleX( 592 );
	const int h = ScaleY( 400 );
	const int tabW = ScaleX( 96 );
	const int rowY = y + ScaleY( 70 );
	const int rowH = ScaleY( 22 );
	const std::vector<ServerEntry*> visible = CollectVisibleServers();

	g_ui.engfuncs.pfnFillRGBA( x, y, w, h, 10, 12, 16, 228 );
	DrawTextInRect( MakeRect( x, y + ScaleY( 8 ), w, ScaleY( 24 ) ), MakeColor( 255, 220, 160, 255 ), "Server Browser", true );

	for( int i = 0; i < SERVERTAB_COUNT; ++i )
	{
		const int tx = x + ScaleX( 8 ) + i * tabW;
		const int selected = i == g_ui.serverTab;
		g_ui.engfuncs.pfnFillRGBA( tx, y + ScaleY( 34 ), tabW - ScaleX( 4 ), ScaleY( 24 ), selected ? 60 : 28, selected ? 56 : 30, 34, 220 );
		DrawTextInRect( MakeRect( tx, y + ScaleY( 36 ), tabW - ScaleX( 4 ), ScaleY( 20 ) ), selected ? MakeColor( 255, 255, 255, 255 ) : MakeColor( 220, 200, 160, 255 ), tabs[i], true );
	}

	g_ui.engfuncs.pfnFillRGBA( x + ScaleX( 8 ), y + ScaleY( 66 ), w - ScaleX( 16 ), 1, 255, 220, 160, 255 );
	DrawTextInRect( MakeRect( x + ScaleX( 16 ), y + ScaleY( 72 ), ScaleX( 220 ), rowH ), MakeColor( 255, 220, 160, 255 ), "Name", false );
	DrawTextInRect( MakeRect( x + ScaleX( 260 ), y + ScaleY( 72 ), ScaleX( 120 ), rowH ), MakeColor( 255, 220, 160, 255 ), "Map", false );
	DrawTextInRect( MakeRect( x + ScaleX( 388 ), y + ScaleY( 72 ), ScaleX( 80 ), rowH ), MakeColor( 255, 220, 160, 255 ), "Players", false );
	DrawTextInRect( MakeRect( x + ScaleX( 474 ), y + ScaleY( 72 ), ScaleX( 48 ), rowH ), MakeColor( 255, 220, 160, 255 ), "Ping", false );
	DrawTextInRect( MakeRect( x + ScaleX( 524 ), y + ScaleY( 72 ), ScaleX( 60 ), rowH ), MakeColor( 255, 220, 160, 255 ), "Addr", false );

	for( size_t i = 0; i < visible.size() && i < 12; ++i )
	{
		const int yy = rowY + (int)i * rowH;
		const bool selected = (int)i == g_ui.selectedServer;
		ServerEntry &server = *visible[i];

		if( selected )
			g_ui.engfuncs.pfnFillRGBA( x + ScaleX( 10 ), yy, w - ScaleX( 20 ), rowH, 44, 40, 28, 220 );

		DrawTextInRect( MakeRect( x + ScaleX( 16 ), yy, ScaleX( 236 ), rowH ), selected ? MakeColor( 255, 255, 255, 255 ) : MakeColor( 220, 220, 220, 255 ), server.name.c_str(), false );
		DrawTextInRect( MakeRect( x + ScaleX( 260 ), yy, ScaleX( 120 ), rowH ), MakeColor( 200, 200, 200, 255 ), server.map.c_str(), false );
		DrawTextInRect( MakeRect( x + ScaleX( 388 ), yy, ScaleX( 80 ), rowH ), MakeColor( 200, 200, 200, 255 ), server.players.c_str(), false );
		DrawTextInRect( MakeRect( x + ScaleX( 474 ), yy, ScaleX( 48 ), rowH ), MakeColor( 200, 200, 200, 255 ), server.ping.c_str(), false );
		DrawTextInRect( MakeRect( x + ScaleX( 524 ), yy, ScaleX( 60 ), rowH ), MakeColor( 200, 200, 200, 255 ), server.address.c_str(), false );
	}

	DrawTextInRect( MakeRect( x + ScaleX( 14 ), y + h - ScaleY( 28 ), ScaleX( 560 ), ScaleY( 20 ) ), MakeColor( 180, 180, 180, 255 ), "[Enter] Join  [R] Refresh  [Tab] Change tab  [F] Favorite  [Esc] Back", false );
}

static void DrawWidget( Widget &widget, int &buttonIndex )
{
	if( !widget.visible )
		return;

	Color4 fg = widget.fg;

	if( widget.type == WIDGET_BUTTON )
	{
		const bool hovered = buttonIndex == g_ui.hoveredButton;
		if( hovered )
			fg = widget.armed;

		if( widget.fill )
		{
			Color4 bg = widget.bg;
			if( hovered )
			{
				bg.r = Clamp255( bg.r + 32 );
				bg.g = Clamp255( bg.g + 24 );
				bg.b = Clamp255( bg.b + 24 );
			}

			g_ui.engfuncs.pfnFillRGBA( widget.rect.x, widget.rect.y, widget.rect.w, widget.rect.h, bg.r, bg.g, bg.b, bg.a );
		}

		if( widget.border )
		{
			g_ui.engfuncs.pfnFillRGBA( widget.rect.x, widget.rect.y, widget.rect.w, 1, fg.r, fg.g, fg.b, 255 );
			g_ui.engfuncs.pfnFillRGBA( widget.rect.x, widget.rect.y + widget.rect.h - 1, widget.rect.w, 1, fg.r, fg.g, fg.b, 255 );
			g_ui.engfuncs.pfnFillRGBA( widget.rect.x, widget.rect.y, 1, widget.rect.h, fg.r, fg.g, fg.b, 255 );
			g_ui.engfuncs.pfnFillRGBA( widget.rect.x + widget.rect.w - 1, widget.rect.y, 1, widget.rect.h, fg.r, fg.g, fg.b, 255 );
		}

		DrawTextInRect( widget.rect, fg, LocalizeText( widget.text ), true );
		buttonIndex++;
	}
	else if( widget.type == WIDGET_LABEL || widget.type == WIDGET_TEXTBOX )
	{
		if( widget.fill )
			g_ui.engfuncs.pfnFillRGBA( widget.rect.x, widget.rect.y, widget.rect.w, widget.rect.h, widget.bg.r, widget.bg.g, widget.bg.b, widget.bg.a );
		DrawTextInRect( widget.rect, fg, LocalizeText( widget.text ), widget.centered );
	}
	else if( widget.type == WIDGET_IMAGE )
	{
		if( !widget.texture && !widget.image.empty() )
			widget.texture = g_ui.engfuncs.pfnPIC_Load( widget.image.c_str(), NULL, 0, PIC_KEEP_SOURCE | PIC_NOFLIP_TGA );

		if( widget.texture )
		{
			g_ui.engfuncs.pfnPIC_Set( widget.texture, 255, 255, 255, 255 );
			g_ui.engfuncs.pfnPIC_Draw( widget.rect.x, widget.rect.y, widget.rect.w, widget.rect.h, NULL );
		}
	}
	else
	{
		if( widget.fill )
			g_ui.engfuncs.pfnFillRGBA( widget.rect.x, widget.rect.y, widget.rect.w, widget.rect.h, widget.bg.r, widget.bg.g, widget.bg.b, widget.bg.a );
	}

	for( size_t i = 0; i < widget.children.size(); ++i )
		DrawWidget( widget.children[i], buttonIndex );
}

static void BuildFallbackMenu( void )
{
	g_ui.root.clear();

	ResourceNode root;
	root.name = "GameMenu";

	ResourceNode background;
	background.name = "Background";
	background.props["ControlName"] = "Panel";
	background.props["xpos"] = "0";
	background.props["ypos"] = "0";
	background.props["wide"] = "640";
	background.props["tall"] = "480";
	background.props["bgcolor"] = "8 12 16 220";
	root.children.push_back( background );

	const char *labels[] = { "Resume", "Servers", "Options", "Console", "Quit" };
	const char *commands[] = { "cancelselect", "vgui_servers", "togglemenu", "toggleconsole", "quit\n" };

	for( int i = 0; i < 5; ++i )
	{
		ResourceNode button;
		char ypos[16];
		snprintf( ypos, sizeof( ypos ), "%d", 150 + i * 48 );
		button.name = labels[i];
		button.props["ControlName"] = "Button";
		button.props["labelText"] = labels[i];
		button.props["command"] = commands[i];
		button.props["xpos"] = "c-110";
		button.props["ypos"] = ypos;
		button.props["wide"] = "220";
		button.props["tall"] = "36";
		button.props["fgcolor"] = "255 220 160 255";
		button.props["bgcolor"] = "20 24 30 220";
		button.props["armedfgcolor"] = "255 255 255 255";
		root.children.push_back( button );
	}

	for( size_t i = 0; i < root.children.size(); ++i )
		g_ui.root.push_back( BuildWidget( root.children[i] ));
}

static void ReloadLayout( void )
{
	ResourceNode root;

	if( ParseResourceFile( "resource/GameMenuVGUI.res", root ) || ParseResourceFile( "resource/GameMenu.res", root ))
	{
		g_ui.root.clear();
		for( size_t i = 0; i < root.children.size(); ++i )
			g_ui.root.push_back( BuildWidget( root.children[i] ));
	}
	else
	{
		BuildFallbackMenu();
	}

	Rect screen = { 0, 0, g_ui.screenW, g_ui.screenH };
	for( size_t i = 0; i < g_ui.root.size(); ++i )
		LayoutWidget( g_ui.root[i], screen );
}

static void ExecuteButtonCommand( Widget &widget )
{
	if( widget.command.empty() )
		return;

	if( !stricmp( widget.command.c_str(), "OpenMOTD" ) || !stricmp( widget.command.c_str(), "ShowMOTD" ))
	{
		g_ui.motdVisible = true;
		return;
	}
	if( !stricmp( widget.command.c_str(), "OpenServers" ) || !stricmp( widget.command.c_str(), "vgui_servers" ))
	{
		OpenServersPage( SERVERTAB_DIRECT );
		return;
	}
	if( !stricmp( widget.command.c_str(), "ResumeGame" ) || !stricmp( widget.command.c_str(), "CloseMenu" ))
	{
		g_ui.visible = false;
		return;
	}

	g_ui.engfuncs.pfnClientCmd( 0, widget.command.c_str() );
}

static bool ActivateButtonByIndex( std::vector<Widget> &widgets, int &index )
{
	for( size_t i = 0; i < widgets.size(); ++i )
	{
		Widget &widget = widgets[i];
		if( !widget.visible )
			continue;

		if( widget.type == WIDGET_BUTTON )
		{
			if( index == 0 )
			{
				ExecuteButtonCommand( widget );
				return true;
			}
			index--;
		}

		if( ActivateButtonByIndex( widget.children, index ))
			return true;
	}

	return false;
}

static void RecalculateHover( void )
{
	int index = 0;
	g_ui.hoveredButton = -1;

	std::vector<Widget> *stack = &g_ui.root;
	for( size_t i = 0; i < stack->size(); ++i )
	{
		Widget &widget = (*stack)[i];
		if( widget.type == WIDGET_BUTTON && PointInRect( g_ui.mouseX, g_ui.mouseY, widget.rect ))
			g_ui.hoveredButton = index;

		if( widget.type == WIDGET_BUTTON )
			index++;

		for( size_t j = 0; j < widget.children.size(); ++j )
		{
			Widget &child = widget.children[j];
			if( child.type == WIDGET_BUTTON && PointInRect( g_ui.mouseX, g_ui.mouseY, child.rect ))
				g_ui.hoveredButton = index;

			if( child.type == WIDGET_BUTTON )
				index++;
		}
	}
}

static std::string StripHtml( const std::string &input )
{
	std::string output;
	bool inTag = false;

	for( size_t i = 0; i < input.size(); ++i )
	{
		const char ch = input[i];

		if( ch == '<' )
		{
			inTag = true;
			continue;
		}
		if( ch == '>' )
		{
			inTag = false;
			continue;
		}
		if( !inTag )
			output.push_back( ch );
	}

	return output;
}

static void LoadMotdText( const char *source )
{
	g_ui.motdSource = source ? source : "";
	g_ui.motdText.clear();

	if( !source || !source[0] )
		source = g_ui.engfuncs.pfnGetCvarString( "motdfile" );

	if( !source || !source[0] )
	{
		g_ui.motdText = "No MOTD source configured.";
		return;
	}

	if( !strnicmp( source, "http://", 7 ) || !strnicmp( source, "https://", 8 ) )
	{
		g_ui.motdText = "Remote HTML MOTD is not embedded in this build. Provide a local file for now.";
		return;
	}

	int length = 0;
	byte *raw = g_ui.engfuncs.COM_LoadFile( source, &length );
	if( !raw || length <= 0 )
	{
		g_ui.motdText = "Failed to load MOTD file.";
		return;
	}

	g_ui.motdText.assign((const char *)raw, length );
	g_ui.engfuncs.COM_FreeFile( raw );

	if( g_ui.motdText.find( '<' ) != std::string::npos )
		g_ui.motdText = StripHtml( g_ui.motdText );
}

static void DrawMotd( void )
{
	if( !g_ui.motdVisible )
		return;

	const int x = g_ui.motdWindowX > 0 ? g_ui.motdWindowX : ScaleX( 60 );
	const int y = g_ui.motdWindowY > 0 ? g_ui.motdWindowY : ScaleY( 50 );
	const int w = ScaleX( 520 );
	const int h = ScaleY( 360 );
	const int lineHeight = ScaleY( 14 );
	size_t start = 0;
	int line = 0;

	g_ui.engfuncs.pfnFillRGBA( x, y, w, h, 12, 14, 18, 230 );
	g_ui.engfuncs.pfnFillRGBA( x, y, w, 2, 255, 220, 160, 255 );
	g_ui.engfuncs.pfnFillRGBA( x, y + h - 2, w, 2, 255, 220, 160, 255 );
	DrawTextInRect( MakeRect( x, y + ScaleY( 8 ), w, ScaleY( 24 ) ), MakeColor( 255, 220, 160, 255 ), "MOTD", true );

	while( start < g_ui.motdText.size() && y + ScaleY( 40 ) + line * lineHeight < y + h - ScaleY( 20 ) )
	{
		size_t end = g_ui.motdText.find( '\n', start );
		std::string row = g_ui.motdText.substr( start, end == std::string::npos ? std::string::npos : end - start );

		DrawTextInRect( MakeRect( x + ScaleX( 16 ), y + ScaleY( 40 ) + line * lineHeight, w - ScaleX( 32 ), lineHeight ), MakeColor( 220, 220, 220, 255 ), row.c_str(), false );

		if( end == std::string::npos )
			break;
		start = end + 1;
		line++;
	}
}

static void VGUI_Reload_f( void )
{
	ReloadLayout();
}

static void VGUI_ShowMenu_f( void )
{
	g_ui.visible = true;
	g_ui.page = UiState::PAGE_MAIN;
}

static void VGUI_Hide_f( void )
{
	g_ui.motdVisible = false;
	g_ui.visible = false;
}

static void VGUI_ShowMotd_f( void )
{
	const char *source = g_ui.engfuncs.pfnCmdArgc() > 1 ? g_ui.engfuncs.pfnCmdArgv( 1 ) : NULL;
	LoadMotdText( source );
	g_ui.motdVisible = true;
	g_ui.visible = true;
}

static void VGUI_Servers_f( void )
{
	OpenServersPage( SERVERTAB_DIRECT );
}

static void UI_UpdateResolution( void )
{
	g_ui.screenW = g_ui.globals && g_ui.globals->scrWidth > 0 ? g_ui.globals->scrWidth : 640;
	g_ui.screenH = g_ui.globals && g_ui.globals->scrHeight > 0 ? g_ui.globals->scrHeight : 480;
}

static int UI_VidInit( void )
{
	UI_UpdateResolution();
	ReloadLayout();
	return 1;
}

static void UI_Init( void )
{
	g_ui.initialized = true;
	g_ui.visible = true;
	g_ui.hoveredButton = -1;
	g_ui.lastHoveredButton = -1;
	g_ui.activeButton = -1;
	g_ui.serverWindowX = ScaleX( 24 );
	g_ui.serverWindowY = ScaleY( 40 );
	g_ui.motdWindowX = ScaleX( 60 );
	g_ui.motdWindowY = ScaleY( 50 );
	g_ui.dragTarget = DRAG_NONE;

	g_ui.engfuncs.pfnAddCommand( "vgui_reload", VGUI_Reload_f );
	g_ui.engfuncs.pfnAddCommand( "vgui_showmenu", VGUI_ShowMenu_f );
	g_ui.engfuncs.pfnAddCommand( "vgui_hide", VGUI_Hide_f );
	g_ui.engfuncs.pfnAddCommand( "vgui_showmotd", VGUI_ShowMotd_f );
	g_ui.engfuncs.pfnAddCommand( "motd_open", VGUI_ShowMotd_f );
	g_ui.engfuncs.pfnAddCommand( "vgui_servers", VGUI_Servers_f );

	LoadAddressList( "favorite_servers.lst", g_favorites );
	LoadAddressList( "history_servers.lst", g_history );

	UI_VidInit();
}

static void UI_Shutdown( void )
{
	g_ui.root.clear();
	g_ui.visible = false;
	g_ui.motdVisible = false;
}

static void UI_Redraw( float time )
{
	int buttonIndex = 0;
	(void)time;

	if( !g_ui.visible )
		return;

	UI_UpdateResolution();

	if( g_ui.page == UiState::PAGE_MAIN )
	{
		for( size_t i = 0; i < g_ui.root.size(); ++i )
			DrawWidget( g_ui.root[i], buttonIndex );
	}
	else
	{
		DrawServerBrowser();
	}

	DrawMotd();
}

static void UI_KeyEvent( int key, int down )
{
	if( key == K_MOUSE1 && !down )
	{
		g_ui.mouseDown = false;

		if( g_ui.activeButton >= 0 && g_ui.activeButton == g_ui.hoveredButton )
		{
			int index = g_ui.activeButton;
			ActivateButtonByIndex( g_ui.root, index );
		}

		g_ui.activeButton = -1;
		g_ui.dragTarget = DRAG_NONE;
		return;
	}

	if( !down )
		return;

	if( key == K_ESCAPE )
	{
		if( g_ui.motdVisible )
			g_ui.motdVisible = false;
		else if( g_ui.page == UiState::PAGE_SERVERS )
			g_ui.page = UiState::PAGE_MAIN;
		else g_ui.visible = !g_ui.visible;
		return;
	}

	if( g_ui.page == UiState::PAGE_SERVERS )
	{
		std::vector<ServerEntry*> visible = CollectVisibleServers();

		if( key == K_TAB )
		{
			g_ui.serverTab = ( g_ui.serverTab + 1 ) % SERVERTAB_COUNT;
			RefreshServers();
			return;
		}
		if( key == K_UPARROW && g_ui.selectedServer > 0 )
		{
			g_ui.selectedServer--;
			return;
		}
		if( key == K_DOWNARROW && g_ui.selectedServer + 1 < (int)visible.size() )
		{
			g_ui.selectedServer++;
			return;
		}
		if( key == 'r' || key == 'R' || key == K_F5 )
		{
			RefreshServers();
			return;
		}
		if(( key == 'f' || key == 'F' ) && g_ui.selectedServer >= 0 && g_ui.selectedServer < (int)visible.size() )
		{
			ServerEntry &server = *visible[g_ui.selectedServer];

			if( !AddressInList( g_favorites, server.address ) )
			{
				g_favorites.push_back( server.address );
				SaveAddressList( "favorite_servers.lst", g_favorites );
				server.favorite = true;
			}
			return;
		}
		if(( key == K_ENTER || key == K_KP_ENTER ) && g_ui.selectedServer >= 0 && g_ui.selectedServer < (int)visible.size() )
		{
			std::string cmd = "connect ";
			cmd += visible[g_ui.selectedServer]->address;
			cmd += "\n";
			AddToHistory( visible[g_ui.selectedServer]->address );
			visible[g_ui.selectedServer]->history = true;
			g_ui.engfuncs.pfnClientCmd( 0, cmd.c_str() );
			return;
		}
	}

	if( key == K_MOUSE1 )
	{
		g_ui.mouseDown = true;
		g_ui.activeButton = g_ui.hoveredButton;
		if( g_ui.motdVisible )
		{
			const Rect title = { g_ui.motdWindowX, g_ui.motdWindowY, ScaleX( 520 ), ScaleY( 28 ) };
			if( PointInRect( g_ui.mouseX, g_ui.mouseY, title ) )
			{
				g_ui.dragTarget = DRAG_MOTD;
				g_ui.dragOffsetX = g_ui.mouseX - g_ui.motdWindowX;
				g_ui.dragOffsetY = g_ui.mouseY - g_ui.motdWindowY;
			}
		}
		else if( g_ui.page == UiState::PAGE_SERVERS )
		{
			const Rect title = { g_ui.serverWindowX, g_ui.serverWindowY, ScaleX( 592 ), ScaleY( 28 ) };
			if( PointInRect( g_ui.mouseX, g_ui.mouseY, title ) )
			{
				g_ui.dragTarget = DRAG_SERVER;
				g_ui.dragOffsetX = g_ui.mouseX - g_ui.serverWindowX;
				g_ui.dragOffsetY = g_ui.mouseY - g_ui.serverWindowY;
			}
		}
		PlayClickSound();
		return;
	}

	if( key == K_ENTER || key == K_KP_ENTER )
	{
		int index = g_ui.hoveredButton >= 0 ? g_ui.hoveredButton : 0;
		ActivateButtonByIndex( g_ui.root, index );
	}
}

static void UI_MouseMove( int x, int y )
{
	g_ui.mouseX = x;
	g_ui.mouseY = y;

	if( g_ui.dragTarget == DRAG_SERVER )
	{
		g_ui.serverWindowX = x - g_ui.dragOffsetX;
		g_ui.serverWindowY = y - g_ui.dragOffsetY;
	}
	else if( g_ui.dragTarget == DRAG_MOTD )
	{
		g_ui.motdWindowX = x - g_ui.dragOffsetX;
		g_ui.motdWindowY = y - g_ui.dragOffsetY;
	}

	RecalculateHover();

	if( g_ui.hoveredButton != g_ui.lastHoveredButton )
	{
		if( g_ui.hoveredButton >= 0 )
			PlayHoverSound();
		g_ui.lastHoveredButton = g_ui.hoveredButton;
	}
}

static void UI_SetActiveMenu( int active )
{
	g_ui.visible = active ? true : false;
	if( active )
	{
		g_ui.page = UiState::PAGE_MAIN;
		UI_VidInit();
	}
}

static void UI_AddServerToList( struct netadr_s adr, const char *info )
{
	AddOrUpdateServer( adr, info );
}

static void UI_GetCursorPos( int *x, int *y )
{
	if( x ) *x = g_ui.mouseX;
	if( y ) *y = g_ui.mouseY;
}

static void UI_SetCursorPos( int x, int y )
{
	g_ui.mouseX = x;
	g_ui.mouseY = y;
}

static void UI_ShowCursor( int show )
{
	g_ui.mouseCaptured = show ? true : false;
}

static void UI_CharEvent( int key )
{
	if( key == '\r' )
	{
		int index = g_ui.hoveredButton >= 0 ? g_ui.hoveredButton : 0;
		ActivateButtonByIndex( g_ui.root, index );
	}
}

static int UI_MouseInRect( void )
{
	return 1;
}

static int UI_IsVisible( void )
{
	return g_ui.visible ? 1 : 0;
}

static int UI_CreditsActive( void )
{
	return 0;
}

static void UI_FinalCredits( void )
{
}

static UI_FUNCTIONS gFunctionTable =
{
	UI_VidInit,
	UI_Init,
	UI_Shutdown,
	UI_Redraw,
	UI_KeyEvent,
	UI_MouseMove,
	UI_SetActiveMenu,
	UI_AddServerToList,
	UI_GetCursorPos,
	UI_SetCursorPos,
	UI_ShowCursor,
	UI_CharEvent,
	UI_MouseInRect,
	UI_IsVisible,
	UI_CreditsActive,
	UI_FinalCredits
};

static void UI_ShowMessageBox_Impl( const char *text )
{
	g_ui.motdText = text ? text : "";
	g_ui.motdVisible = true;
	g_ui.visible = true;
}

static UI_EXTENDED_FUNCTIONS gExtendedTable =
{
	NULL,
	NULL,
	NULL,
	NULL,
	UI_ShowMessageBox_Impl,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static void AddOrUpdateServer( struct netadr_s adr, const char *info )
{
	ServerEntry server;
	const char *address = g_ui.extfuncs.pfnAdrToString ? g_ui.extfuncs.pfnAdrToString( adr ) : "";

	server.address = address ? address : "";
	server.rawInfo = info ? info : "";
	server.name = InfoValueForKey( info, "host" );
	if( server.name.empty() ) server.name = InfoValueForKey( info, "hostname" );
	if( server.name.empty() ) server.name = server.address;
	server.map = InfoValueForKey( info, "map" );
	server.ping = InfoValueForKey( info, "ping" );

	const char *clients = InfoValueForKey( info, "clients" );
	const char *max = InfoValueForKey( info, "max" );
	if( clients[0] && max[0] )
		server.players = std::string( clients ) + "/" + max;
	else server.players = "-";

	server.favorite = AddressInList( g_favorites, server.address );
	server.history = AddressInList( g_history, server.address );
	server.lan = g_ui.serverTab == SERVERTAB_LAN;
	server.nat = g_ui.serverTab == SERVERTAB_NAT;

	for( size_t i = 0; i < g_servers.size(); ++i )
	{
		if( !stricmp( g_servers[i].address.c_str(), server.address.c_str() ))
		{
			g_servers[i] = server;
			return;
		}
	}

	g_servers.push_back( server );
}
}

extern "C" EXPORT int GetMenuAPI( UI_FUNCTIONS *pFunctionTable, ui_enginefuncs_t *pEngfuncsFromEngine, ui_globalvars_t *pGlobals )
{
	if( !pFunctionTable || !pEngfuncsFromEngine || !pGlobals )
		return 0;

	memcpy( pFunctionTable, &gFunctionTable, sizeof( gFunctionTable ));
	memcpy( &g_ui.engfuncs, pEngfuncsFromEngine, sizeof( g_ui.engfuncs ));
	memset( &g_ui.extfuncs, 0, sizeof( g_ui.extfuncs ));
	g_ui.globals = pGlobals;
	return 1;
}

extern "C" EXPORT int GetExtAPI( int version, UI_EXTENDED_FUNCTIONS *pFunctionTable, ui_extendedfuncs_t *pEngfuncsFromEngine )
{
	if( !pFunctionTable || !pEngfuncsFromEngine || version != MENU_EXTENDED_API_VERSION )
		return 0;

	memcpy( &g_ui.extfuncs, pEngfuncsFromEngine, sizeof( g_ui.extfuncs ));
	memcpy( pFunctionTable, &gExtendedTable, sizeof( gExtendedTable ));
	return 1;
}

extern "C" EXPORT void AddTouchButtonToList( const char *name, const char *texture, const char *command, unsigned char *color, int flags )
{
	(void)name;
	(void)texture;
	(void)command;
	(void)color;
	(void)flags;
}
