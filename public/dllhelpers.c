/*
dllhelpers.c - dll exports helpers
Copyright (C) 2024 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "crtlib.h"

void ClearExports( const dllfunc_t *funcs, size_t num_funcs )
{
	size_t i;

	for( i = 0; i < num_funcs; i++ )
		*(funcs[i].func) = NULL;
}

qboolean ValidateExports( const dllfunc_t *funcs, size_t num_funcs )
{
	size_t i;

	for( i = 0; i < num_funcs; i++ )
	{
		if( *(funcs[i].func) == NULL )
			return false;
	}

	return true;
}
