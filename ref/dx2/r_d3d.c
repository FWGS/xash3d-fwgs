/*
r_d3d.c -- dx2 renderer d3d related code
Copyright (C) 2025-2026 lewa_j

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "r_local.h"
#include "xash3d_mathlib.h"

CVAR_DEFINE_AUTO( r_novis, "0", 0, "ignore vis information (perfomance test)" );
CVAR_DEFINE_AUTO( r_nocull, "0", 0, "ignore frustrum culling (perfomance test)" );
CVAR_DEFINE_AUTO( r_lockpvs, "0", FCVAR_CHEAT, "lockpvs area at current point (pvs test)" );
CVAR_DEFINE_AUTO( r_lockfrustum, "0", FCVAR_CHEAT, "lock frustrum area at current point (cull test)" );

dx_globals_t tr;
dx_context_t dxc;

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

static qboolean DD_CreatePrimarySurface( void )
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

static qboolean DD_CreateBackSurface( void )
{
	DDCAPS ddcaps =
	{
		.dwSize = sizeof(ddcaps)
	};
	DDCAPS helcaps =
	{
		.dwSize = sizeof(helcaps)
	};
	DXCheck(IDirectDraw_GetCaps(dxc.pdd, &ddcaps, &helcaps));
	gEngfuncs.Con_Printf("IDirectDraw::GetCaps\n");
	gEngfuncs.Con_Printf("ZBUFFER: %s\n", (ddcaps.ddsCaps.dwCaps & DDSCAPS_ZBUFFER) ? "- ^2enabled" : "- ^1failed");
	if (ddcaps.ddsCaps.dwCaps & DDSCAPS_ZBUFFER)
		gEngfuncs.Con_Printf("ZBufferBitDepths: 0x%X\n", ddcaps.dwZBufferBitDepths);
	if (helcaps.ddsCaps.dwCaps & DDSCAPS_ZBUFFER)
		gEngfuncs.Con_Printf("HEL ZBufferBitDepths: 0x%X\n", helcaps.dwZBufferBitDepths);

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

	struct region_t
	{
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

	int bitDepths = ddcaps.dwZBufferBitDepths;
	if (!bitDepths)
		bitDepths = helcaps.dwZBufferBitDepths;
	if( (ddcaps.ddsCaps.dwCaps & DDSCAPS_ZBUFFER) && bitDepths )
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
		ddsd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
		ddsd.dwWidth = dxc.windowWidth;
		ddsd.dwHeight = dxc.windowHeight;
		ddsd.dwZBufferBitDepth = 16;
		if(bitDepths & DDBD_16 )
			ddsd.dwZBufferBitDepth = 16;
		else if(bitDepths & DDBD_24 )
			ddsd.dwZBufferBitDepth = 24;
		else if(bitDepths & DDBD_32 )
			ddsd.dwZBufferBitDepth = 32;
		DXCheck(IDirectDraw_CreateSurface(dxc.pdd, &ddsd, &dxc.pddsZBuffer, NULL));
		gEngfuncs.Con_Printf("IDirectDraw::CreateSurface(ZBUFFER %dbit) %p\n", ddsd.dwZBufferBitDepth, dxc.pddsZBuffer);
		if( !dxc.pddsZBuffer )
			return false;
		DXCheck(IDirectDrawSurface_AddAttachedSurface(dxc.pddsBack, dxc.pddsZBuffer));
	}

	return true;
}

static qboolean D3D_Init( void )
{
	DXCheck(IDirectDraw_QueryInterface(dxc.pdd, &IID_IDirect3D, (void**)&dxc.pd3d));
	gEngfuncs.Con_Printf("IDirectDraw::QueryInterface(IID_IDirect3D) %p\n", dxc.pd3d);

	if( !dxc.pd3d )
		return false;

	DXCheck(IDirect3D_CreateViewport(dxc.pd3d, &dxc.viewport, NULL));
	gEngfuncs.Con_Printf("IDirect3D::CreateViewport %p\n", dxc.viewport);

	if( !dxc.viewport )
		return false;

	return true;
}

static qboolean D3D_InitDevice( void )
{
	if( !dxc.pd3d )
		return false;

	D3DFINDDEVICESEARCH search = { 0 };
	memset(&search, 0, sizeof(search));
	search.dwSize = sizeof(search);
	search.dwFlags = D3DFDS_COLORMODEL;
	search.dcmColorModel = D3DCOLOR_RGB;
#if 0
	search.dwFlags |= D3DFDS_HARDWARE;
	search.bHardware = FALSE;
#endif
	D3DFINDDEVICERESULT result = { 0 };
	memset(&result, 0, sizeof(result));
	result.dwSize = sizeof(result);

	DXCheck(IDirect3D_FindDevice(dxc.pd3d, &search, &result));
	DXCheck(IDirectDrawSurface_QueryInterface(dxc.pddsBack, &result.guid, (void**)&dxc.pd3dd));
	gEngfuncs.Con_Printf("IDirectDrawSurface::QueryInterface(IDirect3DDevice) %p\n", dxc.pd3dd);
	gEngfuncs.Con_Printf("DeviceZBufferBitDepth: 0x%X\n", result.ddHwDesc.dwDeviceZBufferBitDepth);
	gEngfuncs.Con_Printf("SW DeviceZBufferBitDepth: 0x%X\n", result.ddSwDesc.dwDeviceZBufferBitDepth);

	if( !dxc.pd3dd )
		return false;

	D3DDEVICEDESC d3ddesc =
	{
		.dwSize = sizeof(d3ddesc)
	};
	D3DDEVICEDESC held3ddesc =
	{
		.dwSize = sizeof(held3ddesc)
	};
	DXCheck(IDirect3DDevice_GetCaps(dxc.pd3dd, &d3ddesc, &held3ddesc));
	gEngfuncs.Con_Printf("IDirect3DDevice::GetCaps\n");
	gEngfuncs.Con_Printf("DeviceZBufferBitDepth: 0x%X\n", result.ddHwDesc.dwDeviceZBufferBitDepth);
	gEngfuncs.Con_Printf("HEL DeviceZBufferBitDepth: 0x%X\n", result.ddSwDesc.dwDeviceZBufferBitDepth);

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
	DXCheck(IDirectDrawSurface_GetSurfaceDesc(dxc.pddsBack, &desc));

	D3DVIEWPORT viewport = { 0 };
	viewport.dwSize = sizeof(viewport);
	viewport.dwX = 0;
	viewport.dwY = 0;
	viewport.dwWidth = desc.dwWidth;
	viewport.dwHeight = desc.dwHeight;
	viewport.dvScaleX = 0.5 * viewport.dwWidth;
	viewport.dvScaleY = 0.5f * viewport.dwHeight;
	viewport.dvMaxX = 1;
	viewport.dvMaxY = 1;
	viewport.dvMinZ = 0;
	viewport.dvMaxZ = 1;
	DXCheck(IDirect3DViewport_SetViewport(dxc.viewport, &viewport));

	DXCheck(IDirect3DDevice_CreateMatrix(dxc.pd3dd, &dxc.mtxWorld));
	DXCheck(IDirect3DDevice_CreateMatrix(dxc.pd3dd, &dxc.mtxProjection));

	//set transform state
	{
		d3dEBContext_t ebc = { 0 };
		if (!D3D_StartExecuteBuffer(&ebc))
			return false;
		void *cur = ebc.data;

		D3D_PutInstruction(&cur, D3DOP_STATETRANSFORM, sizeof(D3DSTATE), 2);
		D3D_PutTransformState(&cur, D3DTRANSFORMSTATE_WORLD, dxc.mtxWorld);
		D3D_PutTransformState(&cur, D3DTRANSFORMSTATE_PROJECTION, dxc.mtxProjection);

		D3D_PutInstruction(&cur, D3DOP_STATERENDER, sizeof(D3DSTATE), 2);
		D3D_PutRenderState(&cur, D3DRENDERSTATE_ZENABLE, FALSE);
		D3D_PutRenderState(&cur, D3DRENDERSTATE_ZWRITEENABLE, FALSE);

		D3D_PutInstruction(&cur, D3DOP_EXIT, 0, 0);

		ebc.vertCount = 0;
		ebc.insOffset = 0;
		ebc.insLength = ((char *)cur - (char *)ebc.data);

		if (!D3D_EndExecuteBuffer(&ebc))
			return false;

		DXCheck(IDirect3DDevice_BeginScene(dxc.pd3dd));
		DXCheck(IDirect3DDevice_Execute(dxc.pd3dd, ebc.pd3deb, dxc.viewport, D3DEXECUTE_CLIPPED));
		DXCheck(IDirect3DDevice_EndScene(dxc.pd3dd));

		D3D_ReleaseExecuteBuffer(&ebc);
	}

	return true;
}

qboolean R_Init( void )
{
	memset(&dxc, 0, sizeof(dxc));

	gEngfuncs.Cvar_RegisterVariable( &r_novis );
	gEngfuncs.Cvar_RegisterVariable( &r_nocull );
	gEngfuncs.Cvar_RegisterVariable( &r_lockpvs );
	gEngfuncs.Cvar_RegisterVariable( &r_lockfrustum );

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

	RECT dstRect = { 0 };
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

	// see R_ProcessEntData for tr.entities initialization
	tr.world = (struct world_static_s *)ENGINE_GET_PARM( PARM_GET_WORLD_PTR );
	tr.viewent = (cl_entity_t *)ENGINE_GET_PARM( PARM_GET_VIEWENT_PTR );

	dxc.renderMode = kRenderNormal;
	Vector4Set(dxc.currentColor, 1, 1, 1, 1);

	R_InitImages();

	return true;
}

void R_Shutdown( void )
{
	gEngfuncs.Con_Printf("ref: R_Shutdown()\n");

	Mem_FreePool(&r_temppool);

	gEngfuncs.R_Free_Video();
}

void D3D_Resize( int width, int height )
{
	dxc.windowWidth = width;
	dxc.windowHeight = height;

	if( dxc.pd3deb )
		IDirect3DExecuteBuffer_Release(dxc.pd3deb);
	dxc.pd3deb = NULL;
	if( dxc.pd3dd )
		IDirect3DDevice_Release(dxc.pd3dd);
	dxc.pd3dd = NULL;
	if( dxc.pddsBack )
		IDirectDrawSurface_Release(dxc.pddsBack);
	dxc.pddsBack = NULL;
	if( dxc.pddsZBuffer )
		IDirectDrawSurface_Release(dxc.pddsZBuffer);
	dxc.pddsZBuffer = NULL;
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

void GL_SetupAttributes( int safegl )
{
	;
}

void GL_InitExtensions( void )
{
	tr.framecount = tr.visframecount = 1;
}

void GL_ClearExtensions( void )
{
	;
}

#define DX2_CREATE_EB 0

qboolean D3D_StartExecuteBuffer( d3dEBContext_t *ctx )
{
#if DX2_CREATE_EB
	ctx->pd3deb = NULL;
	D3DEXECUTEBUFFERDESC ebd = { 0 };
	memset(&ebd, 0, sizeof(ebd));
	ebd.dwSize = sizeof(ebd);
	ebd.dwFlags = D3DDEB_BUFSIZE;
	ebd.dwBufferSize = 512;
	DXCheck(IDirect3DDevice_CreateExecuteBuffer(dxc.pd3dd, &ebd, &ctx->pd3deb, NULL));
#else
	ctx->pd3deb = dxc.pd3deb;
#endif

	if (!ctx->pd3deb)
		return false;

	D3DEXECUTEBUFFERDESC ebDesc = { 0 };
	memset(&ebDesc, 0, sizeof(ebDesc));
	ebDesc.dwSize = sizeof(ebDesc);
	ebDesc.dwFlags = D3DDEB_LPDATA;
	HRESULT r = D3D_OK;
	DXCheckRet(r, IDirect3DExecuteBuffer_Lock(ctx->pd3deb, &ebDesc));
	if (r != D3D_OK)
		return false;

	//gEngfuncs.Con_Printf("IDirect3DExecuteBuffer_Lock size %lu data %p\n", ebDesc.dwBufferSize, ebDesc.lpData);

	ctx->bufferSize = ebDesc.dwBufferSize;
	ctx->data = ebDesc.lpData;

	return true;
}

qboolean D3D_EndExecuteBuffer( d3dEBContext_t *ctx )
{
	HRESULT r = D3D_OK;
	DXCheckRet(r, IDirect3DExecuteBuffer_Unlock(ctx->pd3deb));
	if( r != D3D_OK )
		return false;

	D3DEXECUTEDATA exData = { 0 };
	memset(&exData, 0, sizeof(exData));
	exData.dwSize = sizeof(exData);
	exData.dwVertexCount = ctx->vertCount;
	exData.dwVertexOffset = 0;
	exData.dwInstructionOffset = ctx->insOffset;
	exData.dwInstructionLength = ctx->insLength;
	DXCheckRet(r, IDirect3DExecuteBuffer_SetExecuteData(ctx->pd3deb, &exData));

	return r == D3D_OK;
}

void D3D_ReleaseExecuteBuffer( d3dEBContext_t *ctx )
{
#if DX2_CREATE_EB
	if( ctx->pd3deb && ctx->pd3deb != dxc.pd3deb )
		IDirect3DExecuteBuffer_Release( ctx->pd3deb );
#endif
}

void D3D_SetVertTL( D3DTLVERTEX* v, float x, float y, float z, float w, float tu, float tv )
{
	v->sx = x;
	v->sy = y;
	v->sz = z;
	v->rhw = w;
	//v->color = RGBA_MAKE(250, 10, 10, 255);
	v->tu = tu;
	v->tv = tv;
}

void D3D_SetTri( D3DTRIANGLE *t, int v1, int v2, int v3 )
{
	t->v1 = v1;
	t->v2 = v2;
	t->v3 = v3;
	t->wFlags = D3DTRIFLAG_EDGEENABLETRIANGLE;
}

void D3D_PutInstruction( void **dst, BYTE opcode, BYTE size, WORD count )
{
	D3DINSTRUCTION* inst = (D3DINSTRUCTION*)*dst;
	inst->bOpcode = opcode;
	inst->bSize = size;
	inst->wCount = count;
	*dst = (void*)(((D3DINSTRUCTION*)*dst) + 1);
}

void D3D_PutProcessVertices( void** dst, DWORD flags, WORD start, WORD count )
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

void D3D_PutRenderState( void** dst, D3DRENDERSTATETYPE type, DWORD arg )
{
	D3DSTATE* s = (D3DSTATE*)*dst;

	s->drstRenderStateType = type;
	s->dwArg[0] = arg;
	*dst = (void*)(((D3DSTATE*)*dst) + 1);
}

void D3D_PutTransformState( void** dst, D3DTRANSFORMSTATETYPE type, DWORD arg )
{
	D3DSTATE* s = (D3DSTATE*)*dst;

	s->dtstTransformStateType = type;
	s->dwArg[0] = arg;
	*dst = (void*)(((D3DSTATE*)*dst) + 1);
}
