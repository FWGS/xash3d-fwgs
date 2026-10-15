/*
crash_ps4.h - PlayStation 4 crash handler and watchdog
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef CRASH_PS4_H
#define CRASH_PS4_H

#include "xash3d_types.h"
#include <stdint.h>

// time of the last message in the log, the watchdog looks at it
extern volatile uint64_t ps4_last_log_time;

// -ps4watchdog was passed
extern qboolean ps4_watchdog_enabled;

void PS4_InstallCrashHandler( void );
void PS4_StartWatchdog( void );

#endif // CRASH_PS4_H
