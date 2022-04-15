#ifndef RM_TEXTURE_H
#define RM_TEXTURE_H

#include <ref_api.h>

void RM_Init();

void RM_SetRender( ref_interface_t* ref );

void RM_ReuploadTextures();

int  RM_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int	 RM_LoadTextureArray( const char **names, int flags );
int  RM_LoadTextureFromBuffer( const char *name, rgbdata_t *picture, int flags, qboolean update );
void RM_FreeTexture( unsigned int texnum );

const char*	RM_TextureName( unsigned int texnum );
const byte*	RM_TextureData( unsigned int texnum );

int  RM_FindTexture( const char *name );
void RM_GetTextureParams( int *w, int *h, int texnum );

int	RM_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int RM_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );

#endif  // RM_TEXTURE_H