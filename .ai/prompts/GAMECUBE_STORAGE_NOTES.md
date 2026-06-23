# GameCube Storage Notes

Current policy:

- `gcdisc:/xash3d` and the generated ISO/FST content are read-only.
- Legal local Half-Life assets stay outside Git under ignored local paths.
- Generated configs, saves, logs, screenshots, and `.xash_id` must not be
  written to read-only disc paths.

Implementation (G28):

- `GCube_GetDiscPath()` returns `gcdisc:/xash3d` when the DVD is mounted.
- `GCube_GetWritablePath()` returns `sd:/xash3d` when an SD card is mounted.
- `FS_DetermineRootDirectory()` uses the writable SD path when available, else
  falls back to the disc path for read-only boot.
- `FS_DetermineReadOnlyRootDirectory()` always uses the disc path so game
  content is layered separately from SD writes.
- `Host_WriteConfig()` and `FS_SaveVFSConfig()` run only when
  `GCube_HasWritableStorage()` is true.
- `-gcnullaudio` and smoke probes without SD continue to use the read-only
  fallback without write errors.

Disc staging:

- `scripts/build-gamecube-disc.py` stages legal local assets into a generated
  image. Generated images and proprietary assets remain ignored.
- Case mismatches and missing assets should be detected before runtime when
  possible.
