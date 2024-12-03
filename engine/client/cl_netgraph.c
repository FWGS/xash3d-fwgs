/*
cl_netgraph.c - Draw Net statistics (borrowed from Xash3D SDL code)
Copyright (C) 2016 Uncle Mike

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
#include "client.h"
#include "kbutton.h"

#if XASH_LOW_MEMORY == 0
#define NET_TIMINGS			1024
#elif XASH_LOW_MEMORY == 1
#define NET_TIMINGS			256
#elif XASH_LOW_MEMORY == 2
#define NET_TIMINGS			64
#endif
#define NET_TIMINGS_MASK		(NET_TIMINGS - 1)
#define LATENCY_AVG_FRAC		0.5f
#define FRAMERATE_AVG_FRAC		0.5f
#define PACKETLOSS_AVG_FRAC		0.5f
#define PACKETCHOKE_AVG_FRAC		0.5f
#define NETGRAPH_LERP_HEIGHT		24
#define NETGRAPH_NET_COLORS		5
#define NUM_LATENCY_SAMPLES		8

CVAR_DEFINE_AUTO( net_graph, "0", FCVAR_ARCHIVE, "draw network usage graph" );
static CVAR_DEFINE_AUTO( net_graphpos, "1", FCVAR_ARCHIVE, "network usage graph position" );
static CVAR_DEFINE_AUTO( net_scale, "5", FCVAR_ARCHIVE, "network usage graph scale level" );
static CVAR_DEFINE_AUTO( net_graphwidth, "192", FCVAR_ARCHIVE, "network usage graph width" );
static CVAR_DEFINE_AUTO( net_graphheight, "64", FCVAR_ARCHIVE, "network usage graph height" );
static CVAR_DEFINE_AUTO( net_graphsolid, "1", FCVAR_ARCHIVE, "fill segments in network usage graph" );

static struct packet_latency_t
{
	int	latency;
	int	choked;
} netstat_packet_latency[NET_TIMINGS];

static struct cmdinfo_t
{
	float	cmd_lerp;
	int	size;
	qboolean	sent;
} netstat_cmdinfo[NET_TIMINGS];

static byte netcolors[NETGRAPH_NET_COLORS+NETGRAPH_LERP_HEIGHT][4] =
{
	{ 255, 0,   0,   255 },
	{ 0,   0,   255, 255 },
	{ 240, 127, 63,  255 },
	{ 255, 255, 0,   255 },
	{ 63,  255, 63,  150 }
	// other will be generated through NetGraph_InitColors()
};

static const byte sendcolor[4] = { 88, 29, 130, 255 };
static const byte holdcolor[4] = { 255, 0, 0, 200 };
static const byte extrap_base_color[4] = { 255, 255, 255, 255 };
static netbandwidthgraph_t	netstat_graph[NET_TIMINGS];
static float		packet_loss;
static float		packet_choke;
static float		framerate = 0.0;
static int		maxmsgbytes = 0;

/*
==========
NetGraph_DrawRect

NetGraph_FillRGBA shortcut
==========
*/
static void NetGraph_DrawRect( const wrect_t *rect, const byte colors[4] )
{
	ref.dllFuncs.Color4ub( colors[0], colors[1], colors[2], colors[3] );	// color for this quad

	ref.dllFuncs.Vertex3f( rect->left, rect->top, 0 );
	ref.dllFuncs.Vertex3f( rect->left + rect->right, rect->top, 0 );
	ref.dllFuncs.Vertex3f( rect->left + rect->right, rect->top + rect->bottom, 0 );
	ref.dllFuncs.Vertex3f( rect->left, rect->top + rect->bottom, 0 );
}

/*
==========
NetGraph_AtEdge

edge detect
==========
*/
static qboolean NetGraph_AtEdge( int x, int width )
{
	if( x > 3 )
	{
		if( x >= width - 4 )
			return true;
		return false;
	}
	return true;
}

