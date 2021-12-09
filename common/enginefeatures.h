/*
features.h - engine features that can be enabled by mod-maker request
Copyright (C) 2012 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef FEATURES_H
#define FEATURES_H

// list of engine features that can be enabled through callback SV_CheckFeatures
#define ENGINE_WRITE_LARGE_COORD	(1<<0)	// replace standard message WRITE_COORD with big message for support more than 8192 units in world
#define ENGINE_QUAKE_COMPATIBLE	(1<<1)	// make engine compatible with quake (flags and effects)
#define ENGINE_LOAD_DELUXEDATA	(1<<2)	// loading deluxemap for map (if present)
#define ENGINE_PHYSICS_PUSHER_EXT	(1<<3)	// enable sets of improvements for MOVETYPE_PUSH physics
#define ENGINE_LARGE_LIGHTMAPS	(1<<4)	// change lightmap sizes from 128x128 to 1024x1024
#define ENGINE_COMPENSATE_QUAKE_BUG	(1<<5)	// compensate stupid quake bug (inverse pitch) for mods where this bug is fixed
#define ENGINE_IMPROVED_LINETRACE	(1<<6)	// new traceline that tracing through alphatextures
#define ENGINE_COMPUTE_STUDIO_LERP	(1<<7)	// enable MOVETYPE_STEP lerping back in engine
#define ENGINE_LINEAR_GAMMA_SPACE	(1<<8)	// disable influence of gamma/brightness cvars to textures/lightmaps, for mods with custom renderer

#endif//FEATURES_H
