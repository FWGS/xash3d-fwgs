# Bug-compatibility in Xash3D FWGS

Xash3D FWGS has special mode for games that rely on original engine bugs. In this mode, we emulate the behaviour of selected functions that may help running mods relying on engine bugs, but enabling them by default may break majority of other games.

At this time, we only have implemented GoldSrc bug-compatibility. It can be enabled with `-bugcomp` command line switch.

`-bugcomp` requires an argument, which is interpreted as flags separated with `+`. This way it's possible to combine multiple levels of bug-compatibility.

When `-bugcomp` is specified without argument (or with `help`), the engine prints the list of flags it knows about and exits. Treat that output as the authoritative one: this page is written by hand and may describe an older or a newer engine than yours.

Older versions used to enable every flag at once when `-bugcomp` was given without argument. That is no longer supported: each flag reintroduces a bug that only one family of mods wants, so turning all of them on breaks the game you were trying to fix.

The set of flags is not a stable interface. A flag may be renamed, split in two, folded into another one, or dropped entirely - for example, once we figure out how to make the original behaviour safe for every other game and enable it unconditionally. Don't hardcode flag names into scripts or launchers without a way to fix them up later.

## GoldSrc bug-compatibility

| Flag    | Description | Games that require this flag |
| ------- | ----------- | ---------------------------- |
| `peoei` | Reverts `pfnPEntityOfEntIndex` behavior to GoldSrc, where it returns NULL for last player due to incorrect player index comparison | * Counter-Strike: Condition Zero - Deleted Scenes |
| `gsmrf` | Rewrites message at the moment when Game DLL attempts to write an internal engine message, usually specific to GoldSrc protocol.<br>Right now only supports `svc_spawnstaticsound`, more messages added by request. | * MetaMod/AMXModX based mods |
| `sp_attn_none` | Makes sounds with attenuation zero spatialized, i.e. have a stereo effect. | Possibly, every game that was made for GoldSrc. |
| `get_game_dir_full` | Makes server return full path in server's `pfnGetGameDir` API function | Mods targetting engine before HL 1.1.1.1, according to MetaMod [documentation](http://metamod.org/engine_notes.html#GetGameDir) |
| `sf_notdm` | Inhibits entities with "Not in Deathmatch" spawnflag (2048) when `deathmatch` is set, like GoldSrc does. This flag is a Quake leftover and was never implemented in Xash3D, as maps made for it might not expect it | Unknown, possibly maps that have both singleplayer and deathmatch layouts |
| `always_textinput` | Keeps platform text input (and therefore text input events) enabled while in game, like GoldSrc does. Xash3D only enables it when the engine itself expects text, so mods that fetch text events straight from SDL never see any | * Natural Selection, its chat box reads `SDL_TEXTINPUT` events on its own |
