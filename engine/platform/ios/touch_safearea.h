/*
touch_safearea.h - window safe-area insets
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

#pragma once

// Normalized insets; returns the window width in UIKit points (0 if unavailable).
float IOS_GetTouchInsets( void *window, float *left, float *top, float *right, float *bottom );
