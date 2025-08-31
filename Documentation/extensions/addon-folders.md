# Addon folders in Xash3D FWGS

Xash3D FWGS supports both GoldSource-style addon folders and has few own. Each directory can have it's own archives that will be mounted with lower priority than directory itself.

Below is the mounts map, in order of precedence from least important to most important.

|--------------------|------|
| Directory          | Note |
|--------------------|------|
| `$game/downloaded` | Always added. Used to store server downloads.|
| `$game`            | This is the game directory.  |
| `$game/custom`     | Always added. Used for user modifications content. |
| `$game_hd`         | Added with `fs_mount_hd` set to non-zero value. Used for high definition content, similar to GoldSrc.. |
| `$game_addon`      | Added with `fs_mount_addon` set to non-zero value. Used for user modifications content, similar to GoldSrc. |
| `$game_lv`         | Added with `fs_mount_lv` set to non-zero value. Used for low-violence content, similar to GoldSrc. |
| `$game_$language`  | Added with `fs_mount_l10n` set to non-zero value. Language is controlled with `ui_language` cvar or `-language` command line switch. Used for localization content, similar to GoldSrc. |
