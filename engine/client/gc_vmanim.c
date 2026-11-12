/*
gc_vmanim.c - play HL1 weapon animations on viewmodels from another game

See gc_vmanim.h. The HL1 indices below are the hlsdk enums (mp5_e,
shotgun_e, crossbow_e, rpg_e, crowbar_e); the DoD side is matched by
sequence name, so no DoD sequence table is hard-coded here.
*/
#include "gc_vmanim.h"

#include <string.h>

typedef struct
{
	const char *name;	// path without "models/" and ".mdl"
	gc_vm_family_t family;
} gc_vm_model_t;

// the viewmodels dod_game.c hands to each HL1 weapon class
static const gc_vm_model_t gc_vm_models[] =
{
	{ "v_garand",		GC_VMF_MP5 },
	{ "v_98k",		GC_VMF_MP5 },
	{ "v_tommy",		GC_VMF_MP5 },
	{ "v_mp40",		GC_VMF_MP5 },
	{ "v_30cal",		GC_VMF_MP5 },
	{ "v_mg42",		GC_VMF_MP5 },
	{ "v_bren",		GC_VMF_SHOTGUN },
	{ "v_mp44",		GC_VMF_SHOTGUN },
	{ "v_spring",		GC_VMF_CROSSBOW },
	{ "v_scoped98k",	GC_VMF_CROSSBOW },
	{ "v_bazooka",		GC_VMF_RPG },
	{ "v_panzerschreck",	GC_VMF_RPG },
	{ "v_spade",		GC_VMF_CROWBAR },
};

static char GC_VMLower( char c )
{
	return ( c >= 'A' && c <= 'Z' ) ? (char)( c - 'A' + 'a' ) : c;
}

static int GC_VMNameEq( const char *a, const char *b, size_t n )
{
	size_t i;

	for( i = 0; i < n; i++ )
	{
		if( GC_VMLower( a[i] ) != GC_VMLower( b[i] ))
			return 0;
		if( !a[i] )
			return 1;
	}
	return 1;
}

gc_vm_family_t GC_VMAnimFamily( const char *modelname )
{
	const char *base, *dot, *p;
	size_t len, i;

	if( !modelname || !modelname[0] )
		return GC_VMF_NONE;

	base = modelname;
	for( p = modelname; *p; p++ )
	{
		if( *p == '/' || *p == '\\' )
			base = p + 1;
	}
	dot = strrchr( base, '.' );
	len = dot ? (size_t)( dot - base ) : strlen( base );

	for( i = 0; i < sizeof( gc_vm_models ) / sizeof( gc_vm_models[0] ); i++ )
	{
		if( strlen( gc_vm_models[i].name ) == len
			&& GC_VMNameEq( base, gc_vm_models[i].name, len ))
			return gc_vm_models[i].family;
	}
	return GC_VMF_NONE;
}

static gc_vm_intent_t GC_VMIntent( gc_vm_action_t action, int variant, int empty )
{
	gc_vm_intent_t intent;

	intent.action = action;
	intent.variant = variant;
	intent.empty = empty;
	return intent;
}

gc_vm_intent_t GC_VMAnimIntent( gc_vm_family_t family, int hl_anim )
{
	switch( family )
	{
	case GC_VMF_MP5:
		switch( hl_anim )
		{
		case 0: case 1: return GC_VMIntent( GC_VMA_IDLE, 0, 0 );	// LONGIDLE, IDLE1
		case 2: return GC_VMIntent( GC_VMA_FIRE, 0, 0 );		// LAUNCH
		case 3: return GC_VMIntent( GC_VMA_RELOAD, 0, 0 );
		case 4: return GC_VMIntent( GC_VMA_DRAW, 0, 0 );		// DEPLOY
		case 5: case 6: case 7: return GC_VMIntent( GC_VMA_FIRE, hl_anim - 5, 0 );
		}
		break;
	case GC_VMF_SHOTGUN:
		switch( hl_anim )
		{
		case 0: case 8: case 9: return GC_VMIntent( GC_VMA_IDLE, 0, 0 );
		case 1: case 2: return GC_VMIntent( GC_VMA_FIRE, hl_anim - 1, 0 );
		// HL reloads a shotgun shell by shell: START_RELOAD once, RELOAD per
		// shell, PUMP at the end. A box magazine plays one reload, so only
		// the start begins it and the rest let it run.
		case 5: return GC_VMIntent( GC_VMA_RELOAD, 0, 0 );
		case 3: case 4: return GC_VMIntent( GC_VMA_KEEP, 0, 0 );
		case 6: return GC_VMIntent( GC_VMA_DRAW, 0, 0 );
		case 7: return GC_VMIntent( GC_VMA_HOLSTER, 0, 0 );
		}
		break;
	case GC_VMF_CROSSBOW:
		switch( hl_anim )
		{
		case 0: case 2: return GC_VMIntent( GC_VMA_IDLE, 0, 0 );
		case 1: case 3: return GC_VMIntent( GC_VMA_IDLE, 0, 1 );
		case 4: case 5: return GC_VMIntent( GC_VMA_FIRE, 0, 0 );
		case 6: return GC_VMIntent( GC_VMA_FIRE, 0, 1 );
		case 7: return GC_VMIntent( GC_VMA_RELOAD, 0, 0 );
		case 8: return GC_VMIntent( GC_VMA_DRAW, 0, 0 );
		case 9: return GC_VMIntent( GC_VMA_DRAW, 0, 1 );
		case 10: return GC_VMIntent( GC_VMA_HOLSTER, 0, 0 );
		case 11: return GC_VMIntent( GC_VMA_HOLSTER, 0, 1 );
		}
		break;
	case GC_VMF_RPG:
		switch( hl_anim )
		{
		case 0: case 1: return GC_VMIntent( GC_VMA_IDLE, 0, 0 );
		case 2: return GC_VMIntent( GC_VMA_RELOAD, 0, 0 );
		case 3: return GC_VMIntent( GC_VMA_FIRE, 0, 0 );
		case 4: return GC_VMIntent( GC_VMA_HOLSTER, 0, 0 );
		case 5: return GC_VMIntent( GC_VMA_DRAW, 0, 0 );
		case 6: return GC_VMIntent( GC_VMA_HOLSTER, 0, 1 );
		case 7: return GC_VMIntent( GC_VMA_DRAW, 0, 1 );
		case 8: case 9: return GC_VMIntent( GC_VMA_IDLE, 0, 1 );
		}
		break;
	case GC_VMF_CROWBAR:
		switch( hl_anim )
		{
		case 0: case 9: case 10: return GC_VMIntent( GC_VMA_IDLE, 0, 0 );
		case 1: return GC_VMIntent( GC_VMA_DRAW, 0, 0 );
		case 2: return GC_VMIntent( GC_VMA_HOLSTER, 0, 0 );
		case 3: case 4: case 5: case 6: case 7: case 8:	// ATTACK1HIT .. ATTACK3HIT
			return GC_VMIntent( GC_VMA_FIRE, ( hl_anim - 3 ) / 2, 0 );
		}
		break;
	default:
		return GC_VMIntent( GC_VMA_NONE, 0, 0 );
	}
	return GC_VMIntent( GC_VMA_IDLE, 0, 0 );
}

