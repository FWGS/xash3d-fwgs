/*

in_wii.c - wii input
Copyright (C) 2026 mintferret

some code are borrowed from Xash3D PSP Port
Copyright (C) 2021 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if !XASH_DEDICATED
#if XASH_INPUT == INPUT_WII


#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "sound.h"
#include "vid_common.h"

#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>

#endif // XASH_INPUT == INPUT_WII
#endif //!XASH_DEDICATED
