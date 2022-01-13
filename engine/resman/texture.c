#include <common/system.h>

#include "texture.h"

int RM_FindTexture( const char *name )
{
	Sys_Error( "Unimplemented RM_FindTexture. Name %s", name );
}

int RM_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	Sys_Error( "Unimplemented RM_LoadTexture. Name %s, size %d", name, size );
}

void RM_FreeTexture( unsigned int texnum )
{
	Sys_Error( "Unimplemented RM_FreeTexture. TexNum %d", texnum );
}