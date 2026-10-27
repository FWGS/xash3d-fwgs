/*
messagebox.h - engine message boxes shown by the launcher
Copyright (C) 2026 Alibek Omarov

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

// set by the engine, which starts us with the title and the text as arguments and waits until we exit
#define MESSAGEBOX_ENV "XASH3D_SAILFISH_MESSAGEBOX"

int MessageBox_Main( int argc, char **argv );
