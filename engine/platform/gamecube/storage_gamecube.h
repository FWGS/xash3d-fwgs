/*
storage_gamecube.h - GameCube read-only disc vs writable SD routing
Copyright (C) 2026 xash3d-gc contributors
*/
#ifndef STORAGE_GAMECUBE_H
#define STORAGE_GAMECUBE_H

#include "common.h"

qboolean GCube_GetDiscPath( char *buf, size_t buflen );
qboolean GCube_GetWritablePath( char *buf, size_t buflen );
qboolean GCube_HasWritableStorage( void );
void GCube_EnsureWritableLayout( void );

#endif /* STORAGE_GAMECUBE_H */