/*
==========
NetGraph_InitColors

init netgraph colors
==========
*/
static void NetGraph_InitColors( void )
{
	byte	mincolor[2][3];
	byte	maxcolor[2][3];
	float	dc[2][3];
	int	i, hfrac;
	float	f;

	mincolor[0][0] = 63;
	mincolor[0][1] = 0;
	mincolor[0][2] = 100;

	maxcolor[0][0] = 0;
	maxcolor[0][1] = 63;
	maxcolor[0][2] = 255;

	mincolor[1][0] = 255;
	mincolor[1][1] = 127;
	mincolor[1][2] = 0;

	maxcolor[1][0] = 250;
	maxcolor[1][1] = 0;
	maxcolor[1][2] = 0;

	for( i = 0; i < 3; i++ )
	{
		dc[0][i] = (float)(maxcolor[0][i] - mincolor[0][i]);
		dc[1][i] = (float)(maxcolor[1][i] - mincolor[1][i]);
	}

	hfrac = NETGRAPH_LERP_HEIGHT / 3;

	for( i = 0; i < NETGRAPH_LERP_HEIGHT; i++ )
	{
		if( i < hfrac )
		{
			f = (float)i / (float)hfrac;
			VectorMA( mincolor[0], f, dc[0], netcolors[NETGRAPH_NET_COLORS + i] );
		}
		else
		{
			f = (float)(i - hfrac) / (float)(NETGRAPH_LERP_HEIGHT - hfrac );
			VectorMA( mincolor[1], f, dc[1], netcolors[NETGRAPH_NET_COLORS + i] );
		}
		netcolors[NETGRAPH_NET_COLORS + i][3] = 255;
	}
}

/*
==========
NetGraph_GetFrameData

get frame data info, like chokes, packet losses, also update graph, packet and cmdinfo
==========
*/
static void NetGraph_GetFrameData( float *latency, int *latency_count )
{
	int		i, choke_count = 0, loss_count = 0;
	double		newtime = Sys_DoubleTime();
	static double	nexttime = 0;
	float		loss, choke;

	*latency_count = 0;
	*latency = 0.0f;

	if( newtime >= nexttime )
	{
		// soft fading of net peak usage
		maxmsgbytes = Q_max( 0, maxmsgbytes - 50 );
		nexttime = newtime + 0.05;
	}

	for( i = cls.netchan.incoming_sequence - CL_UPDATE_BACKUP + 1; i <= cls.netchan.incoming_sequence; i++ )
	{
		frame_t *f = cl.frames + ( i & CL_UPDATE_MASK );
		struct packet_latency_t *p = netstat_packet_latency + ( i & NET_TIMINGS_MASK );
		netbandwidthgraph_t *g = netstat_graph + ( i & NET_TIMINGS_MASK );

		p->choked = f->choked;
		if( p->choked ) choke_count++;

		if( !f->valid )
		{
			p->latency = 9998; // broken delta
		}
		else if( f->receivedtime == -1.0 )
		{
			p->latency = 9999; // dropped
			loss_count++;
		}
		else if( f->receivedtime == -3.0 )
		{
			p->latency = 9997; // skipped
		}
		else
		{
			int frame_latency = Q_min( 1.0f, f->latency );
			p->latency = (( frame_latency + 0.1f ) / 1.1f ) * ( net_graphheight.value - NETGRAPH_LERP_HEIGHT - 2 );

			if( i > cls.netchan.incoming_sequence - NUM_LATENCY_SAMPLES )
			{
				(*latency) += 1000.0f * f->latency;
				(*latency_count)++;
			}
		}

		memcpy( g, &f->graphdata, sizeof( netbandwidthgraph_t ));

		if( g->msgbytes > maxmsgbytes )
			maxmsgbytes = g->msgbytes;
	}

	if( maxmsgbytes > 1000 )
		maxmsgbytes = 1000;

	for( i = cls.netchan.outgoing_sequence - CL_UPDATE_BACKUP + 1; i <= cls.netchan.outgoing_sequence; i++ )
	{
		netstat_cmdinfo[i & NET_TIMINGS_MASK].cmd_lerp = cl.commands[i & CL_UPDATE_MASK].frame_lerp;
		netstat_cmdinfo[i & NET_TIMINGS_MASK].sent = cl.commands[i & CL_UPDATE_MASK].heldback ? false : true;
		netstat_cmdinfo[i & NET_TIMINGS_MASK].size = cl.commands[i & CL_UPDATE_MASK].sendsize;
	}

	// packet loss
	loss = 100.0f * (float)loss_count / CL_UPDATE_BACKUP;
	packet_loss = PACKETLOSS_AVG_FRAC * packet_loss + ( 1.0f - PACKETLOSS_AVG_FRAC ) * loss;

	// packet choke
	choke = 100.0f * (float)choke_count / CL_UPDATE_BACKUP;
	packet_choke = PACKETCHOKE_AVG_FRAC * packet_choke + ( 1.0f - PACKETCHOKE_AVG_FRAC ) * choke;
}

