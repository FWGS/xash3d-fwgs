/*
gameinfo.h - current game info
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef GAMEINFO_H
#define GAMEINFO_H

#define GFL_NOMODELS	(1<<0)
#define GFL_NOSKILLS	(1<<1)
#define GFL_RENDER_PICBUTTON_TEXT   (1<<2)
#define GFL_HD_BACKGROUND (1<<3)
#define GFL_ANIMATED_TITLE (1<<4)

/*
========================================================================

GAMEINFO stuff

internal shared gameinfo structure (readonly for engine parts)
========================================================================
*/
typedef struct GAMEINFO_s
{
	// filesystem info
	char		gamefolder[64];	// used for change game '-game x'
	char		startmap[64];	// map to start singleplayer game
	char		trainmap[64];	// map to start hazard course (if specified)
	char		title[64];	// Game Main Title
	char		version[14];	// game version (optional)
	short		flags;		// game flags

	// about mod info
	char		game_url[256];	// link to a developer's site
	char		update_url[256];	// link to updates page
	char		type[64];		// single, toolkit, multiplayer etc
	char		date[64];
	char		size[64];		// displayed mod size

	int		gamemode;
} GAMEINFO;

/*
========================================================================

Extended GameInfo struct introduced in Xash3D FWGS

GAMEINFO can't be reliably extended, as nor engine, nor menu can't be
sure about struct size. By adding struct versioning, we can check the
presense for extra fields.
========================================================================
*/

#define GAMEINFO_VERSION 2

typedef enum gametype_e
{
	GAME_NORMAL,
	GAME_SINGLEPLAYER_ONLY,
	GAME_MULTIPLAYER_ONLY,
} gametype_t;

typedef struct gameinfo2_s
{
	int gi_version; // should be set to desired struct version, e.g. GAMEINFO_VERSION

	// filesystem info
	char     gamefolder[64]; // used for change game
	char     startmap[64];   // map to start singleplayer game from
	char     trainmap[64];   // map to start hazardous course from (if specified)
	char     demomap[64];    // map to start demo chapter from (if specified)
	char     title[64];      // game title
	char     iconpath[64];   // path to game icon
	char     version[16];    // game version (optional)
	uint32_t flags;          // gameinfo flags, extended to fit more flags

	// mod info
	char     game_url[256];   // link to a developer's site
	char     update_url[256]; // link to updates page
	char     type[64];        // single, toolkit, multiplayer, etc
	char     date[64];        // release date
	uint64_t size;            // size in bytes

	gametype_t gamemode;
} gameinfo2_t;

#endif//GAMEINFO_H
