/*
in_ogc.c - Wii-specific input code
Copyright (C) 2026 mintferret

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <tuxedo/types.h>

#if HW_RVL
#include <wiikeyboard/keyboard.h>

// Keyboard stubs (its stack breaks multiplayer for some reason)

s32 __wrap_KEYBOARD_GetEvent(keyboard_event *event)
{
	//Con_Print("USBKeyboard ignored\n");
	return 0;
}
#endif
