# Self-made port
## Compatibility with RISC architectures
### Unaligned access
Unaligned access on **i386** causes only performance penalty, but on **RISC** it can cause unstable work.

For HLSDK at least you need such patches in util.cpp:
 - https://github.com/FWGS/halflife/commit/7bfefe86e35d67867ae7af830ac1fc38f2908360
 - https://github.com/FWGS/hlsdk-portable/commit/617d75545f2ecb9b2d46cc30728dc37c9eb6d35e

### Signed chars
`char` type defined as **signed** for **x86** and as **unsigned** for **arm**.

And sometimes such difference can break logic.

As a solution you can use `signed char` type in code directly or use `-fsigned-char` option for gcc/clang compilers.

For HLSDK at least you need such [fix](https://github.com/FWGS/hlsdk-portable/commit/1ca34fcb4381682bd517612b530db22a1354a795) in nodes.cpp/.h.

## Compatibility with 64bit architectures
You need list of patches for Studio Model Render, MAKE_STRING macro and nodes:
 - https://github.com/FWGS/hlsdk-portable/commit/d287ed446332e615ab5fb25ca81b99fa14d18a73
 - https://github.com/FWGS/hlsdk-portable/commit/3bce17e3a04f8af10a927a07ceb8ab0f09152ec4
 - https://github.com/FWGS/hlsdk-portable/commit/9ebfc981773ec4c7a89ffe52d9c249e1fbef9634
 - https://github.com/FWGS/hlsdk-portable/commit/00833188dab87ef5746286479ba5aeb9d83b4a0c
 - https://github.com/FWGS/hlsdk-portable/commit/4661b5c1a5245b27a5532745c11e44b5540e4172
 - https://github.com/FWGS/hlsdk-portable/commit/2b61380146b1d58a8c465f0e312c061b12bda115
 - https://github.com/FWGS/hlsdk-portable/commit/8ef6cb2427ee16a763103bd3f315f38e2f01cfe2

## Mobility API
Xash3D FWGS has special extended interface in `mobility_int.h` which adds some new features like vibration on mobile platforms.

## Porting server-side code
Original valve's server code was compatible with linux and gcc 2.x.

Newer gcc versions have restriction which breaks build.

Now, to make it building with gcc 4.x+ or clang, you need to do following:
* Go to cbase.h and redefine macros as following
```
#define SetThink( a ) m_pfnThink = static_cast <void (CBaseEntity::*)(void)> (&a)
#define SetTouch( a ) m_pfnTouch = static_cast <void (CBaseEntity::*)(CBaseEntity *)> (&a)
#define SetUse( a ) m_pfnUse = static_cast <void (CBaseEntity::*)( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )> (&a)
#define SetBlocked( a ) m_pfnBlocked = static_cast <void (CBaseEntity::*)(CBaseEntity *)> (&a)
#define ResetThink( ) m_pfnThink = static_cast <void (CBaseEntity::*)(void)> (NULL)
#define ResetTouch( ) m_pfnTouch = static_cast <void (CBaseEntity::*)(CBaseEntity *)> (NULL)
#define ResetUse( ) m_pfnUse = static_cast <void (CBaseEntity::*)( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )> (NULL)
#define ResetBlocked( ) m_pfnBlocked = static_cast <void (CBaseEntity::*)(CBaseEntity *)> (NULL)
...
#define SetMoveDone( a ) m_pfnCallWhenMoveDone = static_cast <void (CBaseToggle::*)(void)> (&a)
```
* Replace all SetThink(NULL), SetTouch(NULL), setUse(NULL) and SetBlocked(NULL) by ResetThink(), ResetTouch(), ResetUse() and ResetBlocked()
* Sometimes you may need to add #include <ctype.h> if functions tolower or isspace are missing

## Porting client-side code

* Redefine all DLLEXPORT defines as empty field (Place it under _WIN32 macro if you want to keep windows compatibility).
* Remove hud_servers.cpp and Servers_Init/Servers_Shutdown from hud.cpp.
* Fix CAPS filenames in includes (like STDIO.H, replace by stdio.h).
* Replace broken macros as DECLARE_MESSAGE, DECLARE_COMMAND by fixed examples from our hlsdk-portable port (cl_util.h).
* Add ctype.h where is needed (tolower, isspace functions).
* Add string.h where is needed (memcpy, strcpy, etc).
* Use in_defs.h from hlsdk-portable.
* Add input_xash3d.cpp from hlsdk-portable project to fix input.

Now your client should be able to build and work correctly.

# Porting mod to hlsdk-portable
Look at changes which was made.

If there are not too much changes (for example, only some weapons was added), add these changes in hlsdk-portable.

You can use diff with original HLSDK and apply it as patch to hlsdk-portable.

```
NOTE: Many an old mods was made on HLSDK 2.0-2.1 and some rare mods on HLSDK 1.0.
So you need to have different versions of HLSDK to make diffs.
Plus different Spirit of Half-Life versions if mod was made on it.
Also, weapons in old HLSDK versions does not use client weapon prediction system and you may be need to port standart half-life weapons to server side.
```
Files must have same line endings (use dos2unix on all files).

We recommend to enable ignoring space changes in diff.

Move all new files to separate directories.

# Possible replacements for non-portable external dependencies
1. If mod uses **fmod.dll** or **base.dll** to play mp3/ogg on client-side or to precache sounds on server-side, you can replace it with:
	- [pfnPrimeMusicStream](https://github.com/FWGS/hlsdk-portable/blob/master/engine/cdll_int.h#L293=) engine callback;
	- [miniaudio](https://github.com/mackron/miniaudio);
	- [phonon](https://community.kde.org/Phonon).

2. If mod uses **OpenGL**, porting code to Xash3D render interface is recommended.

3. If mod uses **cg.dll**, you can try to port code to [NVFX](https://github.com/tlorach/nvFX).

4. If mod uses detours, comment code or try to find replacement somehow by yourself.

# Additional recommendations
1. If mod uses STL, you can replace it with [MiniUTL](https://github.com/FWGS/MiniUTL).
2. Avoid to use dynamic casts to make small size binaries.
3. Avoid to use exceptions to make small size binaries.
 
# Writing build scripts

Use wscript/CMakeLists.txt files from hlsdk-portable as build scripts example.

Get .c and .cpp file lists from Visual Studio project.

Add missing include dirs.

