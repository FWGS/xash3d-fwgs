#ifndef RM_TEXTURE_H
#define RM_TEXTURE_H

int  RM_FindTexture( const char *name );
int  RM_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
void RM_FreeTexture( unsigned int texnum );

#endif  // RM_TEXTURE_H