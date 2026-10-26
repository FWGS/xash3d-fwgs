/*
engine.h - engine startup for Sailfish OS
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

// set by the launcher, survives the engine restarting itself on game change
#define ENGINE_ENV "XASH3D_SAILFISH_ENGINE"
#define ENGINE_BINARY "/usr/bin/" XASH_PACKAGE_NAME

// relative to $HOME, sailjail exposes Documents with the Documents permission and it's visible over MTP
#define ENGINE_BASEDIR "Documents/xash3d-fwgs"

int Engine_Main( int argc, char **argv );