/*
===========
NetGraph_DrawTimes

===========
*/
static void NetGraph_DrawTimes( wrect_t rect, int x, int w )
{
	int	i, j, extrap_point = NETGRAPH_LERP_HEIGHT / 3, a, h;
	rgba_t	colors = { 0.9 * 255, 0.9 * 255, 0.7 * 255, 255 };
	wrect_t	fill;

	for( a = 0; a < w; a++ )
	{
		i = ( cls.netchan.outgoing_sequence - a ) & NET_TIMINGS_MASK;
		h = Q_min(( netstat_cmdinfo[i].cmd_lerp / 3.0f ) * NETGRAPH_LERP_HEIGHT, net_graphheight.value * 0.7f);

		fill.left = x + w - a - 1;
		fill.right = fill.bottom = 1;
		fill.top = rect.top + rect.bottom - 4;

		if( h >= extrap_point )
		{
			int	start = 0;

			h -= extrap_point;
			fill.top -= extrap_point;

			if( !net_graphsolid.value )
			{
				fill.top -= (h - 1);
				start = (h - 1);
			}

			for( j = start; j < h; j++ )
			{
				int color = NETGRAPH_NET_COLORS + j + extrap_point;
				color = Q_min( color, ARRAYSIZE( netcolors ) - 1 );

				NetGraph_DrawRect( &fill, netcolors[color] );
				fill.top--;
			}
		}
		else
		{
			int	oldh = h;

			fill.top -= h;
			h = extrap_point - h;

			if( !net_graphsolid.value )
				h = 1;

			for( j = 0; j < h; j++ )
			{
				int color = NETGRAPH_NET_COLORS + j + oldh;
				color = Q_min( color, ARRAYSIZE( netcolors ) - 1 );

				NetGraph_DrawRect( &fill, netcolors[color] );
				fill.top--;
			}
		}

		fill.top = rect.top + rect.bottom - 4 - extrap_point;

		if( NetGraph_AtEdge( a, w ))
			NetGraph_DrawRect( &fill, extrap_base_color );

		fill.top = rect.top + rect.bottom - 4;

		if( netstat_cmdinfo[i].sent )
			NetGraph_DrawRect( &fill, sendcolor );
		else NetGraph_DrawRect( &fill, holdcolor );
	}
}

//left = x
//right = width
//top = y
//bottom = height

/*
===========
NetGraph_DrawHatches

===========
*/
static void NetGraph_DrawHatches( int x, int y )
{
	int	ystep = (int)( 10.0f / net_scale.value );
	byte	colorminor[4] = { 0, 63, 63, 200 };
	byte	color[4] = { 0, 200, 0, 255 };
	wrect_t	hatch = { x, 4, y, 1 };
	int	starty;

	ystep = Q_max( ystep, 1 );

	for( starty = hatch.top; hatch.top > 0 && ((starty - hatch.top) * net_scale.value < (maxmsgbytes + 50)); hatch.top -= ystep )
	{
		if(!((int)((starty - hatch.top) * net_scale.value ) % 50 ))
		{
			NetGraph_DrawRect( &hatch, color );
		}
		else if( ystep > 5 )
		{
			NetGraph_DrawRect( &hatch, colorminor );
		}
	}
}

