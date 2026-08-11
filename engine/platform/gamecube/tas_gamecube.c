/*
 * Replay-only pad TAS loader/player for GameCube Dolphin probes.
 */
#include "common.h"

#if XASH_GAMECUBE && XASH_INPUT == INPUT_GAMECUBE
#include "platform/gamecube/tas_gamecube.h"
#include <ogc/pad.h>
#include <ctype.h>

typedef struct gc_tas_state_s
{
	char              name[GC_TAS_MAX_NAME];
	gc_tas_segment_t  segs[GC_TAS_MAX_SEGMENTS];
	int               seg_count;
	int               seg_index;
	uint              seg_frame;
	qboolean          loaded;
	qboolean          load_attempted;
	qboolean          load_failed;
	qboolean          begun;
	qboolean          complete;
	qboolean          complete_logged;
	qboolean          logged_attack;
	qboolean          logged_jump;
	qboolean          logged_use;
	uint              total_frames;
} gc_tas_state_t;

static gc_tas_state_t gc_tas;

static unsigned short GC_TasParseButtonToken( const char *tok, size_t len )
{
	if( len == 1 )
	{
		switch( toupper( (unsigned char)tok[0] ))
		{
		case 'A': return PAD_BUTTON_A;
		case 'B': return PAD_BUTTON_B;
		case 'X': return PAD_BUTTON_X;
		case 'Y': return PAD_BUTTON_Y;
		case 'Z': return PAD_TRIGGER_Z;
		case 'L': return PAD_TRIGGER_L;
		case 'R': return PAD_TRIGGER_R;
		default: break;
		}
	}
	if( len == 5 && !Q_strnicmp( tok, "START", 5 ))
		return PAD_BUTTON_START;
	if( len == 3 && !Q_strnicmp( tok, "DUP", 3 ))
		return PAD_BUTTON_UP;
	if( len == 5 && !Q_strnicmp( tok, "DDOWN", 5 ))
		return PAD_BUTTON_DOWN;
	if( len == 5 && !Q_strnicmp( tok, "DLEFT", 5 ))
		return PAD_BUTTON_LEFT;
	if( len == 6 && !Q_strnicmp( tok, "DRIGHT", 6 ))
		return PAD_BUTTON_RIGHT;
	return 0;
}

static unsigned short GC_TasParseButtons( const char *spec )
{
	unsigned short mask = 0;
	const char *p = spec;

	if( !p || !p[0] || ( p[0] == '-' && p[1] == '\0' ))
		return 0;

	while( *p )
	{
		const char *start;
		size_t len;

		while( *p == '+' || *p == ' ' || *p == '\t' )
			p++;
		if( !*p )
			break;
		start = p;
		while( *p && *p != '+' && *p != ' ' && *p != '\t' )
			p++;
		len = (size_t)( p - start );
		mask |= GC_TasParseButtonToken( start, len );
	}
	return mask;
}

static const char *GC_TasButtonLabel( unsigned short buttons )
{
	static char buf[48];
	char *out = buf;
	size_t left = sizeof( buf );
	int first = 1;

	buf[0] = '\0';
#define GC_TAS_APPEND( mask, name ) \
	do { \
		if( buttons & (mask) ) \
		{ \
			int n = Q_snprintf( out, left, "%s%s", first ? "" : "+", (name) ); \
			if( n > 0 && (size_t)n < left ) { out += n; left -= (size_t)n; first = 0; } \
		} \
	} while( 0 )
	GC_TAS_APPEND( PAD_BUTTON_A, "A" );
	GC_TAS_APPEND( PAD_BUTTON_B, "B" );
	GC_TAS_APPEND( PAD_BUTTON_X, "X" );
	GC_TAS_APPEND( PAD_BUTTON_Y, "Y" );
	GC_TAS_APPEND( PAD_TRIGGER_Z, "Z" );
	GC_TAS_APPEND( PAD_TRIGGER_L, "L" );
	GC_TAS_APPEND( PAD_TRIGGER_R, "R" );
	GC_TAS_APPEND( PAD_BUTTON_START, "START" );
	GC_TAS_APPEND( PAD_BUTTON_UP, "DUP" );
	GC_TAS_APPEND( PAD_BUTTON_DOWN, "DDOWN" );
	GC_TAS_APPEND( PAD_BUTTON_LEFT, "DLEFT" );
	GC_TAS_APPEND( PAD_BUTTON_RIGHT, "DRIGHT" );
#undef GC_TAS_APPEND
	if( first )
		Q_strncpy( buf, "-", sizeof( buf ));
	return buf;
}

