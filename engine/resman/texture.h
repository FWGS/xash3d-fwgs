#ifndef RM_TEXTURE_H
#define RM_TEXTURE_H

void RM_Init();

int  RM_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
void RM_FreeTexture( unsigned int texnum );

int  RM_FindTexture( const char *name );
void RM_GetTextureParams( int *w, int *h, int texnum );

#endif  // RM_TEXTURE_H