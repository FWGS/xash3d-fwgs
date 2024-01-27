/*
kamod.h - kernel access module header
Copyright (C) 2022 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef KAMOD_H
#define KAMOD_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

int kaGeEdramSetSize(int size);
int kaGeEdramGetHwSize(void);

#ifdef __cplusplus
}
#endif

#endif // KAMOD_H
