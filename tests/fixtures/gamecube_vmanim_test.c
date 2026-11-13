/* Host checks for engine/client/gc_vmanim.c. */
#include "gc_vmanim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	char label[32];	/* like mstudioseqdesc_t: label first, then data */
	int pad[36];
} seq_t;

static int failures;

#define CHECK( cond ) do { if( !( cond )) { printf( "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond ); failures++; } } while( 0 )

static int pick( const seq_t *seqs, int n, gc_vm_family_t family, int hl_anim )
{
	return GC_VMAnimPick( seqs[0].label, sizeof( seq_t ), n, GC_VMAnimIntent( family, hl_anim ));
}

static const char *pick_name( const seq_t *seqs, int n, gc_vm_family_t family, int hl_anim )
{
	const int s = pick( seqs, n, family, hl_anim );
	return s >= 0 ? seqs[s].label : "(none)";
}

int main( void )
{
	static const seq_t rifle[] = {
		{ "idle" }, { "shoot1" }, { "shoot2" }, { "shoot_empty" },
		{ "reload" }, { "reload_empty" }, { "draw" }, { "idle_empty" },
	};
	static const seq_t mg[] = {
		{ "upidle" }, { "upshoot" }, { "updraw" }, { "idle" },
		{ "shoot" }, { "reload" }, { "draw" }, { "deployed_reload" },
	};
	static const seq_t spade[] = { { "idle" }, { "slash1" }, { "slash2" }, { "draw" } };
	static const seq_t odd[] = { { "ref" }, { "spin" } };
	static const seq_t grenade[] = {
		{ "idle" }, { "draw" }, { "pinpull" }, { "holster" }, { "throw" },
		{ "exploding_idle" }, { "exploding_draw" }, { "exploding_pinpull" }, { "exploding_throw" },
	};
	const int ng = (int)( sizeof( grenade ) / sizeof( grenade[0] ));
	/* real sequence names of the DoD gun viewmodels (dumped from the MDLs) */
	static const seq_t garand[] = {
		{ "idle" }, { "shoot1" }, { "shoot2" }, { "shoot3" }, { "shoot_empty" }, { "reload" },
		{ "draw" }, { "empty_idle" }, { "draw_empty" }, { "smash" }, { "smash_empty" },
	};
	static const seq_t k98[] = { { "idle" }, { "shoot" }, { "reload" }, { "draw" }, { "stabslash" }, { "stab" } };
	static const seq_t colt[] = {
		{ "idle" }, { "shoot1" }, { "shoot2" }, { "reload" }, { "reload_noshoot" }, { "draw" },
		{ "shoot_empty" }, { "empty_idle" },
	};
	static const seq_t luger[] = {
		{ "idle" }, { "idle" }, { "idle" }, { "shoot" }, { "shoot_empty" }, { "reload_empty" },
		{ "reload" }, { "draw" }, { "empty_idle" },
	};
	static const seq_t bazooka[] = {
		{ "idle" }, { "draw" }, { "aimed" }, { "launch" }, { "down_to_up" }, { "up_to_down" },
		{ "reload_aimed" }, { "reload_idle" },
	};
	static const seq_t mg42[] = {
		{ "upidle" }, { "upidle8" }, { "upidleempty" }, { "downidle" }, { "downidle8" }, { "downidleempty" },
		{ "downtoup" }, { "uptodown" }, { "upshoot" }, { "downshoot" }, { "downshoot8" }, { "reload" },
	};
	const int ngarand = (int)( sizeof( garand ) / sizeof( garand[0] ));
	const int nk98 = (int)( sizeof( k98 ) / sizeof( k98[0] ));
	const int ncolt = (int)( sizeof( colt ) / sizeof( colt[0] ));
	const int nluger = (int)( sizeof( luger ) / sizeof( luger[0] ));
	const int nbazooka = (int)( sizeof( bazooka ) / sizeof( bazooka[0] ));
	const int nmg42 = (int)( sizeof( mg42 ) / sizeof( mg42[0] ));
	const int nr = (int)( sizeof( rifle ) / sizeof( rifle[0] ));
	const int nm = (int)( sizeof( mg ) / sizeof( mg[0] ));
	const int ns = (int)( sizeof( spade ) / sizeof( spade[0] ));
	int a;

	CHECK( GC_VMAnimFamily( "models/v_garand.mdl" ) == GC_VMF_DODGUN );
	CHECK( GC_VMAnimFamily( "MODELS\\V_MG42.MDL" ) == GC_VMF_DODGUN );
	CHECK( GC_VMAnimFamily( "models/v_bren.mdl" ) == GC_VMF_DODGUN );
	CHECK( GC_VMAnimFamily( "models/v_scoped98k.mdl" ) == GC_VMF_DODGUN );
	CHECK( GC_VMAnimFamily( "models/v_panzerschreck.mdl" ) == GC_VMF_DODGUN );
	CHECK( GC_VMAnimFamily( "models/v_spade.mdl" ) == GC_VMF_CROWBAR );
	CHECK( GC_VMAnimFamily( "models/v_amerk.mdl" ) == GC_VMF_CROWBAR );
	CHECK( GC_VMAnimFamily( "models/v_colt.mdl" ) == GC_VMF_DODGUN );
	CHECK( GC_VMAnimFamily( "models/v_luger.mdl" ) == GC_VMF_DODGUN );
	CHECK( GC_VMAnimIntent( GC_VMF_GLOCK, 3 ).action == GC_VMA_FIRE );
	CHECK( GC_VMAnimIntent( GC_VMF_GLOCK, 7 ).action == GC_VMA_DRAW );
	CHECK( GC_VMAnimFamily( "models/v_9mmAR.mdl" ) == GC_VMF_NONE );
	CHECK( GC_VMAnimFamily( "models/v_98k_x.mdl" ) == GC_VMF_NONE );
	CHECK( GC_VMAnimFamily( NULL ) == GC_VMF_NONE );

	/* HL models keep their own indices */
	CHECK( GC_VMAnimIntent( GC_VMF_NONE, 5 ).action == GC_VMA_NONE );
	CHECK( GC_VMAnimPick( rifle[0].label, sizeof( seq_t ), nr, GC_VMAnimIntent( GC_VMF_NONE, 5 )) == -1 );

	/* CMP5 on a rifle: deploy, idle, three fire variants, reload */
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_MP5, 4 ), "draw" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_MP5, 0 ), "idle" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_MP5, 1 ), "idle" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_MP5, 5 ), "shoot1" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_MP5, 6 ), "shoot2" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_MP5, 7 ), "shoot1" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_MP5, 3 ), "reload" ));

	/* MG carried, not bipod-up */
	CHECK( !strcmp( pick_name( mg, nm, GC_VMF_MP5, 0 ), "idle" ));
	CHECK( !strcmp( pick_name( mg, nm, GC_VMF_MP5, 5 ), "shoot" ));
	CHECK( !strcmp( pick_name( mg, nm, GC_VMF_MP5, 4 ), "draw" ));
	CHECK( !strcmp( pick_name( mg, nm, GC_VMF_MP5, 3 ), "reload" ));

	/* shotgun steps: start plays the reload, shell and pump keep it */
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_SHOTGUN, 5 ), "reload" ));
	CHECK( GC_VMAnimIntent( GC_VMF_SHOTGUN, 3 ).action == GC_VMA_KEEP );
	CHECK( GC_VMAnimIntent( GC_VMF_SHOTGUN, 4 ).action == GC_VMA_KEEP );
	CHECK( pick( rifle, nr, GC_VMF_SHOTGUN, 3 ) == -1 );

	/* crossbow empty variants prefer the empty sequence */
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_CROSSBOW, 1 ), "idle_empty" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_CROSSBOW, 6 ), "shoot_empty" ));
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_CROSSBOW, 0 ), "idle" ));

	/* holster has no sequence: hold the idle */
	CHECK( !strcmp( pick_name( rifle, nr, GC_VMF_RPG, 4 ), "idle" ));

	/* spade swings */
	CHECK( !strcmp( pick_name( spade, ns, GC_VMF_CROWBAR, 3 ), "slash1" ));
	CHECK( !strcmp( pick_name( spade, ns, GC_VMF_CROWBAR, 5 ), "slash2" ));
	CHECK( !strcmp( pick_name( spade, ns, GC_VMF_CROWBAR, 1 ), "draw" ));

	/* grenades: HL handgrenade_e onto the DoD names, plain beats exploding_ */
	CHECK( GC_VMAnimFamily( "models/v_grenade.mdl" ) == GC_VMF_GRENADE );
	CHECK( GC_VMAnimFamily( "models/v_stick.mdl" ) == GC_VMF_GRENADE );
	CHECK( !strcmp( pick_name( grenade, ng, GC_VMF_GRENADE, 0 ), "idle" ));
	CHECK( !strcmp( pick_name( grenade, ng, GC_VMF_GRENADE, 1 ), "idle" ));
	CHECK( !strcmp( pick_name( grenade, ng, GC_VMF_GRENADE, 2 ), "pinpull" ));
	CHECK( !strcmp( pick_name( grenade, ng, GC_VMF_GRENADE, 3 ), "throw" ));
	CHECK( !strcmp( pick_name( grenade, ng, GC_VMF_GRENADE, 5 ), "throw" ));
	CHECK( !strcmp( pick_name( grenade, ng, GC_VMF_GRENADE, 6 ), "holster" ));
	CHECK( !strcmp( pick_name( grenade, ng, GC_VMF_GRENADE, 7 ), "draw" ));

	/* the table-driven DoD gun: dodweapon_anim_e onto each model's own names */
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 0 ), "idle" ));
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 7 ), "empty_idle" ));
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 1 ), "shoot1" ));
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 2 ), "shoot2" ));
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 3 ), "shoot3" ));
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 8 ), "shoot_empty" ));
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 4 ), "reload" ));
	CHECK( !strcmp( pick_name( garand, ngarand, GC_VMF_DODGUN, 5 ), "draw" ));
	CHECK( !strcmp( pick_name( k98, nk98, GC_VMF_DODGUN, 1 ), "shoot" ));	/* not the bayonet stab */
	CHECK( !strcmp( pick_name( k98, nk98, GC_VMF_DODGUN, 3 ), "shoot" ));
	CHECK( !strcmp( pick_name( colt, ncolt, GC_VMF_DODGUN, 4 ), "reload" ));	/* not reload_noshoot */
	CHECK( !strcmp( pick_name( colt, ncolt, GC_VMF_DODGUN, 8 ), "shoot_empty" ));
	CHECK( !strcmp( pick_name( luger, nluger, GC_VMF_DODGUN, 4 ), "reload" ));	/* not reload_empty */
	CHECK( !strcmp( pick_name( luger, nluger, GC_VMF_DODGUN, 1 ), "shoot" ));
	CHECK( !strcmp( pick_name( bazooka, nbazooka, GC_VMF_DODGUN, 1 ), "launch" ));
	CHECK( !strcmp( pick_name( bazooka, nbazooka, GC_VMF_DODGUN, 4 ), "reload_idle" ));	/* carried, not aimed */
	CHECK( !strcmp( pick_name( bazooka, nbazooka, GC_VMF_DODGUN, 0 ), "idle" ));
	CHECK( !strcmp( pick_name( mg42, nmg42, GC_VMF_DODGUN, 0 ), "downidle" ));	/* bipod folded wins */
	CHECK( !strcmp( pick_name( mg42, nmg42, GC_VMF_DODGUN, 7 ), "downidleempty" ));
	CHECK( !strncmp( pick_name( mg42, nmg42, GC_VMF_DODGUN, 1 ), "downshoot", 9 ));
	CHECK( !strcmp( pick_name( mg42, nmg42, GC_VMF_DODGUN, 4 ), "reload" ));
	CHECK( GC_VMAnimIntent( GC_VMF_DODGUN, 6 ).action == GC_VMA_HOLSTER );

	/* every index of every family stays in range, even on a model with no names */
	for( a = -1; a < 40; a++ )
	{
		gc_vm_family_t f;

		for( f = GC_VMF_MP5; f <= GC_VMF_DODGUN; f++ )
		{
			const gc_vm_intent_t intent = GC_VMAnimIntent( f, a );
			const int s1 = GC_VMAnimPick( rifle[0].label, sizeof( seq_t ), nr, intent );
			const int s2 = GC_VMAnimPick( odd[0].label, sizeof( seq_t ), 2, intent );

			if( intent.action == GC_VMA_KEEP )
				continue;
			CHECK( s1 >= 0 && s1 < nr );
			CHECK( s2 >= 0 && s2 < 2 );
		}
	}
	CHECK( GC_VMAnimPick( NULL, sizeof( seq_t ), 0, GC_VMAnimIntent( GC_VMF_MP5, 5 )) == 0 );

	if( failures )
		return 1;
	printf( "vmanim ok\n" );
	return 0;
}
