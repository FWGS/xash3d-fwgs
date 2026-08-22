/*
key_modifiers.h - enumeration of possible key modifiers
Copyright (C) 2022 FWGS Team

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
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
