/*
r_draw.c -- dx2 2D drawing
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


void R_Set2DMode( qboolean enable )
{

}

void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{
	;
}

static void D3D_SetVert( D3DTLVERTEX* v, float x, float y, float z, float w, float tu, float tv )
{
	v->sx = x;
	v->sy = y;
	v->sz = z;
	v->rhw = w;
	//v->color = RGBA_MAKE(250, 10, 10, 255);
	v->tu = tu;
	v->tv = tv;
}

static void D3D_SetTri( D3DTRIANGLE *t, int v1, int v2, int v3 )
{
	t->v1 = v1;
	t->v2 = v2;
	t->v3 = v3;
	t->wFlags = D3DTRIFLAG_EDGEENABLETRIANGLE;
}

static void D3D_PutInstruction( void **dst, BYTE opcode, BYTE size, WORD count )
{
	D3DINSTRUCTION* inst = (D3DINSTRUCTION*)*dst;
	inst->bOpcode = opcode;
	inst->bSize = size;
	inst->wCount = count;
	*dst = (void*)(((D3DINSTRUCTION*)*dst) + 1);
}

static void D3D_PutProcessVertices( void** dst, DWORD flags, WORD start, WORD count )
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

static void D3D_PutRenderState( void** dst, D3DRENDERSTATETYPE type, DWORD arg )
{
	D3DSTATE* s = (D3DSTATE*)*dst;

	s->drstRenderStateType = type;
	s->dwArg[0] = arg;
	*dst = (void*)(((D3DSTATE*)*dst) + 1);

}

void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
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

#define DX2_CREATE_EB 0

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

#define VecToD3DCOLOR(v) D3DRGBA(v[0], v[1], v[2], v[3])

		verts[0].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVert(verts, x, y, 0.5, 1, s1, t1);
		verts[1].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVert(verts + 1, x + w, y, 0.5, 1, s2, t1);
		verts[2].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVert(verts + 2, x + w, y + h, 0.5, 1, s2, t2);
		verts[3].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVert(verts + 3, x, y + h, 0.5, 1, s1, t2);

		cur = (void*)(((D3DTLVERTEX*)cur) + vertCount);

		void* insStart = cur;

		D3D_PutProcessVertices(&cur, D3DPROCESSVERTICES_COPY | D3DPROCESSVERTICES_UPDATEEXTENTS, 0, vertCount);
		//align
		if (!(((ULONG)cur) & 7)) {
			D3D_PutInstruction(&cur, D3DOP_TRIANGLE, sizeof(D3DTRIANGLE), 0);
		}

		D3D_PutInstruction(&cur, D3DOP_STATERENDER, sizeof(D3DSTATE), 3);
		D3D_PutRenderState(&cur, D3DRENDERSTATE_TEXTUREHANDLE, tex->d3dHandle);
		//D3D_PutRenderState(&cur, D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_DECAL);

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

void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a )
{
	RECT dstRect = { x, y, x + w, y + h };
	DDBLTFX bltFx = { 0 };
	bltFx.dwSize = sizeof(bltFx);
	bltFx.dwFillColor = RGBA_MAKE(r, g, b, a);
	DXCheck(IDirectDrawSurface_Blt(dxc.pddsBack, &dstRect, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &bltFx));
}


void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	if( frameWidth )
		*frameWidth	= 0;

	if( frameHeight )
		*frameHeight = 0;

	if( numFrames )
		*numFrames = 0;
}

int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	return 0;
}

void AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data )
{
	;
}
