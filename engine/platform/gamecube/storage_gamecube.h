/*
storage_gamecube.h - GameCube read-only disc vs writable FAT routing

Writable volumes (Swiss / libdvm): sd: (SD2SP2), carda:/cardb: (SD Gecko).
Disc content: gcdisc: ISO9660. Probe-only: gcprobe: RAM bank.
*/
#ifndef STORAGE_GAMECUBE_H
#define STORAGE_GAMECUBE_H

#include "common.h"

qboolean GCube_GetDiscPath( char *buf, size_t buflen );
qboolean GCube_GetWritablePath( char *buf, size_t buflen );
qboolean GCube_HasWritableStorage( void );
qboolean GCube_HasPersistentWritableStorage( void );
void GCube_EnsureWritableLayout( void );

#endif /* STORAGE_GAMECUBE_H */
