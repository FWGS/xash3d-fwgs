/*
boneinfo.h - structure that send delta-compressed bones across network
Copyright (C) 2018 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef BONEINFO_H
#define BONEINFO_H

typedef struct
{
	vec3_t	angles;
	vec3_t	origin;
} boneinfo_t;

#endif//BONEINFO_H
