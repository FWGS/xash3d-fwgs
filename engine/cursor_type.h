/*
cursor_type.h - enumeration of possible mouse cursor types
Copyright (C) 2022 FWGS Team

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
#ifndef CURSOR_TYPE_H
#define CURSOR_TYPE_H

typedef enum
{
	CursorType_User,
	CursorType_None,
	CursorType_Arrow,
	CursorType_Ibeam,
	CursorType_Wait,
	CursorType_Crosshair,
	CursorType_Up,
	CursorType_SizeNwSe,
	CursorType_SizeNeSw,
	CursorType_SizeWe,
	CursorType_SizeNs,
	CursorType_SizeAll,
	CursorType_No,
	CursorType_Hand,
	CursorType_Last
} cursor_type_t;

#endif
