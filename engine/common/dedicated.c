/*
dedicated.c - stubs for dedicated server
Copyright (C) 2018 a1batross, mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/ 
#ifdef XASH_DEDICATED
#include "common.h"
#include "mathlib.h"

const char *svc_strings[256] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_changing",
	"svc_version",
	"svc_setview",
	"svc_sound",
	"svc_time",
	"svc_print",
	"svc_stufftext",
	"svc_setangle",
	"svc_serverdata",
	"svc_lightstyle",
	"svc_updateuserinfo",
	"svc_deltatable",
	"svc_clientdata",
	"svc_stopsound",
	"svc_updatepings",
	"svc_particle",
	"svc_restoresound",
	"svc_spawnstatic",
	"svc_event_reliable",
	"svc_spawnbaseline",
	"svc_temp_entity",
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_event",
	"svc_soundindex",
	"svc_ambientsound",
	"svc_intermission",
	"svc_modelindex",
	"svc_cdtrack",
	"svc_serverinfo",
	"svc_eventindex",
	"svc_weaponanim",
	"svc_bspdecal",
	"svc_roomtype",
	"svc_addangle",
	"svc_usermessage",
	"svc_packetentities",
	"svc_deltapacketentities",
	"svc_chokecount",
	"svc_resourcelist",
	"svc_deltamovevars",
	"svc_customization",
	"svc_unused46",
	"svc_crosshairangle",
	"svc_soundfade",
	"svc_unused49",
	"svc_unused50",
	"svc_director",
	"svc_studiodecal",
	"svc_unused53",
	"svc_unused54",
	"svc_unused55",
	"svc_unused56",
	"svc_querycvarvalue",
	"svc_querycvarvalue2",
	"svc_unused59",
	"svc_unused60",
	"svc_unused61",
	"svc_unused62",
	"svc_unused63",
};

int CL_Active( void )
{
	return false;
}

qboolean CL_Initialized( void )
{
	return false;
}

qboolean CL_IsInGame( void )
{
	return true;	// always active for dedicated servers
}

qboolean CL_IsInMenu( void )
{
	return false;
}

qboolean CL_IsInConsole( void )
{
	return false;
}

qboolean CL_IsIntermission( void )
{
	return false;
}

qboolean CL_IsPlaybackDemo( void )
{
	return false;
}

qboolean CL_IsRecordDemo( void )
{
	return false;
}


qboolean CL_DisableVisibility( void )
{
	return false;
}

qboolean CL_IsBackgroundDemo( void )
{
	return false;
}

qboolean CL_IsBackgroundMap( void )
{
	return false;
}

void CL_Init()
{

}

void Key_Init()
{

}

void IN_Init()
{

}

void CL_Drop()
{

}

void CL_ClearEdicts()
{

}

void Key_SetKeyDest(int key_dest)
{

}

void UI_SetActiveMenu( qboolean fActive )
{

}

void CL_WriteMessageHistory()
{

}

void Host_ClientBegin()
{

}

void Host_ClientFrame()
{

}

void Host_InputFrame()
{
	Cbuf_Execute();
}

void VID_InitDefaultResolution()
{
	
}

void Con_Init()
{
	
}

void R_ClearAllDecals()
{

}
int R_CreateDecalList( struct decallist_s *pList )
{
	return 0;
}

void S_StopSound(int entnum, int channel, const char *soundname)
{

}

int S_GetCurrentStaticSounds( soundlist_t *pout, int size )
{
	return 0;
}

int CL_GetMaxClients()
{
	return 0;
}

void IN_TouchInitConfig()
{

}

void CL_Disconnect()
{

}

void CL_Shutdown()
{

}

void R_ClearStaticEntities()
{

}

void Host_Credits()
{

}

qboolean UI_CreditsActive()
{
	return false;
}

void GL_FreeImage( const char *name )
{

}

void S_StopBackgroundTrack()
{

}

void SCR_BeginLoadingPlaque( qboolean is_background )
{

}

int S_GetCurrentDynamicSounds( soundlist_t *pout, int size )
{
	return 0;
}

void S_StopAllSounds( qboolean ambient )
{

}

void Con_NPrintf( int idx, const char *fmt, ... )
{

}

void Con_NXPrintf( struct  con_nprint_s *info, const char *fmt, ... )
{

}

const byte *GL_TextureData( unsigned int texnum )
{
	return NULL;
}

void SCR_CheckStartupVids()
{

}

imgfilter_t *R_FindTexFilter( const char *texname )
{
	return NULL;
}

#include "sprite.h"

/*
====================
Mod_LoadSpriteModel

load sprite model
====================
*/
void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags )
{
	dsprite_q1_t	*pinq1;
	dsprite_hl_t	*pinhl;
	dsprite_t		*pin;
	short		*numi = NULL;
	dframetype_t	*pframetype;
	msprite_t		*psprite;
	int		i, size;

	if( loaded ) *loaded = false;
	pin = (dsprite_t *)buffer;
	mod->type = mod_sprite;
	i = pin->version;

	if( pin->ident != IDSPRITEHEADER )
	{
		MsgDev( D_ERROR, "%s has wrong id (%x should be %x)\n", mod->name, pin->ident, IDSPRITEHEADER );
		return;
	}
		
	if( i != SPRITE_VERSION_Q1 && i != SPRITE_VERSION_HL && i != SPRITE_VERSION_32 )
	{
		MsgDev( D_ERROR, "%s has wrong version number (%i should be %i or %i)\n", mod->name, i, SPRITE_VERSION_Q1, SPRITE_VERSION_HL );
		return;
	}

	mod->mempool = Mem_AllocPool( va( "^2%s^7", mod->name ));

	if( i == SPRITE_VERSION_Q1 || i == SPRITE_VERSION_32 )
	{
		pinq1 = (dsprite_q1_t *)buffer;
		size = sizeof( msprite_t ) + ( pinq1->numframes - 1 ) * sizeof( psprite->frames );
		psprite = Mem_Alloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinq1->type;
		psprite->texFormat = SPR_ADDITIVE;	//SPR_ALPHTEST;
		psprite->numframes = mod->numframes = pinq1->numframes;
		psprite->facecull = SPR_CULL_FRONT;
		psprite->radius = pinq1->boundingradius;
		psprite->synctype = pinq1->synctype;

		mod->mins[0] = mod->mins[1] = -pinq1->bounds[0] * 0.5f;
		mod->maxs[0] = mod->maxs[1] = pinq1->bounds[0] * 0.5f;
		mod->mins[2] = -pinq1->bounds[1] * 0.5f;
		mod->maxs[2] = pinq1->bounds[1] * 0.5f;
		numi = NULL;
	}
	else if( i == SPRITE_VERSION_HL )
	{
		pinhl = (dsprite_hl_t *)buffer;
		size = sizeof( msprite_t ) + ( pinhl->numframes - 1 ) * sizeof( psprite->frames );
		psprite = Mem_Alloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinhl->type;
		psprite->texFormat = pinhl->texFormat;
		psprite->numframes = mod->numframes = pinhl->numframes;
		psprite->facecull = pinhl->facetype;
		psprite->radius = pinhl->boundingradius;
		psprite->synctype = pinhl->synctype;

		mod->mins[0] = mod->mins[1] = -pinhl->bounds[0] * 0.5f;
		mod->maxs[0] = mod->maxs[1] = pinhl->bounds[0] * 0.5f;
		mod->mins[2] = -pinhl->bounds[1] * 0.5f;
		mod->maxs[2] = pinhl->bounds[1] * 0.5f;
		numi = (short *)(pinhl + 1);
	}

	if( host.type == HOST_DEDICATED )
	{
		// skip frames loading
		if( loaded ) *loaded = true;	// done
		psprite->numframes = 0;
		return;
	}

	if( numi == NULL )
	{
		rgbdata_t	*pal;
	
		pal = FS_LoadImage( "#id.pal", (byte *)&i, 768 );
		pframetype = (dframetype_t *)(pinq1 + 1);
		FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else if( *numi == 256 )
	{	
		byte	*src = (byte *)(numi+1);
		rgbdata_t	*pal;
	
		// install palette
		switch( psprite->texFormat )
		{
                    case SPR_INDEXALPHA:
			pal = FS_LoadImage( "#gradient.pal", src, 768 ); 
			break;
		case SPR_ALPHTEST:		
			pal = FS_LoadImage( "#masked.pal", src, 768 );
                              break;
		default:
			pal = FS_LoadImage( "#normal.pal", src, 768 );
			break;
		}

		pframetype = (dframetype_t *)(src + 768);
		FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else 
	{
		MsgDev( D_ERROR, "%s has wrong number of palette colors %i (should be 256)\n", mod->name, *numi );
		return;
	}

	if( mod->numframes < 1 )
	{
		MsgDev( D_ERROR, "%s has invalid # of frames: %d\n", mod->name, mod->numframes );
		return;
	}

	if( loaded ) *loaded = true;	// done
}

/*
====================
Mod_UnloadSpriteModel

release sprite model and frames
====================
*/
void Mod_UnloadSpriteModel( model_t *mod )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;	
	mspriteframe_t	*pspriteframe;
	int		i, j;

	Assert( mod != NULL );

#ifndef XASH_DEDICATED
	if( mod->type == mod_sprite )
	{
		if( host.type != HOST_DEDICATED )
		{
			psprite = mod->cache.data;

			if( psprite )
			{
				// release all textures
				for( i = 0; i < psprite->numframes; i++ )
				{
					if( psprite->frames[i].type == SPR_SINGLE )
					{
						pspriteframe = psprite->frames[i].frameptr;
						GL_FreeTexture( pspriteframe->gl_texturenum );
					}
					else
					{
						pspritegroup = (mspritegroup_t *)psprite->frames[i].frameptr;

						for( j = 0; j < pspritegroup->numframes; j++ )
						{
							pspriteframe = pspritegroup->frames[i];
							GL_FreeTexture( pspriteframe->gl_texturenum );
						}
					}
				}
			}
		}
	}
#endif

	Mem_FreePool( &mod->mempool );
	memset( mod, 0, sizeof( *mod ));
}

#endif // XASH_DEDICATED