/*
zlib_wrapper.h - select zlib API implementation
Copyright (C) 2026 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#ifndef ZLIB_WRAPPER_H
#define ZLIB_WRAPPER_H

#if XASH_ZLIB_NG
#include <zlib-ng.h>

#define z_stream     zng_stream
#define inflateInit2 zng_inflateInit2
#define inflate      zng_inflate
#define inflateReset zng_inflateReset
#define inflateEnd   zng_inflateEnd
#define deflateInit  zng_deflateInit
#define deflate      zng_deflate
#define deflateBound zng_deflateBound
#define deflateEnd   zng_deflateEnd
#elif XASH_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif

#endif // ZLIB_WRAPPER_H
