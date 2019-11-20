I propose a new library naming scheme, which will allow to distribute mods and games in single archive to different operating systems and CPUs:

Legend:
* $os -- Q_buildos() return value, in lower case.
* $arch -- Q_buildarch() return value, in lower case.
* $ext -- OS-specific extension: dll, so, dylib, etc.

The scheme will be:

1. Client library:
* ```client.$ext``` for **Win/Lin/Mac** with **x86**.
* ```client_$arch.$ext``` for **Win/Lin/Mac** with **NON-x86**.
* ```client_$os_$arch.$ext``` for everything else.

2. Menu library:
* ```menu.$ext``` for **Win/Lin/Mac** with **x86**.
* ```menu_$arch.$ext``` for **Win/Lin/Mac** with **NON-x86**.
* ```menu_$os_$arch.$ext``` for everything else.

3. Server library:
* On  **Win/Lin/Mac** with **x86**, it **MUST** use the raw gamedll name for corresponding OS field from `gameinfo.txt`.
* On **Win/Lin/Mac** with **NON-x86**, it **MUST** use the raw gamedll name for corresponding OS field from `gameinfo.txt`, but append ```_$arch``` before file extension. Like: ```hl_amd64.so``` or ```cs_e2k.so```.
* On everything else, it must use gamedll name from ```gamedll_linux``` field, but append ```_$os_$arch``` before file extension. Like: ```hl_haiku_amd64.so``` or ```cs_freebsd_armhf.so```.
Why ```gamedll_linux``` and not ```gamedll```? Because it looks more logic that way, most operating systems are *nix-like and share code with Linux, rather than Windows.

4. Refresh library: not needed, as RefAPI is not stable and it's not intended to distribute with mods.

For any libraries distributed **with** engine, naming scheme should be used more convenient for OS port.

Issue #0. Inconsistency between ABI and Q_buildarch.\
Resolution: Change Q_buildarch return value to use Debian-styled architectures list: https://www.debian.org/ports/, which includes a special naming for big/little-endian and hard/soft-float ARM.

Issue #1: Build-system integration.\
Resolution: implemented as [LibraryNaming.cmake](https://github.com/FWGS/hlsdk-xash3d/blob/master/cmake/LibraryNaming.cmake) and [library_naming.py](https://github.com/FWGS/hlsdk-xash3d/blob/master/scripts/waifulib/library_naming.py) extensions, see 

Issue #2(related to #0): Which ARM flavours we actually need to handle?\
Resolution: Little-endian only, as there is no known big-endian ARM platforms in the wild.
Architecture is coded this way:
* ```armvxy```, where `x` is ARM instruction set level and `y` is hard-float ABI presence: `hf` where hard float ABI used, otherwise `l`.

See discussion: https://github.com/FWGS/xash3d-fwgs/issues/39
