#pragma once
#include "build.h"
#ifdef XASH_EMSCRIPTEN
#ifndef EM_LIB_H
#define EM_LIB_H

#define Platform_POSIX_LoadLibrary( x ) EMSCRIPTEN_LoadLibrary(( x ))
#ifndef EMSCRIPTEN_LIB_FS
#define Platform_POSIX_FreeLibrary( x ) // nothing
#endif // EMSCRIPTEN_LIB_FS

void *EMSCRIPTEN_LoadLibrary( const char *dllname );

#endif // EM_LIB_H
#endif // __EMSCRIPTEN__