static qboolean GC_TasParseLine( char *line, gc_tas_segment_t *out )
{
	char *tok[6];
	int n = 0;
	char *p = line;
	long frames;
	int sx, sy, cx, cy;

	while( *p == ' ' || *p == '\t' )
		p++;
	if( *p == '\0' || *p == '#' || *p == '\r' || *p == '\n' )
		return false;

	while( *p && n < 6 )
	{
		while( *p == ' ' || *p == '\t' )
			p++;
		if( !*p || *p == '#' || *p == '\r' || *p == '\n' )
			break;
		tok[n++] = p;
		while( *p && *p != ' ' && *p != '\t' && *p != '#' && *p != '\r' && *p != '\n' )
			p++;
		if( *p )
			*p++ = '\0';
	}
	if( n < 6 )
		return false;

	frames = strtol( tok[0], NULL, 10 );
	if( frames < 1 || frames > GC_TAS_MAX_FRAMES )
		return false;
	sx = (int)strtol( tok[2], NULL, 10 );
	sy = (int)strtol( tok[3], NULL, 10 );
	cx = (int)strtol( tok[4], NULL, 10 );
	cy = (int)strtol( tok[5], NULL, 10 );
	if( sx < -128 || sx > 127 || sy < -128 || sy > 127
		|| cx < -128 || cx > 127 || cy < -128 || cy > 127 )
		return false;

	out->frames = (uint)frames;
	out->buttons = GC_TasParseButtons( tok[1] );
	out->stick_x = (signed char)sx;
	out->stick_y = (signed char)sy;
	out->cstick_x = (signed char)cx;
	out->cstick_y = (signed char)cy;
	return true;
}

static qboolean GC_TasLoadFromBuffer( const char *name, char *buf, fs_offset_t size )
{
	char *cursor = buf;
	char *end = buf + size;
	gc_tas_segment_t seg;
	uint total = 0;

	gc_tas.seg_count = 0;
	gc_tas.total_frames = 0;
	Q_strncpy( gc_tas.name, name, sizeof( gc_tas.name ));

	while( cursor < end && gc_tas.seg_count < GC_TAS_MAX_SEGMENTS )
	{
		char *line = cursor;
		char *nl;

		nl = memchr( cursor, '\n', (size_t)( end - cursor ));
		if( nl )
		{
			*nl = '\0';
			cursor = nl + 1;
		}
		else
		{
			cursor = end;
		}
		{
			size_t len = strlen( line );
			if( len && line[len - 1] == '\r' )
				line[len - 1] = '\0';
		}
		if( !GC_TasParseLine( line, &seg ))
			continue;
		if( total + seg.frames > GC_TAS_MAX_FRAMES )
		{
			Con_Reportf( S_WARN "Xash3D GameCube: TAS %s exceeds %u frames\n",
				name, GC_TAS_MAX_FRAMES );
			return false;
		}
		gc_tas.segs[gc_tas.seg_count++] = seg;
		total += seg.frames;
	}

	if( gc_tas.seg_count <= 0 )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: TAS %s has no segments\n", name );
		return false;
	}

	gc_tas.total_frames = total;
	gc_tas.loaded = true;
	gc_tas.load_failed = false;
	gc_tas.seg_index = 0;
	gc_tas.seg_frame = 0;
	gc_tas.begun = false;
	gc_tas.complete = false;
	gc_tas.complete_logged = false;
	gc_tas.logged_attack = false;
	gc_tas.logged_jump = false;
	gc_tas.logged_use = false;
	return true;
}

qboolean GC_TasTryLoad( void )
{
	char name[GC_TAS_MAX_NAME];
	char path[MAX_QPATH];
	byte *data;
	fs_offset_t size = 0;

	if( gc_tas.loaded )
		return true;
	if( gc_tas.load_attempted )
		return false;
	gc_tas.load_attempted = true;

	if( !Sys_GetParmFromCmdLine( "-gctas", name ) || !name[0] )
		return false;
	/* Reject path separators in script names. */
	if( strchr( name, '/' ) || strchr( name, '\\' ) || strchr( name, '.' ))
	{
		Con_Reportf( S_WARN "Xash3D GameCube: invalid -gctas name '%s'\n", name );
		gc_tas.load_failed = true;
		return false;
	}

	Q_snprintf( path, sizeof( path ), "tas/%s.tas", name );
	data = FS_LoadFile( path, &size, false );
	if( !data || size <= 0 )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: TAS load failed path=%s\n", path );
		gc_tas.load_failed = true;
		if( data )
			Mem_Free( data );
		return false;
	}

	if( !GC_TasLoadFromBuffer( name, (char *)data, size ))
	{
		gc_tas.load_failed = true;
		Mem_Free( data );
		return false;
	}
	Mem_Free( data );

	Con_Reportf( "Xash3D GameCube: probe tas begin name=%s segments=%d\n",
		gc_tas.name, gc_tas.seg_count );
	gc_tas.begun = true;
	return true;
}