/*
===========
NetGraph_DrawTextFields

===========
*/
static void NetGraph_DrawTextFields( int x, int y, int w, wrect_t rect, int count, float avg, int packet_loss, int packet_choke, int graphtype )
{
	static int	lastout;
	cl_font_t *font = Con_GetFont( 0 );
	rgba_t		colors = { 0.9 * 255, 0.9 * 255, 0.7 * 255, 255 };
	int		ptx = Q_max( x + w - NETGRAPH_LERP_HEIGHT - 1, 1 );
	int		pty = Q_max( rect.top + rect.bottom - NETGRAPH_LERP_HEIGHT - 3, 1 );
	int		out, i = ( cls.netchan.outgoing_sequence - 1 ) & NET_TIMINGS_MASK;
	int		j = cls.netchan.incoming_sequence & NET_TIMINGS_MASK;
	int		last_y = y - net_graphheight.value;

	if( count > 0 )
	{
		avg = avg / (float)( count - ( host.frametime * FRAMERATE_AVG_FRAC ));

		if( cl_updaterate.value > 0.0f )
			avg -= 1000.0f / cl_updaterate.value;

		// can't be below zero
		avg = Q_max( 0.0f, avg );
	}
	else avg = 0.0;

	// move rolling average
	framerate = FRAMERATE_AVG_FRAC * host.frametime + ( 1.0f - FRAMERATE_AVG_FRAC ) * framerate;

	CL_SetFontRendermode( font );

	if( framerate > 0.0f )
	{
		y -= net_graphheight.value;

		CL_DrawStringf( font, x, y, colors, FONT_DRAW_NORENDERMODE, "%.1f fps" , 1.0f / framerate);

		if( avg > 1.0f )
			CL_DrawStringf( font, x + 75, y, colors, FONT_DRAW_NORENDERMODE, "%i ms" , (int)avg );

		y += 15;

		out = netstat_cmdinfo[i].size;
		if( !out ) out = lastout;
		else lastout = out;

		CL_DrawStringf( font, x, y, colors, FONT_DRAW_NORENDERMODE,
			"in :  %i %.2f kb/s", netstat_graph[j].msgbytes, cls.netchan.flow[FLOW_INCOMING].avgkbytespersec );
		y += 15;

		CL_DrawStringf( font, x, y, colors, FONT_DRAW_NORENDERMODE,
			"out:  %i %.2f kb/s", out, cls.netchan.flow[FLOW_OUTGOING].avgkbytespersec );
		y += 15;

		if( graphtype > 2 )
		{
			int	loss = (int)(( packet_loss + PACKETLOSS_AVG_FRAC ) - 0.01f );
			int	choke = (int)(( packet_choke + PACKETCHOKE_AVG_FRAC ) - 0.01f );

			CL_DrawStringf( font, x, y, colors, FONT_DRAW_NORENDERMODE, "loss: %i choke: %i", loss, choke );
		}
	}

	if( graphtype < 3 )
		CL_DrawStringf( font, ptx, pty, colors, FONT_DRAW_NORENDERMODE, "%i/s", (int)cl_cmdrate.value );

	CL_DrawStringf( font, ptx, last_y, colors, FONT_DRAW_NORENDERMODE, "%i/s" , (int)cl_updaterate.value );
}

/*
===========
NetGraph_DrawDataSegment

===========
*/
static int NetGraph_DrawDataSegment( wrect_t *fill, int bytes, byte r, byte g, byte b, byte a )
{
	float	h = bytes / net_scale.value;
	byte	colors[4] = { r, g, b, a };

	fill->top -= (int)h;

	if( net_graphsolid.value )
		fill->bottom = (int)h;
	else fill->bottom = 1;

	if( fill->top > 1 )
	{
		NetGraph_DrawRect( fill, colors );
		return 1;
	}

	return 0;
}

/*
===========
NetGraph_ColorForHeight

color based on packet latency
===========
*/
static void NetGraph_ColorForHeight( struct packet_latency_t *packet, byte color[4], int *ping )
{
	switch( packet->latency )
	{
	case 9999:
		memcpy( color, netcolors[0], sizeof( byte ) * 4 ); // dropped
		*ping = 0;
		break;
	case 9998:
		memcpy( color, netcolors[1], sizeof( byte ) * 4 ); // invalid
		*ping = 0;
		break;
	case 9997:
		memcpy( color, netcolors[2], sizeof( byte ) * 4 ); // skipped
		*ping = 0;
		break;
	default:
		*ping = 1;
		if( packet->choked )
		{
			memcpy( color, netcolors[3], sizeof( byte ) * 4 );
		}
		else
		{
			memcpy( color, netcolors[4], sizeof( byte ) * 4 );
		}
	}
}

