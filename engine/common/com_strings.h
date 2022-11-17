/*
com_strings.h - all paths to external resources that hardcoded into engine
Copyright (C) 2018 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef COM_STRINGS_H
#define COM_STRINGS_H

// default colored message headers
#define S_NOTE			"^2Note:^7 "
#define S_WARN			"^3Warning:^7 "
#define S_ERROR			"^1Error:^7 "
#define S_USAGE			"Usage: "
#define S_USAGE_INDENT  "       "

#define S_OPENGL_NOTE		"^2OpenGL Note:^7 "
#define S_OPENGL_WARN		"^3OpenGL Warning:^7 "
#define S_OPENGL_ERROR		"^3OpenGL Error:^7 "

// end game final default message
#define DEFAULT_ENDGAME_MESSAGE	"The End"

// path to the hash-pak that contain custom player decals
#define CUSTOM_RES_PATH		"custom.hpk"

// path to default playermodel in GoldSrc
#define DEFAULT_PLAYER_PATH_HALFLIFE	"models/player.mdl"

// path to default playermodel in Quake
#define DEFAULT_PLAYER_PATH_QUAKE	"progs/player.mdl"

// debug beams
#define DEFAULT_LASERBEAM_PATH	"sprites/laserbeam.spr"

#define DEFAULT_INTERNAL_PALETTE	"gfx/palette.lmp"

#define DEFAULT_EXTERNAL_PALETTE	"gfx/palette.pal"

// path to folders where placed all sounds
#define DEFAULT_SOUNDPATH		"sound/"

// path store saved games
#define DEFAULT_SAVE_DIRECTORY	"save/"

// fallback to this skybox
#define DEFAULT_SKYBOX_PATH		"gfx/env/desert"

// playlist for startup videos
#define DEFAULT_VIDEOLIST_PATH	"media/StartupVids.txt"

#define CVAR_GLCONFIG_DESCRIPTION	"enable or disable %s"

#define DEFAULT_BSP_BUILD_ERROR	"%s can't be loaded in this build. Please rebuild engine with enabled SUPPORT_BSP2_FORMAT\n"

#define DEFAULT_UPDATE_PAGE "https://github.com/FWGS/xash3d-fwgs/releases/latest"

#define XASH_ENGINE_NAME "Xash3D FWGS"
#define XASH_VERSION        "0.20" // engine current version
#define XASH_COMPAT_VERSION "0.99" // version we are based on

// renderers order is important, software is always a last chance fallback
#define DEFAULT_RENDERERS { "gl", "gles1", "gles2", "gl4es", "soft" }
#define DEFAULT_RENDERERS_LEN 5

#endif//COM_STRINGS_H
