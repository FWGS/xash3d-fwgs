#pragma once
#ifndef VID_COMMON
#define VID_COMMON

#define FCONTEXT_CORE_PROFILE		BIT( 0 )
#define FCONTEXT_DEBUG_ARB		BIT( 1 )

#define GL_CheckForErrors() GL_CheckForErrors_( __FILE__, __LINE__ )
void GL_CheckForErrors_( const char *filename, const int fileline );
const char *GL_ErrorString( int err );
void GL_UpdateSwapInterval( void );
qboolean GL_Support( int r_ext );
int GL_MaxTextureUnits( void );
void GL_CheckExtension( const char *name, const dllfunc_t *funcs, const char *cvarname, int r_ext );
void GL_SetExtension( int r_ext, int enable );

#endif // VID_COMMON
