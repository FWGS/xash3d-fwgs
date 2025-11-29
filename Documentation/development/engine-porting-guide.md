## Abstract

Before start, I would recommend you to compile and run engine for already supported and well-spread platform, such as GNU/Linux or Windows. Hack it, get familiar with engine.

One beauties of Xash3D FWGS Engine is it modularity, so game logic, UI and renderers are located in platform-independent libraries. Every library is loaded at run time and may or may be not optional.  For example, if your platform doesn't have OpenGL of any kind, you can skip it and use our software renderer! Of course, there is no real reason to run game engine, if you don't have game logic of any kind, so the minimal build of Xash3D FWGS is headless dedicated server.

Historically, Xash3D Engine was even more modular(see github.com/a1batross/Xash3D_ancient), but we thought that to fulfill our crossplatform needs we will keep all platform-specific stuff in the engine itself and libraries must import or expose platform-independent APIs.

Ok, get to the point!

## Porting guidelines

It will not be a complete tutorial as covering everything in one article is probably just impossible. Instead, I will give you hints on how engine can be ported, how to get your port to upstreamed and how you should maintain your port.

0) Get to know your platform. Maybe I asked for this lately, but you MUST KNOW YOUR PLATFORM, i.e. what's this capable of.

The one of unsupported configurations at this time is when platform can't load dynamic libraries(`*.so` or DLLs). We can't help you as supporting full-static ports are violating the GPL license in various ways.

The other yet unsupported configuration is the big endian.

1) Setup toolchain. For dirty port, you can write Makefile for yourself, engine is written that way, so **it doesn't relies** on any generated data or any special processing. But I recommend you to use Waf anyway, it's better integrated to engine and self-documentable. Also, I will cover only our Waf build options.

2) Open `public/build.h` file. Add appropriate checks for your operating system and/or CPU architecture. It shouldn't be hard.

Also, you'll need to build [Half-Life SDK](https://github.com/FWGS/hlsdk-xash3d/). It has same `public/build.h` so reflect changes into it. Note that to be compatible with HLSDK proprietary license, it's relicensed as public domain under [Unlicense](https://unlicense.org).

We have a library naming scheme that allows us and game creators to distribute binaries for different platforms in one archive. Read `Documentation/extensions/library-naming.md` for more information.

In short, you need to call your platform in unique way. For engine side it's done in `engine/common/build.c` file in engine source code, for HLSDK side it's done in `cmake/LibraryNaming.cmake` and `scripts/waifulib/library-naming.py` files in HLSDK source code.

You can use [predef project wiki](https://sourceforge.net/p/predef/wiki/Home/) for reference.

2) Look at the `engine/platform` directory. We usually try to have all platform-specific stuff inside this folder.

* Functions that must be available on your platform are declared in `platform.h` header. Most of them aren't used in headless dedicated build, but I will cover that later.

* The POSIX-compliant(for *nix operating systems) goes into `posix` subdirectory.

* The SDL-specific code goes into `sdl` subdirectory. Note that even I said before that platform-specific code is in `platform` directory, SDL obviously isn't a __platform__, so you can met checks for XASH_SDL inside whole engine, usually in input.

* Custom SWAP implementation for *nix-based systems is in `swap` subdirectory. It's used when hardware may not have enough memory and you can't add swap memory. I will cover that later
It relies on a fact that systems with paged memory will load pages into RAM and move unused to mmap()-ed by custom swap area.
That allowed to run game engine on music player with MIPS CPU, Linux without SWAP support and

* Other folders as `win32`, `android`, `linux` and so on are self-descriptive.

3) As proof of concept, you can try to test that network features and dynamic library loading are implemented correctly. Build a dedicated server with XASH_DEDICATED or `--dedicated` passed  to `waf configure`.

Start a server and try to connect to it from PC. If everything is fine, then you can move to next step.

If you get out of memory issues, you can try Low Memory Mode, it's enabled with `--low-memory-mode=N` passed to `waf configure` where N is 1 or 2. Low Memory Mode 1 means that we will NOT break a network compatibility. 2 is deeper and doesn't guarantee protocol compatibility and available multiplayer. Of course, with Low Memory Mode 2 you can't test your port dedicated server.

If you got anything compiling, it's nice time to make a commit. Commit your changes into git and push them somewhere(GitHub, GitLab or what you prefer), so you will not lose them accidentally.

4) Here is most interesting, building the client part.

There are three possible situations for you, by increasing difficulty.

1. Do you have SDL2 for your platform? If yes, go ahead and compile engine with client part enabled.

2. Do you have SDL1.2 for your platform? If yes, try to compile it, but SDL1.2 support is very limited and not tested well.

3. You don't have SDL and you can't port it for some reason by yourself or SDL port just don't stable for your platform? Then you need to implement an engine backend.

How to implement engine backend? Well, we have a backends system that was introduced in Old Engine. It's simply two files: backends.h and defaults.h in `common` folder in repository root. You will need to add a macros for your backends in `backends.h` and define a logic in `defaults.h`. We have already have some, check them out, as they may be handy for you.

Once you did it, go back to `platform` directory and open `platform.h` headers. Create new subdirectory in `platform` folder and start implementing missing platform-specific backends one by one. I can't explain this as for obvious reasons it's too specific. You can use macros you defined before in `build.h` to check that you're compiling for your platform and `backends.h` macros to check if platform-specific backend must be enabled.

As you finished, compile engine, fix errors and commit your changes and push to repository.

Remember, if something doesn't work and you can't figure out why, you can join our Discord server at discord.me/fwgs. Ping me (@a1batross) or other engine developer and attach link to your repository, so we can discuss and help you in porting.
