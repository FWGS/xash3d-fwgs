/*
lightstyle.h - lighstyle description
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef LIGHTSTYLE_H
#define LIGHTSTYLE_H

typedef struct
{
	char		pattern[256];
	float		map[256];
	int		length;
	float		value;
	qboolean		interp;		// allow to interpolate this lightstyle
	float		time;		// local time is gurantee what new style begins from the start, not mid or end of the sequence
} lightstyle_t;

#endif//LIGHTSTYLE_H