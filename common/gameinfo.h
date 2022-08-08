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

/*
========================================================================

GAMEINFO stuff

internal shared gameinfo structure (readonly for engine parts)
========================================================================
*/
typedef struct
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

#endif//GAMEINFO_H
