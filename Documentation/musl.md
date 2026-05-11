# Xash3D FWGS on `musl`

Xash3D FWGS works on `musl` out of the box. However, the engine doesn't try to differentiate glibc and musl anymore. If you see error similar to:

```
Host_InitError: can't initialize cl_dlls/client.so: Error relocating valve/cl_dlls/client.so: __sprintf_chk: symbol not found
```

... or you know that the game you're running is linked against glibc, you can try using `libgcompat`, like this:

```
$ LD_PRELOAD=/lib/libgcompat.so.0 ./xash3d ...
```

It will automatically add the missing symbols that glibc binaries usually need. In the future we might automatically link engine against `libgcompat` for better compatibility with prebuilt or closed-source games, if there will be any use for this.
