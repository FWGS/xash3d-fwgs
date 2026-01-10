/*
r_context.c -- dx2 renderer context
Copyright (C) 2023-2024 a1batross
Copyright (C) 2025 lewa_j

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#define INITGUID
#include "r_local.h"
#include <string.h>
#include "xash3d_types.h"
#include "const.h"
#include "cvardef.h"
#include "xash3d_mathlib.h"
#include "ref_api.h"
#include "com_strings.h"
#include "ref_params.h"

//#pragma comment(lib, "ddraw.lib")
//TODO COM_GetProcAddress

ref_api_t      gEngfuncs;
static ref_globals_t *gpGlobals;
ref_client_t *gp_cl;

poolhandle_t r_temppool;

gl_globals_t	tr;
ref_instance_t	RI;

static void R_SimpleStub( void )
{
	;
}

static void R_SimpleStubInt( int unused )
{
	;
}

static void R_SimpleStubUInt( unsigned int unused )
{
	;
}

static void R_SimpleStubBool( qboolean unused )
{
	;
}

const char *dxResultToStr( HRESULT r )
{
	if( r == DD_OK )
		return "DD_OK";
	if( r == DDERR_INVALIDPARAMS )
		return "DDERR_INVALIDPARAMS";
	if( r == DDERR_INVALIDOBJECT )
		return "DDERR_INVALIDOBJECT";
	if( r == DDERR_UNSUPPORTED )
		return "DDERR_UNSUPPORTED";
	if( r == DDERR_NOCOOPERATIVELEVELSET )
		return "DDERR_NOCOOPERATIVELEVELSET";
	if( r == DDERR_INVALIDCAPS )
		return "DDERR_INVALIDCAPS";
	if( r == DDERR_INCOMPATIBLEPRIMARY )
		return "DDERR_INCOMPATIBLEPRIMARY";
	if( r == DDERR_NOEXCLUSIVEMODE )
		return "DDERR_NOEXCLUSIVEMODE";
	if( r == DDERR_NOFLIPHW )
		return "DDERR_NOFLIPHW";
	if( r == DDERR_OUTOFVIDEOMEMORY )
		return "DDERR_OUTOFVIDEOMEMORY";
	if( r == DDERR_PRIMARYSURFACEALREADYEXISTS )
		return "DDERR_PRIMARYSURFACEALREADYEXISTS";
	if( r == DDERR_UNSUPPORTEDMODE )
		return "DDERR_UNSUPPORTEDMODE";
	if( r == DDERR_NOTFOUND )
		return "DDERR_NOTFOUND";
	if( r == DDERR_INVALIDRECT )
		return "DDERR_INVALIDRECT";
	if( r == D3DERR_VIEWPORTDATANOTSET )
		return "D3DERR_VIEWPORTDATANOTSET";
	if( r == D3DERR_VIEWPORTHASNODEVICE )
		return "D3DERR_VIEWPORTHASNODEVICE";
	if( r == D3DERR_SURFACENOTINVIDMEM )
		return "D3DERR_SURFACENOTINVIDMEM";

	return "UNKNOWN";
}

dx_context_t dxc;

static qboolean DD_CreatePrimarySurface()
{
	DDSURFACEDESC ddsd = { 0 };
	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS;
	ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

	DXCheck(IDirectDraw_CreateSurface(dxc.pdd, &ddsd, &dxc.pddsPrimary, NULL));
	printf("IDirectDraw::CreateSurface(PRIMARY) %p\n", dxc.pddsPrimary);
	if (!dxc.pddsPrimary)
		return false;

	DXCheck(IDirectDraw_CreateClipper(dxc.pdd, 0, &dxc.pddClipper, NULL));
	gEngfuncs.Con_Printf("IDirectDraw::CreateClipper %p\n", dxc.pddClipper);
	if (!dxc.pddClipper)
		return false;

	DXCheck(IDirectDrawClipper_SetHWnd(dxc.pddClipper, 0, dxc.window));
	DXCheck(IDirectDrawSurface_SetClipper(dxc.pddsPrimary, dxc.pddClipper));

	return true;
}

static qboolean DD_CreateBackSurface()
{
	DDSURFACEDESC ddsd = { 0 };
	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
	ddsd.dwWidth = dxc.windowWidth;
	ddsd.dwHeight = dxc.windowHeight;

	DXCheck(IDirectDraw_CreateSurface(dxc.pdd, &ddsd, &dxc.pddsBack, NULL));
	gEngfuncs.Con_Printf("IDirectDraw::CreateSurface(BACK) %p\n", dxc.pddsBack);
	if (!dxc.pddsBack)
		return false;

	DXCheck(IDirectDraw_CreateClipper(dxc.pdd, 0, &dxc.pddBackClipper, NULL));
	gEngfuncs.Con_Printf("IDirectDraw::CreateClipper(BACK) %p\n", dxc.pddBackClipper);
	if (!dxc.pddBackClipper)
		return false;

	struct region_t {
		RGNDATAHEADER rdh;
		RECT rect;
	} region;
	region.rdh.dwSize = sizeof(region.rdh);
	region.rdh.iType = RDH_RECTANGLES;
	region.rdh.nCount = 1;
	region.rdh.nRgnSize = sizeof(RECT);
	region.rdh.rcBound.left = 0;
	region.rdh.rcBound.top = 0;
	region.rdh.rcBound.right = dxc.windowWidth;
	region.rdh.rcBound.bottom = dxc.windowHeight;
	memcpy(&region.rect, &region.rdh.rcBound, sizeof(region.rdh.rcBound));

	DXCheck(IDirectDrawClipper_SetClipList(dxc.pddBackClipper, (RGNDATA *)&region, 0));
	DXCheck(IDirectDrawSurface_SetClipper(dxc.pddsBack, dxc.pddBackClipper));

	return true;
}

static qboolean D3D_Init()
{
	DXCheck(IDirectDraw_QueryInterface(dxc.pdd, &IID_IDirect3D, (void**)&dxc.pd3d));
	gEngfuncs.Con_Printf("IDirectDraw::QueryInterface(IID_IDirect3D) %p\n", dxc.pd3d);

	if (!dxc.pd3d)
		return false;

	DXCheck(IDirect3D_CreateViewport(dxc.pd3d, &dxc.viewport, NULL));
	gEngfuncs.Con_Printf("IDirect3D::CreateViewport %p\n", dxc.viewport);

	if (!dxc.viewport)
		return false;

	return true;
}

static qboolean D3D_InitDevice()
{
	if (!dxc.pd3d)
		return false;

	D3DFINDDEVICESEARCH search = { 0 };
	memset(&search, 0, sizeof(search));
	search.dwSize = sizeof(search);
	search.dwFlags = D3DFDS_COLORMODEL;
	search.dcmColorModel = D3DCOLOR_RGB;
	D3DFINDDEVICERESULT result = { 0 };
	memset(&result, 0, sizeof(result));
	result.dwSize = sizeof(result);

	DXCheck(IDirect3D_FindDevice(dxc.pd3d, &search, &result));
	DXCheck(IDirectDrawSurface_QueryInterface(dxc.pddsBack, &result.guid, (void**)&dxc.pd3dd));
	gEngfuncs.Con_Printf("IDirectDrawSurface::QueryInterface(IDirect3DDevice) %p\n", dxc.pd3dd);

	if (!dxc.pd3dd)
		return false;

	D3DEXECUTEBUFFERDESC ebd = { 0 };
	memset(&ebd, 0, sizeof(ebd));
	ebd.dwSize = sizeof(ebd);
	ebd.dwFlags = D3DDEB_BUFSIZE;
	ebd.dwBufferSize = 1024 * 64;
	DXCheck(IDirect3DDevice_CreateExecuteBuffer(dxc.pd3dd, &ebd, &dxc.pd3deb, NULL));
	gEngfuncs.Con_Printf("IDirect3DDevice::CreateExecuteBuffer %p\n", dxc.pd3deb);

	if (!dxc.pd3deb)
		return false;

	if (!dxc.viewport)
		return false;

	DXCheck(IDirect3DDevice_AddViewport(dxc.pd3dd, dxc.viewport));

	DDSURFACEDESC desc = { 0 };
	memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT;
	DXCheck(IDirectDrawSurface_GetSurfaceDesc(dxc.pddsBack , &desc));

	D3DVIEWPORT viewport = { 0 };
	viewport.dwSize = sizeof(viewport);
	viewport.dwX = 0;
	viewport.dwY = 0;
	viewport.dwWidth = desc.dwWidth;
	viewport.dwHeight = desc.dwHeight;
	viewport.dvScaleX = 1;
	viewport.dvScaleY = 1;
	viewport.dvMaxX = 1;
	viewport.dvMaxY = 1;
	viewport.dvMinZ = 0;
	viewport.dvMaxZ = 1;
	DXCheck(IDirect3DViewport_SetViewport(dxc.viewport, &viewport));

	return true;
}

DEFINE_ENGINE_SHARED_CVAR_LIST()

static qboolean R_Init( void )
{
	memset(&dxc, 0, sizeof(dxc));

	RETRIEVE_ENGINE_SHARED_CVAR_LIST();

	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;

	r_temppool = Mem_AllocPool("Render Zone");

	// create the window
	if (!gEngfuncs.R_Init_Video(REF_D3D))
	{
		Mem_FreePool(&r_temppool);
		return false;
	}

	void *handle = NULL;
	ref_window_type_t wt = gEngfuncs.R_GetWindowHandle( &handle, REF_WINDOW_TYPE_WIN32 );
	gEngfuncs.Con_Printf( "R_Init: WindowHandle %p type %d\n", handle, wt );
	if (wt != REF_WINDOW_TYPE_WIN32 || !handle)
	{
		Mem_FreePool(&r_temppool);
		return false;
	}
	dxc.window = (HWND)handle;

	RECT dstRect = {};
	GetClientRect(dxc.window, &dstRect);
	dxc.windowWidth = dstRect.right - dstRect.left;
	dxc.windowHeight = dstRect.bottom - dstRect.top;

	DXCheck(DirectDrawCreate(NULL, &dxc.pdd, NULL));
	gEngfuncs.Con_Reportf("DirectDrawCreate %p\n", dxc.pdd);
	if (!dxc.pdd)
	{
		Mem_FreePool(&r_temppool);
		return false;
	}

	DXCheck(IDirectDraw_SetCooperativeLevel(dxc.pdd, dxc.window, DDSCL_NORMAL));

	if (!DD_CreatePrimarySurface())
	{
		Mem_FreePool(&r_temppool);
		return false;
	}

	if (!DD_CreateBackSurface())
	{
		Mem_FreePool(&r_temppool);
		return false;
	}

	if (!D3D_Init())
	{
		Mem_FreePool(&r_temppool);
		return false;
	}

	if (!D3D_InitDevice())
	{
		Mem_FreePool(&r_temppool);
		return false;
	}

	dxc.renderMode = kRenderNormal;

	R_InitImages();

	return true;
}

void R_Shutdown(void)
{
	Mem_FreePool(&r_temppool);

	gEngfuncs.R_Free_Video();
}

static void GL_InitExtensions( void )
{
	;
}

static void GL_ClearExtensions( void )
{
	;
}

void R_AllowFog(qboolean allowed)
{

}

void GL_SetRenderMode(int mode)
{
	dxc.renderMode = mode;
}

static const char *R_GetConfigName( void )
{
	return "ref_dx2";
}

static qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int offset_x, int offset_y, float scale_x, float scale_y )
{
	return false;

	qboolean ret = true;
	if (rotate > 0)
	{
		gEngfuncs.Con_Printf("rotation transform not supported\n");
		ret = false;
	}

	if (offset_x || offset_y)
	{
		gEngfuncs.Con_Printf("offset transform not supported\n");
		ret = false;
	}

	if (scale_x != 1.0f || scale_y != 1.0f)
	{
		gEngfuncs.Con_Printf("scale transform not supported\n");
		ret = false;
	}

	return ret;
}

static void GL_SetupAttributes( int safegl )
{
	;
}

static void R_ClearScene(void)
{
	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;

	// clear the scene befor start new frame
	if (gEngfuncs.drawFuncs->R_ClearScene != NULL)
		gEngfuncs.drawFuncs->R_ClearScene();
}

static qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	return true;
}

static void CL_AddCustomBeam( cl_entity_t *pEnvBeam )
{
	;
}

static void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	if (!allocate)
	{
		tr.draw_list->num_solid_entities = 0;
		tr.draw_list->num_trans_entities = 0;
		tr.draw_list->num_beam_entities = 0;

		tr.max_entities = 0;
		tr.entities = NULL;
	}
	else
	{
		tr.max_entities = max_entities;
		tr.entities = entities;
	}

	if (gEngfuncs.drawFuncs->R_ProcessEntData)
		gEngfuncs.drawFuncs->R_ProcessEntData(allocate);
}

static void R_SetupSky( int *skytextures )
{
	;
}

static void R_Set2DMode(qboolean enable)
{

}

static void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{
	;
}

static void D3D_SetVert(D3DTLVERTEX* v, float x, float y, float z, float w, float tu, float tv)
{
	v->sx = x;
	v->sy = y;
	v->sz = z;
	v->rhw = w;
	//v->color = RGBA_MAKE(250, 10, 10, 255);
	v->tu = tu;
	v->tv = tv;
}

static void D3D_SetTri(D3DTRIANGLE *t, int v1, int v2, int v3)
{
	t->v1 = v1;
	t->v2 = v2;
	t->v3 = v3;
	t->wFlags = D3DTRIFLAG_EDGEENABLETRIANGLE;
}

static void D3D_PutInstruction(void **dst, BYTE opcode, BYTE size, WORD count)
{
	D3DINSTRUCTION* inst = (D3DINSTRUCTION*)*dst;
	inst->bOpcode = opcode;
	inst->bSize = size;
	inst->wCount = count;
	*dst = (void*)(((D3DINSTRUCTION*)*dst) + 1);
}

static void D3D_PutProcessVertices(void** dst, DWORD flags, WORD start, WORD count)
{
	D3D_PutInstruction(dst, D3DOP_PROCESSVERTICES, sizeof(D3DPROCESSVERTICES), 1);

	D3DPROCESSVERTICES* pv = (D3DPROCESSVERTICES*)*dst;
	pv->dwFlags = flags;
	pv->wStart = start;
	pv->wDest = start;
	pv->dwCount = count;
	pv->dwReserved = 0;
	*dst = (void*)(((D3DPROCESSVERTICES*)*dst) + 1);
}

static void D3D_PutRenderState(void** dst, D3DRENDERSTATETYPE type, DWORD arg)
{
	D3DSTATE* s = (D3DSTATE*)*dst;

	s->drstRenderStateType = type;
	s->dwArg[0] = arg;
	*dst = (void*)(((D3DSTATE*)*dst) + 1);

}

static void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	if (texnum <= 0 || texnum >= MAX_TEXTURES)
		return;

	dx_texture_t *tex = R_GetTexture(texnum);
	if (!tex->dds)
		return;

#if 0
	RECT dstRect = { x, y, x + w, y + h };
	RECT srcRect = { s1 * tex->width, t1 * tex->height, s2 * tex->width, t2 * tex->height};

	if (dxc.renderMode == kRenderNormal)
	{
		DXCheck(IDirectDrawSurface_Blt(dxc.pddsBack, &dstRect, tex->dds, &srcRect, DDBLT_WAIT, NULL));
	}
	else
	{
		DDCOLORKEY ddck;
		ddck.dwColorSpaceLowValue = 0;
		ddck.dwColorSpaceHighValue = ddck.dwColorSpaceLowValue;
		DXCheck(IDirectDrawSurface_SetColorKey(tex->dds, DDCKEY_SRCBLT, &ddck));

		DXCheck(IDirectDrawSurface_Blt(dxc.pddsBack, &dstRect, tex->dds, &srcRect, DDBLT_WAIT | DDBLT_KEYSRC, NULL));
	}
#else

#define DX2_CREATE_EB 1

	if (!dxc.pd3dd)
		return;

	IDirect3DExecuteBuffer* pd3deb = NULL;
#if DX2_CREATE_EB
	D3DEXECUTEBUFFERDESC ebd = { 0 };
	memset(&ebd, 0, sizeof(ebd));
	ebd.dwSize = sizeof(ebd);
	ebd.dwFlags = D3DDEB_BUFSIZE;
	ebd.dwBufferSize = 512;
	DXCheck(IDirect3DDevice_CreateExecuteBuffer(dxc.pd3dd, &ebd, &pd3deb, NULL));
#else
	pd3deb = dxc.pd3deb;
#endif

	if (!pd3deb)
		return;
	{
		D3DEXECUTEBUFFERDESC ebDesc = { 0 };
		memset(&ebDesc, 0, sizeof(ebDesc));
		ebDesc.dwSize = sizeof(ebDesc);
		ebDesc.dwFlags = D3DDEB_LPDATA;
		DXCheck(IDirect3DExecuteBuffer_Lock(pd3deb, &ebDesc));

		void* cur = ebDesc.lpData;

		const int vertCount = 4;
		D3DTLVERTEX* verts = (D3DTLVERTEX*)cur;

		verts[0].color = RGBA_MAKE(250, 10, 10, 255);
		D3D_SetVert(verts, x, y, 0.5, 1, s1, t1);
		verts[1].color = RGBA_MAKE(10, 250, 10, 255);
		D3D_SetVert(verts + 1, x + w, y, 0.5, 1, s2, t1);
		verts[2].color = RGBA_MAKE(10, 10, 250, 255);
		D3D_SetVert(verts + 2, x + w, y + h, 0.5, 1, s2, t2);
		verts[3].color = RGBA_MAKE(250, 250, 10, 255);
		D3D_SetVert(verts + 3, x, y + h, 0.5, 1, s1, t2);

		cur = (void*)(((D3DTLVERTEX*)cur) + vertCount);

		void* insStart = cur;

		D3D_PutProcessVertices(&cur, D3DPROCESSVERTICES_COPY | D3DPROCESSVERTICES_UPDATEEXTENTS, 0, vertCount);
		//align
		if (!(((ULONG)cur) & 7)) {
			D3D_PutInstruction(&cur, D3DOP_TRIANGLE, sizeof(D3DTRIANGLE), 0);
		}

		D3D_PutInstruction(&cur, D3DOP_STATERENDER, sizeof(D3DSTATE), 4);
		D3D_PutRenderState(&cur, D3DRENDERSTATE_TEXTUREHANDLE, tex->d3dHandle);
		D3D_PutRenderState(&cur, D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_DECAL);

		if (dxc.renderMode == kRenderTransAlpha)
		{
			D3D_PutRenderState(&cur, D3DRENDERSTATE_BLENDENABLE, FALSE);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
		}
		else if (dxc.renderMode == kRenderGlow ||
			dxc.renderMode == kRenderTransAdd)
		{
			D3D_PutRenderState(&cur, D3DRENDERSTATE_BLENDENABLE, TRUE);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
			D3D_PutInstruction(&cur, D3DOP_STATERENDER, sizeof(D3DSTATE), 2);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
		}
		else if (dxc.renderMode == kRenderTransColor || dxc.renderMode == kRenderTransTexture)
		{
			D3D_PutRenderState(&cur, D3DRENDERSTATE_BLENDENABLE, TRUE);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
			D3D_PutInstruction(&cur, D3DOP_STATERENDER, sizeof(D3DSTATE), 2);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}
		else//kRenderNormal
		{
			D3D_PutRenderState(&cur, D3DRENDERSTATE_BLENDENABLE, FALSE);
			D3D_PutRenderState(&cur, D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
		}

		D3D_PutInstruction(&cur, D3DOP_TRIANGLE, sizeof(D3DTRIANGLE), 2);

		D3DTRIANGLE* tris = (D3DTRIANGLE*)cur;
		D3D_SetTri(tris, 0, 1, 2);
		D3D_SetTri(tris + 1, 0, 2, 3);
		cur = (void*)(((D3DTRIANGLE*)cur) + 2);

		D3D_PutInstruction(&cur, D3DOP_EXIT, 0, 0);

		DXCheck(IDirect3DExecuteBuffer_Unlock(pd3deb));

		D3DEXECUTEDATA exData = { 0 };
		memset(&exData, 0, sizeof(exData));
		exData.dwSize = sizeof(exData);
		exData.dwVertexCount = vertCount;
		exData.dwVertexOffset = 0;
		exData.dwInstructionOffset = ((char*)insStart - (char*)ebDesc.lpData);
		exData.dwInstructionLength = ((char*)cur - (char*)insStart);
		DXCheck(IDirect3DExecuteBuffer_SetExecuteData(pd3deb , &exData));
	}

	DXCheck(IDirect3DDevice_BeginScene(dxc.pd3dd));
	DXCheck(IDirect3DDevice_Execute(dxc.pd3dd, pd3deb, dxc.viewport, D3DEXECUTE_CLIPPED));
	DXCheck(IDirect3DDevice_EndScene(dxc.pd3dd));

#if DX2_CREATE_EB
	if (pd3deb && pd3deb != dxc.pd3deb)
		IDirect3DExecuteBuffer_Release(pd3deb);
#endif
#endif
}

static void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a )
{
	RECT dstRect = { x, y, x + w, y + h };
	DDBLTFX bltFx = { 0 };
	bltFx.dwSize = sizeof(bltFx);
	bltFx.dwFillColor = RGBA_MAKE(r, g, b, a);
	DXCheck(IDirectDrawSurface_Blt(dxc.pddsBack, &dstRect, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &bltFx));
}

static int  WorldToScreen( const vec3_t world, vec3_t screen )
{
	VectorClear( screen );
	return 0;
}

static qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	return false;
}

static qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	return false;
}

static colorVec R_LightPoint( const float *p )
{
	colorVec c = { 0 };
	return c;
}

static void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale )
{
	;
}

static int R_CreateDecalList( struct decallist_s *pList )
{
	return 0;
}

static float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time )
{
	return 0.0f;
}

static void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
{
	;
}

void CL_InitStudioAPI(void)
{
	;
}

static void R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	;
}

static void GL_SubdivideSurface( model_t *mod, msurface_t *fa )
{
	;
}

static void CL_RunLightStyles( lightstyle_t *ls )
{

}

static void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	if( frameWidth )
		*frameWidth	= 0;

	if( frameHeight )
		*frameHeight = 0;

	if( numFrames )
		*numFrames = 0;
}

static int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	return 0;
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buffer, size_t buffersize )
{
	gEngfuncs.Con_Printf("Mod_ProcessRenderData(%p(%s), %d, %p, %zu)\n", mod, mod->name, create, buffer, buffersize);
	return true;
}

static void Mod_StudioLoadTextures( model_t *mod, void *data )
{
	;
}

static void CL_DrawParticles( double frametime, particle_t *particles, float partsize )
{
	;
}

static void CL_DrawTracers( double frametime, particle_t *tracers )
{
	;
}

static void CL_DrawBeams( int fTrans, BEAM *beams )
{
	;
}

static qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly )
{
	return false;
}

static int RefGetParm(int parm, int arg)
{
	dx_texture_t *glt;

	switch (parm)
	{
	case PARM_TEX_WIDTH:
		glt = R_GetTexture(arg);
		return glt->width;
	case PARM_TEX_HEIGHT:
		glt = R_GetTexture(arg);
		return glt->height;
	case PARM_TEX_SRC_WIDTH:
		glt = R_GetTexture(arg);
		return glt->srcWidth;
	case PARM_TEX_SRC_HEIGHT:
		glt = R_GetTexture(arg);
		return glt->srcHeight;
	case PARM_TEX_GLFORMAT:
		glt = R_GetTexture(arg);
		return 0;
	case PARM_TEX_ENCODE:
		glt = R_GetTexture(arg);
		return 0;
	case PARM_TEX_MIPCOUNT:
		glt = R_GetTexture(arg);
		return glt->numMips;
	case PARM_TEX_DEPTH:
		glt = R_GetTexture(arg);
		return 1;
#if 0
	case PARM_TEX_SKYBOX:
		Assert(arg >= 0 && arg < 6);
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound(0, arg, MAX_LIGHTMAPS - 1);
		return tr.lightmapTextures[arg];
#endif
	case PARM_TEX_TARGET:
		glt = R_GetTexture(arg);
		return 0;
	case PARM_TEX_TEXNUM:
		glt = R_GetTexture(arg);
		return arg;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture(arg);
		return glt->flags;
#if 0
	case PARM_TEX_MEMORY:
		return GL_TexMemory();
	case PARM_ACTIVE_TMU:
		return glState.activeTMU;
	case PARM_LIGHTSTYLEVALUE:
		arg = bound(0, arg, MAX_LIGHTSTYLES - 1);
		return tr.lightstylevalue[arg];
	case PARM_MAX_IMAGE_UNITS:
		return GL_MaxTextureUnits();
	case PARM_REBUILD_GAMMA:
		return glConfig.softwareGammaUpdate;
	case PARM_GL_CONTEXT_TYPE:
		return glConfig.context;
	case PARM_GLES_WRAPPER:
		return glConfig.wrapper;
	case PARM_STENCIL_ACTIVE:
		return glState.stencilEnabled;
#endif
	case PARM_TEX_FILTERING:
		return 0;
#if 0
		if (arg < 0)
			return gl_texture_nearest.value == 0.0f;

		return GL_TextureFilteringEnabled(R_GetTexture(arg));
#endif
	default:
		return (*gEngfuncs.EngineGetParm)(parm, arg);
	}
	return 0;
}


static float GetFrameTime( void )
{
	return 0.0f;
}

static void R_SetCurrentEntity( struct cl_entity_s *ent )
{
	;
}

static void R_SetCurrentModel( struct model_s *mod )
{
	;
}

static void DrawSingleDecal( struct decal_s *pDecal, struct msurface_s *fa )
{
	;
}

static float *R_DecalSetupVerts( struct decal_s *pDecal, struct msurface_s *surf, int texture, int *outCount )
{
	return NULL;
}

static void R_EntityRemoveDecals( struct model_s *mod )
{
	;
}

static void AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data )
{
	;
}

static void GL_Bind( int tmu, unsigned int texnum )
{
	;
}

static void GL_LoadTextureMatrix( const float *glmatrix )
{
	;
}

static void GL_TexGen( unsigned int coord, unsigned int mode )
{
	;
}

static void GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
	;
}

static void GL_DrawParticles( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime )
{
	;
}

static colorVec LightVec( const float *start, const float *end, float *lightspot, float *lightvec )
{
	colorVec c = { 0 };
	return c;
}

static struct mstudiotex_s *StudioGetTexture( struct cl_entity_s *e )
{
	return NULL;
}

void R_BeginFrame(qboolean clearScene)
{

	gEngfuncs.CL_ExtraUpdate();
}

void R_EndFrame(void)
{
	//swap
	if (!dxc.window)
		return;

	if( dxc.pddsBack )
	{
		RECT dstRect = {};
		POINT point = { 0, 0 };
		ClientToScreen( dxc.window, &point );
		GetClientRect( dxc.window, &dstRect );
		OffsetRect( &dstRect, point.x, point.y );

		RECT srcRect = { 0, 0, dxc.windowWidth, dxc.windowHeight };
		DXCheck( IDirectDrawSurface_Blt( dxc.pddsPrimary, &dstRect, dxc.pddsBack, &srcRect, DDBLT_WAIT, NULL ) );
	}

	//resize
	RECT rect = {};
	GetClientRect(dxc.window, &rect);
	int w = rect.right - rect.left;
	int h = rect.bottom - rect.top;

	if (w != dxc.windowWidth || h != dxc.windowHeight)
	{
		dxc.windowWidth = w;
		dxc.windowHeight = h;

		if( dxc.pd3deb )
			IDirect3DExecuteBuffer_Release(dxc.pd3deb);
		dxc.pd3deb = NULL;
		if( dxc.pd3dd )
			IDirect3DDevice_Release(dxc.pd3dd);
		dxc.pd3dd = NULL;
		if( dxc.pddsBack )
			IDirectDrawSurface_Release(dxc.pddsBack);
		dxc.pddsBack = NULL;
		if( dxc.pddBackClipper )
			IDirectDrawClipper_Release(dxc.pddBackClipper);
		dxc.pddBackClipper = NULL;
		if( DD_CreateBackSurface() )
		{
			if( D3D_InitDevice() )
			{
			}
		}
	}
}

void GL_BackendStartFrame(void)
{

}

void GL_BackendEndFrame(void)
{

}


static void R_SetupRefParams(const ref_viewpass_t *rvp)
{
//	RI.params = RP_NONE;
	RI.drawWorld = FBitSet(rvp->flags, RF_DRAW_WORLD);
	RI.onlyClientDraw = FBitSet(rvp->flags, RF_ONLY_CLIENTDRAW);
	RI.farClip = 0;

	if (!FBitSet(rvp->flags, RF_DRAW_CUBEMAP))
		RI.drawOrtho = FBitSet(rvp->flags, RF_DRAW_OVERVIEW);
	else RI.drawOrtho = false;

	// setup viewport
	RI.viewport[0] = rvp->viewport[0];
	RI.viewport[1] = rvp->viewport[1];
	RI.viewport[2] = rvp->viewport[2];
	RI.viewport[3] = rvp->viewport[3];

	// calc FOV
	RI.fov_x = rvp->fov_x;
	RI.fov_y = rvp->fov_y;

	VectorCopy(rvp->vieworigin, RI.vieworg);
	VectorCopy(rvp->viewangles, RI.viewangles);
	VectorCopy(rvp->vieworigin, RI.pvsorigin);
}

static void R_RenderScene(void)
{
	model_t *worldmodel = gp_cl->models[1];
	if (!worldmodel && RI.drawWorld)
		gEngfuncs.Host_Error("%s: NULL worldmodel\n", __func__);

	// frametime is valid only for normal pass
	if (FBitSet(RI.params, RP_ENVVIEW) == 0)
		tr.frametime = gp_cl->time - gp_cl->oldtime;
	else tr.frametime = 0.0;

	// begin a new frame
	tr.framecount++;

#if 0
	R_PushDlights();

	R_SetupFrustum();
	R_SetupFrame();
	R_SetupGL(true);
	R_Clear(~0);

	R_MarkLeaves();
	R_DrawFog();
	if (RI.drawWorld)
		R_AnimateRipples();

	R_CheckGLFog();
	R_DrawWorld();
	R_CheckFog();

	gEngfuncs.CL_ExtraUpdate();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList();

	R_DrawWaterSurfaces();
#endif
}

static void R_RenderFrame( const struct ref_viewpass_s *rvp )
{
	if (r_norefresh->value)
		return;

	// setup the initial render params
	R_SetupRefParams(rvp);

	// completely override rendering
	if (gEngfuncs.drawFuncs->GL_RenderFrame != NULL)
	{
		tr.fCustomRendering = true;

		if (gEngfuncs.drawFuncs->GL_RenderFrame(rvp))
		{
			//R_GatherPlayerLight();
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	//if (!RI.onlyClientDraw)
	//	R_RunViewmodelEvents();

	tr.realframecount++; // right called after viewmodel events
	R_RenderScene();
}

static void GL_OrthoBounds( const float *mins, const float *maxs )
{
	;
}

static qboolean R_SpeedsMessage( char *out, size_t size )
{
	return false;
}

static byte *Mod_GetCurrentVis( void )
{
	return NULL;
}

static void R_NewMap(void)
{

	if (gEngfuncs.drawFuncs->R_NewMap != NULL)
		gEngfuncs.drawFuncs->R_NewMap();
}

static void *R_GetProcAddress( const char *name )
{
	return NULL;
}

static void Color4f( float r, float g, float b, float a )
{
	;
}

static void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	;
}

static void TexCoord2f( float u, float v )
{
	;
}

static void Vertex3fv( const float *worldPnt )
{
	;
}

static void Vertex3f( float x, float y, float z )
{
	;
}

static void Fog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	;
}

static void ScreenToWorld( const float *screen, float *world  )
{
	;
}

static void GetMatrix( const int pname, float *matrix )
{
	;
}

static void FogParams( float flDensity, int iFogSkybox )
{
	;
}

static void CullFace( TRICULLSTYLE mode )
{
	;
}

static void VGUI_SetupDrawing( qboolean rect )
{
	;
}

static void VGUI_UploadTextureBlock( int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
	;
}

static const ref_interface_t gReffuncs =
{
	.R_Init                = R_Init,
	.R_Shutdown            = R_Shutdown,
	.R_GetConfigName       = R_GetConfigName,
	.R_SetDisplayTransform = R_SetDisplayTransform,

	.GL_SetupAttributes = GL_SetupAttributes,
	.GL_InitExtensions  = GL_InitExtensions,
	.GL_ClearExtensions = GL_ClearExtensions,

	.R_GammaChanged       = R_SimpleStubBool,
	.R_BeginFrame         = R_BeginFrame,
	.R_RenderScene        = R_RenderScene,
	.R_EndFrame           = R_EndFrame,
	.R_PushScene          = R_SimpleStub,
	.R_PopScene           = R_SimpleStub,
	.GL_BackendStartFrame = GL_BackendStartFrame,
	.GL_BackendEndFrame   = GL_BackendEndFrame,

	.R_ClearScreen    = R_SimpleStub,
	.R_AllowFog       = R_AllowFog,
	.GL_SetRenderMode = GL_SetRenderMode,

	.R_AddEntity      = R_AddEntity,
	.CL_AddCustomBeam = CL_AddCustomBeam,
	.R_ProcessEntData = R_ProcessEntData,
	.R_Flush          = R_SimpleStubUInt,

	.R_ShowTextures = R_ShowTextures,

	.R_GetTextureOriginalBuffer = R_GetTextureOriginalBuffer,
	.GL_LoadTextureFromBuffer   = GL_LoadTextureFromBuffer,
	.GL_ProcessTexture          = GL_ProcessTexture,
	.R_SetupSky                 = R_SetupSky,

	.R_Set2DMode      = R_Set2DMode,
	.R_DrawStretchRaw = R_DrawStretchRaw,
	.R_DrawStretchPic = R_DrawStretchPic,
	.FillRGBA         = FillRGBA,
	.WorldToScreen    = WorldToScreen,

	.VID_ScreenShot  = VID_ScreenShot,
	.VID_CubemapShot = VID_CubemapShot,

	.R_LightPoint = R_LightPoint,

	.R_DecalShoot      = R_DecalShoot,
	.R_DecalRemoveAll  = R_SimpleStubInt,
	.R_CreateDecalList = R_CreateDecalList,
	.R_ClearAllDecals  = R_SimpleStub,

	.R_StudioEstimateFrame = R_StudioEstimateFrame,
	.R_StudioLerpMovement  = R_StudioLerpMovement,
	.CL_InitStudioAPI      = CL_InitStudioAPI,

	.R_SetSkyCloudsTextures     = R_SetSkyCloudsTextures,
	.GL_SubdivideSurface = GL_SubdivideSurface,
	.CL_RunLightStyles   = CL_RunLightStyles,

	.R_GetSpriteParms    = R_GetSpriteParms,
	.R_GetSpriteTexture  = R_GetSpriteTexture,

	.Mod_ProcessRenderData  = Mod_ProcessRenderData,
	.Mod_StudioLoadTextures = Mod_StudioLoadTextures,

	.CL_DrawParticles = CL_DrawParticles,
	.CL_DrawTracers   = CL_DrawTracers,
	.CL_DrawBeams     = CL_DrawBeams,
	.R_BeamCull       = R_BeamCull,

	.RefGetParm               = RefGetParm,
	.GetDetailScaleForTexture = GetDetailScaleForTexture,
	.GetExtraParmsForTexture  = GetExtraParmsForTexture,
	.GetFrameTime             = GetFrameTime,

	.R_SetCurrentEntity = R_SetCurrentEntity,
	.R_SetCurrentModel  = R_SetCurrentModel,

	.GL_FindTexture        = GL_FindTexture,
	.GL_TextureName        = GL_TextureName,
	.GL_TextureData        = GL_TextureData,
	.GL_LoadTexture        = GL_LoadTexture,
	.GL_CreateTexture      = GL_CreateTexture,
	.GL_LoadTextureArray   = GL_LoadTextureArray,
	.GL_CreateTextureArray = GL_CreateTextureArray,
	.GL_FreeTexture        = GL_FreeTexture,
	.R_OverrideTextureSourceSize = R_OverrideTextureSourceSize,

	.DrawSingleDecal      = DrawSingleDecal,
	.R_DecalSetupVerts    = R_DecalSetupVerts,
	.R_EntityRemoveDecals = R_EntityRemoveDecals,

	.AVI_UploadRawFrame = AVI_UploadRawFrame,

	.GL_Bind                = GL_Bind,
	.GL_SelectTexture       = R_SimpleStubInt,
	.GL_LoadTextureMatrix   = GL_LoadTextureMatrix,
	.GL_TexMatrixIdentity   = R_SimpleStub,
	.GL_CleanUpTextureUnits = R_SimpleStubInt,
	.GL_TexGen              = GL_TexGen,
	.GL_TextureTarget       = R_SimpleStubUInt,
	.GL_TexCoordArrayMode   = R_SimpleStubUInt,
	.GL_UpdateTexSize       = GL_UpdateTexSize,
	.GL_Reserved0           = NULL,
	.GL_Reserved1           = NULL,

	.GL_DrawParticles = GL_DrawParticles,
	.LightVec         = LightVec,
	.StudioGetTexture = StudioGetTexture,

	.GL_RenderFrame    = R_RenderFrame,
	.GL_OrthoBounds    = GL_OrthoBounds,
	.R_SpeedsMessage   = R_SpeedsMessage,
	.Mod_GetCurrentVis = Mod_GetCurrentVis,
	.R_NewMap          = R_NewMap,
	.R_ClearScene      = R_ClearScene,
	.R_GetProcAddress  = R_GetProcAddress,

	.TriRenderMode = R_SimpleStubInt,
	.Begin         = R_SimpleStubInt,
	.End           = R_SimpleStub,
	.Color4f       = Color4f,
	.Color4ub      = Color4ub,
	.TexCoord2f    = TexCoord2f,
	.Vertex3fv     = Vertex3fv,
	.Vertex3f      = Vertex3f,
	.Fog           = Fog,
	.ScreenToWorld = ScreenToWorld,
	.GetMatrix     = GetMatrix,
	.FogParams     = FogParams,
	.CullFace      = CullFace,

	.VGUI_SetupDrawing   = VGUI_SetupDrawing,
	.VGUI_UploadTextureBlock = VGUI_UploadTextureBlock,
};

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals );
int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	*funcs = gReffuncs;
	gEngfuncs = *engfuncs;
	gpGlobals = globals;
	gp_cl = (ref_client_t *)gEngfuncs.EngineGetParm(PARM_GET_CLIENT_PTR, 0);

	return REF_API_VERSION;
}
