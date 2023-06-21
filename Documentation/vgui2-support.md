# Experimental VGUI2 Support

Thanks to @kungfulon contributions, Xash3D FWGS now supports VGUI2.

VGUI2 is undocumented UI system from Source Engine that GoldSrc used in some games and it's main menu.

## Support status

Doesn't draw anything, doesn't take any input.

## List of games that require VGUI2 support

- Counter-Strike 1.6
- Counter-Strike: Condition Zero
- Counter-Strike: Condition Zero Deleted Scenes
- Day of Defeat
- Half-Life: Blue Shift

## How to enable VGUI2 support

By default, engine doesn't initialize VGUI2 to not confuse existing games and mods that use or do not use VGUI1. This support also relies on a number of proprietary libraries, that we better not redistribute, so you have to supply them yourself. Keep in mind, that these libraries are compiled only for 32-bit Windows, Linux and OSX, you shouldn't expect them running anywhere else.

1. Download latest Half-Life from Steam.
2. Open local files.
3. Copy libraries in the list below according to your platform, to folder where Xash3D searches it's libraries (usually in same folder where executable is located)

| Linux    | Windows
| -------- | --------
| `chromehtml.so` | `chromehtml.dll`
| `vgui2.so` | `vgui2.dll`
| `libcef.so` | `libcef.dll`
| `libtier0.so` | `tier0.dll`
| `libvstdlib.so` | `vstdlib.dll`
| `libsteam_api.so` | `steam_api.dll`

Some files must also be copied:
- `cef_gtk.pak`

Additionally, on Linux other libraries must be provided. Use `ldd` tool to figure out which libraries you miss. Many of them can be pulled from Steam Runtime.

Do not recommend using RoDir with VGUI2 games. Many of them are broken. Steam API will crash without `steam_appid.txt` in Xash root directory.
