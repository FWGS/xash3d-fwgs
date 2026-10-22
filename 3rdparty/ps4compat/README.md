# ps4compat

Runtime fixes for applications built with the OpenOrbis PS4 toolchain. Used by
Xash3D FWGS and hlsdk-portable, licensed under MIT so any project can take it.

Every PS4 image (`eboot.bin` and each `.prx`) must link these objects. The
linker redirects libc functions to them with `-Wl,--wrap=<function>`.

| File | Purpose |
|------|---------|
| `ps4_cwd.c` | working directory emulation (`chdir`, relative paths), FreeBSD 9 `stat` conversion, `fcntl` on sockets |
| `ps4_malloc.c` | one process-wide heap owned by `eboot.bin`, modules forward to it |
| `ps4_stdio.c` | thread safe `printf` family |
| `ps4_crtlib.c` | module startup code that runs global constructors |
| `ps4_sockaddr.h` | fixed `struct sockaddr_storage`, force-included with `-include` |

The list of wrapped functions and how to link everything is in
`scripts/waifulib/ps4.py` of xash3d-fwgs.