/*
===========
NetGraph_DrawDataUsage

===========
*/
static void NetGraph_DrawDataUsage( int x, int y, int w, int graphtype )
{
	int	a, i, h, lastvalidh = 0, ping;
	int	pingheight = net_graphheight.value - NETGRAPH_LERP_HEIGHT - 2;
	wrect_t	fill = { 0 };
	byte	color[4];

	for( a = 0; a < w; a++ )
	{
		i = (cls.netchan.incoming_sequence - a) & NET_TIMINGS_MASK;
		h = netstat_packet_latency[i].latency;

		NetGraph_ColorForHeight( &netstat_packet_latency[i], color, &ping );

		if( !ping ) h = lastvalidh;
		else lastvalidh = h;

		if( h > pingheight )
			h = pingheight;

		fill.left = x + w - a - 1;
		fill.top = y - h;
		fill.right = 1;
		fill.bottom = ping ? 1: h;

		if( !ping )
		{
			if( fill.bottom > 3 )
			{
				fill.bottom = 2;
				NetGraph_DrawRect( &fill, color );
				fill.top += fill.bottom - 2;
				NetGraph_DrawRect( &fill, color );
			}
			else
			{
				NetGraph_DrawRect( &fill, color );
			}
		}
		else
		{
			NetGraph_DrawRect( &fill, color );
		}

		fill.top = y;
		fill.bottom = 1;

		color[0] = 0;
		color[1] = 255;
		color[2] = 0;
		color[3] = 160;

		if( NetGraph_AtEdge( a, w ))
			NetGraph_DrawRect( &fill, color );

		if( graphtype < 2 )
			continue;

		color[0] = color[1] = color[2] = color[3] = 255;
		fill.top = y - net_graphheight.value - 1;
		fill.bottom = 1;

		if( NetGraph_AtEdge( a, w ))
			NetGraph_DrawRect( &fill, color );

		fill.top -= 1;

		if( netstat_packet_latency[i].latency > 9995 )
			continue; // skip invalid

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].client, 255, 0, 0, 128 ))
			continue;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].players, 255, 255, 0, 128 ))
			continue;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].entities, 255, 0, 255, 128 ))
			continue;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].tentities, 0, 0, 255, 128 ))
			continue;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].sound, 0, 255, 0, 128 ))
			continue;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].event, 0, 255, 255, 128 ))
			continue;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].usr, 200, 200, 200, 128 ))
			continue;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].voicebytes, 255, 255, 255, 255 ))
			continue;

		fill.top = y - net_graphheight.value - 1;
		fill.bottom = 1;
		fill.top -= 2;

		if( !NetGraph_DrawDataSegment( &fill, netstat_graph[i].msgbytes, 240, 240, 240, 128 ))
			continue;
	}

	if( graphtype >= 2 )
		NetGraph_DrawHatches( x, y - net_graphheight.value - 1 );
}

/*
===========
NetGraph_GetScreenPos

===========
*/
static void NetGraph_GetScreenPos( wrect_t *rect, int *w, int *x, int *y )
{
	rect->left = rect->top = 0;
	rect->right = refState.width;
	rect->bottom = refState.height;

	*w = Q_min( NET_TIMINGS, net_graphwidth.value );
	if( rect->right < *w + 10 )
		*w = rect->right - 10;

	// detect x and y position
	switch( (int)net_graphpos.value )
	{
	case 1: // right sided
		*x = rect->left + rect->right - 5 - *w;
		break;
	case 2: // center
		*x = ( rect->left + ( rect->right - 10 - *w )) / 2;
		break;
	default: // left sided
		*x = rect->left + 5;
		break;
	}

	*y = rect->bottom + rect->top - NETGRAPH_LERP_HEIGHT - 5;
}

/*
===========
SCR_DrawNetGraph

===========
*/
void SCR_DrawNetGraph( void )
{
	wrect_t	rect;
	float	avg_ping;
	int	ping_count;
	int	w, x, y;
	kbutton_t *in_graph;
	int   graphtype;

	if( !host.allow_console )
		return;

	if( cls.state != ca_active )
		return;

	in_graph = clgame.dllFuncs.KB_Find( "in_graph" );

	if( in_graph && in_graph->state & 1 )
		graphtype = 2;
	else if( net_graph.value != 0.0f )
		graphtype = (int)net_graph.value;
	else return;

	if( net_scale.value <= 0 )
		Cvar_SetValue( "net_scale", 0.1f );

	NetGraph_GetScreenPos( &rect, &w, &x, &y );

	NetGraph_GetFrameData( &avg_ping, &ping_count );

	NetGraph_DrawTextFields( x, y, w, rect, ping_count, avg_ping, packet_loss, packet_choke, graphtype );

	if( graphtype < 3 )
	{
		ref.dllFuncs.GL_SetRenderMode( kRenderTransColor );
		ref.dllFuncs.GL_Bind( XASH_TEXTURE0, R_GetBuiltinTexture( REF_WHITE_TEXTURE ) );
		ref.dllFuncs.Begin( TRI_QUADS ); // draw all the fills as a long solid sequence of quads for speedup reasons

		// NOTE: fill colors without texture at this point
		NetGraph_DrawDataUsage( x, y, w, graphtype );
		NetGraph_DrawTimes( rect, x, w );

		ref.dllFuncs.End();
		ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
		ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	}
}

void CL_InitNetgraph( void )
{
	Cvar_RegisterVariable( &net_graph );
	Cvar_RegisterVariable( &net_graphpos );
	Cvar_RegisterVariable( &net_scale );
	Cvar_RegisterVariable( &net_graphwidth );
	Cvar_RegisterVariable( &net_graphheight );
	Cvar_RegisterVariable( &net_graphsolid );
	packet_loss = packet_choke = 0.0;

	NetGraph_InitColors();
}
