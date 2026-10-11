# Compatibility policy

Xash3D FWGS runs games made for three engines, and sometimes they need conflicting behaviour from it. This page sets which games we keep working first, and what we promise about the interfaces between the engine and game libraries.

## Which games come first

1. Games made for Xash3D FWGS, like PrimeXT, or migrated to it, like Diffusion.
2. Games made for the original Xash3D by Uncle Mike, like XashXT and Paranoia 2: Savior.
3. Games and mods made for GoldSrc.

When a change made for a lower group breaks a game from a higher group, the higher group wins. If a GoldSrc mod still needs the original GoldSrc behaviour, it goes behind a [bug-compatibility](bug-compatibility.md) flag, disabled by default.

For example, GoldSrc keeps BSP texture names in their original case, while Xash3D converts them to lowercase. PrimeXT, Diffusion and other XashXT-derived games rely on lowercase names, so that stays the default, and Snow War mod, which needs the original case, has to be started with `-bugcomp texname_case`.

## Interfaces

Xash3D extends GoldSrc: it implements Valve's interfaces, and both the original Xash3D and Xash3D FWGS added interfaces of their own on top of them. So there are several levels of binary compatibility, each with its own promise, described below.

Binary compatibility means that an already compiled game library works with a newer engine as is, without being rebuilt. For that, everything the compiled code relies on must stay the same: the size of every structure and the offset of every field in it, the order of functions in function tables, and the arguments and return value of every function and how they're passed. Only these matter to compiled code, not how the headers describing them are written.

### Valve interfaces

On the targets Valve supports, Windows, macOS and Linux on 32-bit x86 (i386), we never break binary compatibility with Valve's interfaces: the interfaces between the engine and the client and server libraries, VGUI and the `VFileSystem009` filesystem interface. Game libraries compiled with Valve's SDKs keep working.

Other targets have no Valve-built binaries to stay compatible with, so some structures differ there, see [expanded common structures](extensions/expanded-common-structures.md).

For example, `pfnRandomLong` uses `int` instead of Valve's original `long`: both are 32-bit on i386, so compiled games see no difference, and on 64-bit Linux, where `long` is 64-bit, `int` keeps the function working with 32-bit values.

### Original Xash3D interfaces

We keep compatibility with interfaces introduced by Uncle Mike in the original Xash3D, like the render interface (`render_api.h`), the physics interface (`physint.h`) and the menu interface (`menu_int.h`).

Fewer games use them than Valve's interfaces, so we allow extending them: new fields may be added at the end of a structure, assuming nobody stores it in arrays. Existing fields are never removed, reordered or changed. Games built with older headers don't fill the new fields, so the engine must treat them as missing when they are zero.

### Xash3D FWGS interfaces

Xash3D FWGS adds interfaces of its own: the extensions to the menu interface in `menu_int.h` (`GetExtAPI`, `GiveTextAPI` and `AddTouchButtonToList`), the mobility interface (`mobility_int.h`), SoundAPI (`common/sound_api.h`) and the VGUI support interface (`vgui_api.h`). Some of them have design flaws, but we keep them compatible anyway, unless it's proven that nothing uses them.

### Internal interfaces

Interfaces between the engine and its own libraries are not stable and we don't guarantee backwards compatibility for them. These are the C filesystem interface (`fs_api_t` in `filesystem/filesystem.h`, not to be confused with Valve's `VFileSystem009`) and the renderer interface (`engine/ref_api.h`). They can change in any commit, so these libraries must come from the same engine build.