qboolean GC_TasActive( void )
{
	return gc_tas.loaded && !gc_tas.complete;
}

qboolean GC_TasComplete( void )
{
	return gc_tas.loaded && gc_tas.complete;
}

void GC_TasResetPlayback( void )
{
	if( !gc_tas.loaded )
		return;
	gc_tas.seg_index = 0;
	gc_tas.seg_frame = 0;
	gc_tas.complete = false;
	gc_tas.complete_logged = false;
	gc_tas.logged_attack = false;
	gc_tas.logged_jump = false;
	gc_tas.logged_use = false;
}

void GC_TasGetSticks( signed char *stick_x, signed char *stick_y,
	signed char *cstick_x, signed char *cstick_y )
{
	const gc_tas_segment_t *seg;

	if( stick_x ) *stick_x = 0;
	if( stick_y ) *stick_y = 0;
	if( cstick_x ) *cstick_x = 0;
	if( cstick_y ) *cstick_y = 0;
	if( !gc_tas.loaded || gc_tas.complete || gc_tas.seg_index >= gc_tas.seg_count )
		return;
	seg = &gc_tas.segs[gc_tas.seg_index];
	if( stick_x ) *stick_x = seg->stick_x;
	if( stick_y ) *stick_y = seg->stick_y;
	if( cstick_x ) *cstick_x = seg->cstick_x;
	if( cstick_y ) *cstick_y = seg->cstick_y;
}

unsigned short GC_TasPollButtons( void )
{
	gc_tas_segment_t *seg;
	unsigned short buttons;

	if( !gc_tas.loaded )
		return 0;

	if( gc_tas.complete )
	{
		if( !gc_tas.complete_logged )
		{
			gc_tas.complete_logged = true;
			Con_Reportf( "Xash3D GameCube: probe tas complete name=%s\n", gc_tas.name );
			Con_Reportf( "Xash3D GameCube: probe gameplay input ready\n" );
		}
		return 0;
	}

	if( gc_tas.seg_index >= gc_tas.seg_count )
	{
		gc_tas.complete = true;
		return GC_TasPollButtons();
	}

	seg = &gc_tas.segs[gc_tas.seg_index];
	if( gc_tas.seg_frame == 0 )
	{
		Con_Reportf( "Xash3D GameCube: probe tas frame=%d/%d buttons=%s\n",
			gc_tas.seg_index + 1, gc_tas.seg_count, GC_TasButtonLabel( seg->buttons ));
		/* Compat with probe_newgame_progress_ready action triad. */
		if( ( seg->buttons & PAD_TRIGGER_R ) && !gc_tas.logged_attack )
		{
			gc_tas.logged_attack = true;
			Con_Reportf( "Xash3D GameCube: probe gameplay action attack\n" );
		}
		if( ( seg->buttons & PAD_BUTTON_Y ) && !gc_tas.logged_jump )
		{
			gc_tas.logged_jump = true;
			Con_Reportf( "Xash3D GameCube: probe gameplay action jump\n" );
		}
		if( ( seg->buttons & PAD_BUTTON_A ) && !gc_tas.logged_use )
		{
			gc_tas.logged_use = true;
			Con_Reportf( "Xash3D GameCube: probe gameplay action use\n" );
		}
	}

	buttons = seg->buttons;
	gc_tas.seg_frame++;
	if( gc_tas.seg_frame >= seg->frames )
	{
		gc_tas.seg_index++;
		gc_tas.seg_frame = 0;
		if( gc_tas.seg_index >= gc_tas.seg_count )
			gc_tas.complete = true;
	}
	return buttons;
}

#endif /* XASH_GAMECUBE && XASH_INPUT == INPUT_GAMECUBE */
