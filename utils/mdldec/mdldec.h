/*
mdldec.h - Half-Life Studio Model Decompiler
Copyright (C) 2020 Andrey Akhmichin

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
#ifndef MDLDEC_H
#define MDLDEC_H

extern char		  destdir[MAX_SYSPATH];
extern char		  modelfile[MAX_SYSPATH];
extern studiohdr_t	 *model_hdr;
extern studiohdr_t	 *texture_hdr;
extern studiohdr_t	**anim_hdr;

#endif // MDLDEC_H