static const char *const gc_vm_words_idle[] = { "idle", NULL };
static const char *const gc_vm_words_fire[] = { "shoot", "fire", "attack", "slash", "stab", "swing", NULL };
static const char *const gc_vm_words_reload[] = { "reload", NULL };
static const char *const gc_vm_words_draw[] = { "draw", "deploy", NULL };
static const char *const gc_vm_words_holster[] = { "holster", NULL };

static const char *const *GC_VMWords( gc_vm_action_t action )
{
	switch( action )
	{
	case GC_VMA_IDLE: return gc_vm_words_idle;
	case GC_VMA_FIRE: return gc_vm_words_fire;
	case GC_VMA_RELOAD: return gc_vm_words_reload;
	case GC_VMA_DRAW: return gc_vm_words_draw;
	case GC_VMA_HOLSTER: return gc_vm_words_holster;
	default: return NULL;
	}
}

// case-insensitive substring in a label of at most 32 chars
static int GC_VMHas( const char *label, const char *word )
{
	char low[33];
	size_t i;

	for( i = 0; i < 32 && label[i]; i++ )
		low[i] = GC_VMLower( label[i] );
	low[i] = '\0';
	return strstr( low, word ) != NULL;
}

static int GC_VMStartsUp( const char *label )
{
	return GC_VMLower( label[0] ) == 'u' && GC_VMLower( label[1] ) == 'p';
}

/*
Score a label for an action, 0 when it does not match. An earlier keyword
scores higher; "empty" versions lose unless the empty one was asked for;
MG bipod-up ("up...", "deployed") versions lose so the carried gun wins.
*/
static int GC_VMScore( const char *label, gc_vm_intent_t intent )
{
	const char *const *words = GC_VMWords( intent.action );
	int score = 0, i;

	if( !words )
		return 0;
	for( i = 0; words[i]; i++ )
	{
		if( GC_VMHas( label, words[i] ))
		{
			score = 1000 - 100 * i;
			break;
		}
	}
	if( !score )
		return 0;
	if( GC_VMHas( label, "empty" ))
		score += intent.empty ? 50 : -50;
	if( GC_VMStartsUp( label ) || GC_VMHas( label, "deployed" ))
		score -= 40;
	return score;
}

static int GC_VMPickAction( const char *labels, size_t stride, int numseq, gc_vm_intent_t intent )
{
	int best = 0, count = 0, want, i;

	for( i = 0; i < numseq; i++ )
	{
		const int score = GC_VMScore( labels + stride * (size_t)i, intent );

		if( score > best )
		{
			best = score;
			count = 1;
		}
		else if( score && score == best )
			count++;
	}
	if( !best )
		return -1;

	// HL FIRE1/FIRE2/FIRE3 pick among equally good sequences (shoot1, shoot2)
	want = ( intent.variant > 0 ? intent.variant : 0 ) % count;
	for( i = 0; i < numseq; i++ )
	{
		if( GC_VMScore( labels + stride * (size_t)i, intent ) == best && want-- == 0 )
			return i;
	}
	return -1;
}

int GC_VMAnimPick( const char *labels, size_t stride, int numseq, gc_vm_intent_t intent )
{
	int seq;

	if( intent.action == GC_VMA_NONE || intent.action == GC_VMA_KEEP )
		return -1;
	if( !labels || numseq <= 0 )
		return 0;

	seq = GC_VMPickAction( labels, stride, numseq, intent );
	if( seq < 0 && intent.action != GC_VMA_IDLE )
	{
		// nothing named for it: hold the gun rather than play a stray sequence
		intent.action = GC_VMA_IDLE;
		intent.variant = 0;
		seq = GC_VMPickAction( labels, stride, numseq, intent );
	}
	return seq >= 0 ? seq : 0;
}

const char *GC_VMAnimActionName( gc_vm_action_t action )
{
	switch( action )
	{
	case GC_VMA_IDLE: return "idle";
	case GC_VMA_FIRE: return "fire";
	case GC_VMA_RELOAD: return "reload";
	case GC_VMA_DRAW: return "draw";
	case GC_VMA_HOLSTER: return "holster";
	case GC_VMA_KEEP: return "keep";
	default: return "none";
	}
}
