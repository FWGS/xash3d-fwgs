/*
gc_vmanim.h - play HL1 weapon animations on viewmodels from another game

The DoD port drives its weapons with HL1 weapon code (CMP5, CShotgun, ...)
while the player holds DoD viewmodels. Every animation the weapon code,
its client prediction and its events ask for is an HL1 sequence index,
which means nothing on v_garand.mdl. This maps each HL1 index to what it
means (idle, fire, reload, draw) and then to the DoD sequence whose name
says the same thing.

Pure C with no engine headers, so the host tests can build it alone.
*/
#ifndef GC_VMANIM_H
#define GC_VMANIM_H

#include <stddef.h>

typedef enum
{
	GC_VMA_NONE = 0,	// not a remapped model: leave the index alone
	GC_VMA_IDLE,
	GC_VMA_FIRE,
	GC_VMA_RELOAD,
	GC_VMA_DRAW,
	GC_VMA_HOLSTER,
	GC_VMA_KEEP,		// a step of a longer animation already playing
	GC_VMA_PRIME		// grenade pin pull before the throw
} gc_vm_action_t;

typedef enum
{
	GC_VMF_NONE = 0,
	GC_VMF_MP5,		// rifles, SMGs and MGs on CMP5
	GC_VMF_SHOTGUN,		// BAR-class guns on CShotgun
	GC_VMF_CROSSBOW,	// sniper rifles on CCrossbow
	GC_VMF_RPG,		// rocket launchers on CRpg
	GC_VMF_CROWBAR,		// spade on CCrowbar
	GC_VMF_GLOCK,		// Colt/Luger on CGlock
	GC_VMF_HANDGRENADE	// frag and stick grenades on CHandGrenade
} gc_vm_family_t;

typedef struct
{
	gc_vm_action_t action;
	int variant;	// which of several fire sequences (HL FIRE1/FIRE2/...)
	int empty;	// HL asked for the empty-magazine version
} gc_vm_intent_t;

// family of a viewmodel by its path ("models/v_garand.mdl"), GC_VMF_NONE if not remapped
gc_vm_family_t GC_VMAnimFamily( const char *modelname );

// what an HL1 weapon animation index means for that family
gc_vm_intent_t GC_VMAnimIntent( gc_vm_family_t family, int hl_anim );

// pick the sequence for an intent from the model's sequence labels.
// labels: the first label; stride: bytes from one label to the next
// (sizeof( mstudioseqdesc_t ) for a studio header). Never returns an
// index outside [0, numseq), and returns -1 only for GC_VMA_NONE/KEEP.
int GC_VMAnimPick( const char *labels, size_t stride, int numseq, gc_vm_intent_t intent );

const char *GC_VMAnimActionName( gc_vm_action_t action );

#endif // GC_VMANIM_H
