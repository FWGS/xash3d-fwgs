/*
r_refcontext.c - renderer DLL entry for static GameCube link
Based on ref/common/ref_context.c
*/
#include "ref_common.h"
#include "com_strings.h"

ref_api_t      gEngfuncs;
ref_globals_t *gpGlobals;
ref_client_t  *gp_cl;
ref_host_t    *gp_host;
struct movevars_s *gp_movevars;
uint16_t       rtable[MOD_FRAMES][MOD_FRAMES];
dlight_t      *gp_dlights;
int            g_lightstylevalue[MAX_LIGHTSTYLES];
poolhandle_t   r_temppool;

void GL_InitRandomTable( void )
{
	for( int tu = 0; tu < MOD_FRAMES; tu++ )
	{
		for( int tv = 0; tv < MOD_FRAMES; tv++ )
		{
			rtable[tu][tv] = gEngfuncs.COM_RandomLong( 0, 0x7FFF );
		}
	}

	gEngfuncs.COM_SetRandomSeed( 0 );
}
