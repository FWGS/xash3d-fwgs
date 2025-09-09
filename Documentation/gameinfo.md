# Game definition and information file

gameinfo.txt is an essential part of any Xash3D based game. It allows basic customization for games creators, like setting game title, DLL paths, etc.

This document defines gameinfo.txt syntax, supported keys and liblist.gam conversion rules for the latest version of the engine. Note for engine developers, keep this document in sync with an implementation.

## gameinfo.txt syntax

* gameinfo.txt is a simple list of keys and values, separated by newline.
* You can add single line comments using double slashes (//).
* Keys can accept integer, float, string or boolean values.
* Boolean keys use 0 as false and 1 as true value.
* To have spaces in string values, you must enclose them in double quotes. Then, to have double quotes, you must escape it with backslash, and to have a backslash you need to use double backslashes.

The example:
```
// this is a comment :)
// this is another comment
some_integer_key 123

this_is_float_key 13.37 // optional comment

enable_feature 1 // boolean, 1 to enable, 0 to disable

example_string_key string_value
example_spaces "string with spaces"
example_title "Fate\\Stay Night" // engine will parse it as Fate\Stay Night
```

## gameinfo.txt keys

This is a list of all gameinfo.txt keys supported by the engine. Be aware that the engine will silently skip all unrecognized keys.

| Key              | Type       | Default value   | Description |
| ---------------- | ---------- | --------------- | ----------- |
| `ambient0`       | string     | Empty string    | Automatic ambient sound |
| `ambient1`       | string     | Empty string    | Automatic ambient sound |
| `ambient2`       | string     | Empty string    | Automatic ambient sound |
| `ambient3`       | string     | Empty string    | Automatic ambient sound |
| `basedir`        | string     | `valve`         | Game base directory, used to share assets between games |
| `date`           | string     | Empty string    | Game release date. Unused. |
| `dllpath`        | string     | `cl_dlls`       | Game DLL path. Engine will search custom DLLs (client or menu, for example) in this directory, except `gamedll`, see below. |
| `fallback_dir`   | string     | Empty string    | Additional game base directory |
| `gamedir`        | string     | Current gamedir | Game directory, ignored in FWGS, as game directory is defined by the game directory name |
| `gamedll`        | string     | `dlls/hl.dll`   | Game server DLL for 32-bit x86 Windows (see LibraryNaming.md for details) |
| `gamemode`       | string     | Empty string    | Game type. When set to `singleplayer_only` or `multiplayer_only` marks the game as SP or MP only respectively, hiding the option in game UI. Omitting this key or using custom values mark the game as both MP and SP compatible. |
| `icon`           | string     | `game.ico`      | Game icon. Engine will automatically append .ico and may automatically switch to .tga icon as well |
| `max_beams`      | integer    | 128             | Beams limit, 64 min, 512 max |
| `max_edicts`     | integer    | 900             | Entities limit, 600 min, 8192 max (protocol limit). In FWGS, minimum is 64. |
| `max_particles`  | integer    | 4096            | Particles limit, 1024 min, 131072 max |
| `max_tempents`   | integer    | 500             | Temporary entities limit. 300 min, 2048 max |
| `mp_entity`      | string     | `info_player_deathmatch` | Entity used to mark maps as multiplayer |
| `mp_filter`      | string     | Empty string    | When set, used to filter multiplayer maps instead of `mp_entity`.<br>If the map name starts with the same characters as this filter, it's considered a multiplayer map |
| `nomodels`       | boolean    | 0               | When set to 1, disallows changing player model in UI |
| `noskills`       | boolean    | 0               | When set to 1, disallows selection of game difficulty |
| `secure`         | boolean    | 0               | When set to 1, original Unkle Mike's engine will completely disable developer mode. FWGS ignores but preserves this value for compatibility. |
| `size`           | integer    | 0               | Game directory size in bytes, used in Change Game dialog only |
| `startmap`       | string     | `c0a0`          | The name of the map used in new game |
| `sp_entity`      | string     | `info_player_start` | Entity used to mark map as single player. Used in map validation |
| `title`          | string     | `New Game`      | Game title, used in window title, default server name, etc. |
| `trainmap`       | string     | `t0a0`          | The name of the training map (Hazard Course) |
| `type`           | string     | Empty string    | Game type, used in Change Game UI. |
| `url_info`       | string     | Empty string    | Game homepage URL, used in Change Game UI |
| `url_update`     | string     | Empty string    | Game updates URL, used in Settings UI |
| `version`        | float      | 1.0             | Game version, used in Change Game dialog and in server info |

## FWGS-specific gameinfo.txt keys

These strings are specific to Xash3D FWGS.

| Key                     | Type       | Default value            | Description |
| ----------------------- | ---------- | ------------------------ | ----------- |
| `animated_title`        | boolean    | 0                        | Use animated title in main menu (WON Half-Life logo.avi imitation from Half-Life 25-th anniversary update)
| `autosave_aged_count`   | integer    | 2                        | Auto saves limit used in saves rotation |
| `gamedll_linux`         | string     | Generated from `gamedll` | Game server DLL for 32-bit x86 Linux (see LibraryNaming.md for details) |
| `gamedll_osx`           | string     | Generated from `gamedll` | Game server DLL for 32-bit x86 macOS (see LibraryNaming.md for details) |
| `hd_background`         | boolean    | 0                        | Use HD background for main menu (Half-Life 25-th anniversary update) |
| `internal_vgui_support` | boolean    | 0                        | Only for programmers! Required to be set as 1 for PrimeXT!<br>When set to 1, the engine will not load vgui_support DLL, as VGUI support is done (or intentionally ignored) on the game side. |
| `render_picbutton_text` | boolean    | 0                        | When set to 1, the UI will not use prerendered `btns_main.bmp` and dynamically render them instead |
| `quicksave_aged_count`  | integer    | 2                        | Quick saves limit used in saves rotation |
| `demomap`               | string     | Empty string             | The name of the demo chapter map (Half-Life Uplink) |

## Note on GoldSrc liblist.gam support

As Xash3D accidentally supports GoldSrc games, it also supports parsing liblist.gam.\
Xash3D will use this file if gameinfo.txt is absent, or if its modification timestamp is older than liblist.gam.

> [!NOTE]
> Starting from January 2025, Xash3D FWGS doesn't automatically generate gameinfo.txt from liblist.gam. The key conversion table still remains but if you wish to use gameinfo.txt instead of liblist.gam, you can execute `fs_make_gameinfo` in console.

For game creators who plan supporting only Xash3D, using this file is not recommended.

The table below defines conversion rules from liblist.gam to gameinfo.txt. Some keys' interpretation does differ from `gameinfo.txt`, in this case a note will be left. If `liblist.gam` key isn't present in this table, it's ignored.

| `liblist.gam` key | `gameinfo.txt` key | Note |
| ----------------- | ------------------ | ---- |
| `animated_title`  | `animated_title`   |      |
| `edicts`          | `max_edicts`       |      |
| `fallback_dir`    | `fallback_dir`     |      |
| `game`            | `title`            |      |
| `gamedir`         | `gamedir`          |      |
| `gamedll`         | `gamedll`          |      |
| `gamedll_linux`   | `gamedll_linux`    |      |
| `gamedll_osx`     | `gamedll_osx`      |      |
| `hd_background`   | `hd_background`    |      |
| `icon`            | `icon`             |      |
| `mpentity`        | `mp_entity`        |      |
| `mpfilter`        | `mp_filter`        |      |
| `nomodels`        | `nomodels`         |      |
| `secure`          | `secure`           | In GoldSrc it's used to mark the multiplayer game as anti-cheat enabled.<br>Original Xash3D misinterprets its value for disallowing console and developer mode.<br>FWGS ignores this key but preserves for compatibility. |
| `startmap`        | `startmap`         |      |
| `size`            | `size`             |      |
| `trainingmap`     | `trainmap`         |      |
| `trainmap`        | `trainmap`         |      |
| `type`            | `type` & `gamemode`| In `liblist.gam` this key works as both `type` and `gamemode`.<br>If value is `singleplayer_only` or `multiplayer_only` the game is marked as SP or MP only, and `gameinfo.txt` type set to `Single` or `Multiplayer`.<br>Any custom value will mark the game as both SP and MP compatible, and type is set to whatever custom value. |
| `url_dl`          | `url_update`       |      |
| `url_info`        | `url_info`         |      |
| `version`         | `version`          |      |


