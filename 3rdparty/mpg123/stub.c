/*
stub.c - replacement for libmpg123 internal I/O
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

#include "mpg123lib_intern.h"
#include "lfs_wrap.h"

// mpg123 bug: PORTABLE_API don't include lfs_wrap.c, yet mpg123_open64() still calls this
// just stub it, engine does IO anyway
int INT123_wrap_open( mpg123_handle *mh, void *handle, const char *path, int fd, long timeout, int quiet )
{
	return MPG123_ERR;
}
