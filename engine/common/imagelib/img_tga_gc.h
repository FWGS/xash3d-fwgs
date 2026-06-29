/*
img_tga_gc.h - GameCube native Valve TGA decode
Copyright (C) 2026 Xash3D GameCube port contributors
*/
#ifndef IMG_TGA_GC_H
#define IMG_TGA_GC_H

#include "img_tga.h"

qboolean Image_LoadTGA_GameCube( const char *name, const tga_t *header, byte *buf_p, const byte *buf_end, const rgba_t palette[256] );

#endif // IMG_TGA_GC_H
