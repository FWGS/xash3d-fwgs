/*
gu_extension.h - PSP Graphic Unit extensions header
Copyright (C) 2020 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef GU_EXTENSION_H
#define GU_EXTENSION_H

void extGuStart(int cid, void* list, int size);
void *extGuBeginPacket( unsigned int *maxsize );
void extGuEndPacket( void *eaddr );

#endif//GU_EXTENSION_H
