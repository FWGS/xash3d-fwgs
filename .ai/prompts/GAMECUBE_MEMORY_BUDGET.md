# GameCube Memory Budget Notes

Hard target:

- MEM1 main memory is 24 MiB. Every gameplay feature competes for this.

Track high-water points:

- Filesystem mount and asset search paths.
- HLSDK server/client static initialization.
- Server progs/entity setup.
- BSP load, clipnodes, textures, lightmaps, models, sprites.
- Client HUD and renderer registration.
- Sound precache and streaming buffers.
- Save/load and writable-storage buffers.

Rules of thumb:

- Record map name and allocation size for failures.
- Prefer one bounded reduction with evidence over broad cache rewrites.
- Do not treat ARAM as normal malloc space.
- Keep compatibility probes tied to memory evidence when possible.
