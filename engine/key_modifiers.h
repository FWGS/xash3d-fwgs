/*
key_modifiers.h - enumeration of possible key modifiers
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
#ifndef KEY_MODIFIERS_H
#define KEY_MODIFIERS_H

typedef enum
{
	KeyModifier_None = 0,
	KeyModifier_LeftShift = (1 << 0),
	KeyModifier_RightShift = (1 << 1),
	KeyModifier_LeftCtrl = (1 << 2),
	KeyModifier_RightCtrl = (1 << 3),
	KeyModifier_LeftAlt = (1 << 4),
	KeyModifier_RightAlt = (1 << 5),
	KeyModifier_LeftSuper = (1 << 6),
	KeyModifier_RightSuper = (1 << 7),
	KeyModifier_NumLock = (1 << 8),
	KeyModifier_CapsLock = (1 << 9)
} key_modifier_t;

#endif
