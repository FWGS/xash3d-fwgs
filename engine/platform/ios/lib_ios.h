/*
ios_lib.h - dynamic library code for iOS
Copyright (C) 2017-2018 mittorn

This program is free software: you can redistribute it and/sor modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#ifdef TARGET_OS_IPHONE
#ifndef IOS_LIB_H
#define IOS_LIB_H

void *IOS_LoadLibrary( const char *dllname );

#endif // IOS_LIB_H
#endif // TARGET_OS_IPHONE
