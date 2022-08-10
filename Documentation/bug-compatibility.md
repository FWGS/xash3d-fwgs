# Bug-compatibility in Xash3D FWGS

Xash3D FWGS has special mode for games that rely on original engine bugs.

In this mode, we emulate the behaviour of selected functions that may help running mods relying on engine bugs, but enabling them by default may break majority of other games.

At this time, we only have implemented GoldSrc bug-compatibility. It can be enabled with `-bugcomp` command line switch.

## GoldSrc bug-compatibility

### Emulated bugs

* `pfnPEntityOfEntIndex` in GoldSrc returns NULL for last player due to incorrect player index comparison

### Games and mods that require this

* Counter-Strike: Condition Zero - Deleted Scenes
