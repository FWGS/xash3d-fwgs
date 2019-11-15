# Server porting
Original valve's server code was compatible with linux and gcc 2.x.
Newer gcc versions have restriction which breaks build.
Now, to make it building with gcc 4.x, you need do following:
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

# Client porting

## VGUI library

Valve's client uses vgui library which is available only on x86 systems and has big amount of mistakes in headers. The best and simplest way of porting is removing VGUI dependency at all.
Most singleplayer mods don't really use VGUI at all. It is used in multiplayer only to show score table and MOTD window

## Porting 

### First strategy: full port

#### Basic port (Stage 1)
* First, remove all files and headers which use vgui (contains "vgui" in file name).
* After that, try to build it and remove vgui includes.
* Remove all gViewPort usings.
* Remove all CVoiceManager usings (xash3d does not support voice)
* Redefine all DLLEXPORT defines as empty field (Place it under _WIN32 macro if you want to keep windows compatibility).
* Remove hud_servers.cpp and Servers_Init/Servers_Shutdown from hud.cpp
* Fix CAPS filenames in includes (like STDIO.H, replace by stdio.h).
* Replace broken macros as DECLARE_MESSAGE, DECLARE_COMMAND by fixed examples from our hlsdk-xash3d port (cl_util.h)
* Add ctype.h where it is need (tolower, isspace functions)
* Add string.h where it is need (memcpy, strcpy, etc)
* Use in_defs.h from hlsdk_client
* Add scoreboard_stub.cpp and input_stub.cpp from hlsdk-xash3d to fix linking.
Now your client should be able to build and work correctly. Add input_xash3d.cpp from hlsdk-xash3d project to fix input.

#### Multiplayer fix (Stage 2)
Look at hlsdk-xash3d project.

Main changes are:
* Add MOTD.cpp, scoreboard.cpp and input_xash3d.cpp
* Add missing functions to hud_redraw.cpp, hud.cpp and tri.cpp, fix class defination in hud.h
* Remove duplicate functions from hud.cpp and HOOK_MESSAGE's for it
* Remove +showscores/-showscores hooks from input.h
* Fix cl_util.h

### Second way: move mod to hlsdk-xash3d
Look at changes you made in client.

If there are not much changes (for example, only some weapons was add), add these changes in hlsdk-xash3d.

You may use diff with original HLSDK you used and apply it as patch to hlsdk-xash3d.

Files must have same line endings (use dos2unix on all files).

I recommend to enable ignoring space changes in diff.
### Writing Makefiles

Use Makefile from hlsdk-xash3d as Makefile example.

Get .c and .cpp file lists from Visual Studio project and fill SRCS and SRCS_C variables to Makefile.

Remove all files containing vgui in name from list, add missing include dirs.

Do same for Android.mk if you are building for android.
