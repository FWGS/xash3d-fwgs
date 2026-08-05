Swiss/libogc2 runtime progress: multi-volume FAT + return-to-loader.

libdvm volumes probed (in order):
- `sd:` SD2SP2
- `carda:` / `cardb:` SD Gecko

Markers:
- `FAT volume ready sd:/|carda:/|cardb:/`
- `FAT preferred volume …`
- quit → Swiss via libogc2 `STUBHAXX` exit stub

Rebuild on a libogc2 machine, then Swiss/Dolphin validation.
