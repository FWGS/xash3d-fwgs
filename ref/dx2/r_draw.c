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

	if (!dxc.pd3dd)
		return;

	d3dEBContext_t ebc = { 0 };
	{
		if( !D3D_StartExecuteBuffer( &ebc ))
			return;

		void *cur = ebc.data;

		const int vertCount = 4;
		D3DTLVERTEX* verts = (D3DTLVERTEX*)cur;

#define VecToD3DCOLOR(v) D3DRGBA(v[0], v[1], v[2], v[3])

		verts[0].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVertTL(verts, x, y, 0.5, 1, s1, t1);
		verts[1].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVertTL(verts + 1, x + w, y, 0.5, 1, s2, t1);
		verts[2].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVertTL(verts + 2, x + w, y + h, 0.5, 1, s2, t2);
		verts[3].color = VecToD3DCOLOR(dxc.currentColor);
		D3D_SetVertTL(verts + 3, x, y + h, 0.5, 1, s1, t2);

		cur = (void*)(((D3DTLVERTEX*)cur) + vertCount);

		void* insStart = cur;

		D3D_PutProcessVertices(&cur, D3DPROCESSVERTICES_COPY | D3DPROCESSVERTICES_UPDATEEXTENTS, 0, vertCount);
		// align
		if (!(((ULONG)cur) & 7)) {
			D3D_PutInstruction(&cur, D3DOP_TRIANGLE, sizeof(D3DTRIANGLE), 0);
		}

		D3D_PutInstruction(&cur, D3DOP_STATERENDER, sizeof(D3DSTATE), 3);
		// TODO disable when == tr.whiteTexture
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
		else// if (dxc.renderMode == kRenderNormal)
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

		ebc.vertCount = 4;
		ebc.insOffset = ((char*)insStart - (char*)ebc.data);
		ebc.insLength = ((char*)cur - (char*)insStart);

		if( !D3D_EndExecuteBuffer( &ebc ))
			return;
	}

	DXCheck(IDirect3DDevice_BeginScene(dxc.pd3dd));
	DXCheck(IDirect3DDevice_Execute(dxc.pd3dd, ebc.pd3deb, dxc.viewport, D3DEXECUTE_CLIPPED));
	DXCheck(IDirect3DDevice_EndScene(dxc.pd3dd));

	D3D_ReleaseExecuteBuffer( &ebc );
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
