# Bug-compatibility in Xash3D FWGS

Xash3D FWGS has special mode for games that rely on original engine bugs. In this mode, we emulate the behaviour of selected functions that may help running mods relying on engine bugs, but enabling them by default may break majority of other games.

At this time, we only have implemented GoldSrc bug-compatibility. It can be enabled with `-bugcomp` command line switch.

When `-bugcomp` is specified without argument, it enables everything. This behavior might be changed or removed in future versions.

When `-bugcomp` is specified with argument, it interpreted as flags separated with `+`. This way it's possible to combine multiple levels of bug-compatibility.

## GoldSrc bug-compatibility

| Flag    | Description | Games that require this flag |
| ------- | ----------- | ---------------------------- |
| `peoei` | Reverts `pfnPEntityOfEntIndex` behavior to GoldSrc, where it returns NULL for last player due to incorrect player index comparison | * Counter-Strike: Condition Zero - Deleted Scenes |
| `gsmrf` | Rewrites message at the moment when Game DLL attempts to write an internal engine message, usually specific to GoldSrc protocol.<br>Right now only supports `svc_spawnstaticsound`, more messages added by request. | * MetaMod/AMXModX based mods |
| `sp_attn_none` | Makes sounds with attenuation zero spatialized, i.e. have a stereo effect. | Possibly, every game that was made for GoldSrc. |
| `get_game_dir_full` | Makes server return full path in server's `pfnGetGameDir` API function | Mods targetting engine before HL 1.1.1.1, according to MetaMod [documentation](http://metamod.org/engine_notes.html#GetGameDir) |
