# GameCube Audio Notes

Current state:

- The GameCube port has a stable null audio backend in
  `engine/platform/gamecube/snddma_gamecube.c`.
- `SNDDMA_Init` may satisfy the engine contract while leaving `snd.buffer`
  NULL for silent low-memory operation.
- `S_UpdateChannels` must return early when no DMA buffer exists.
- The null backend is a fallback, not the final audio implementation.

Future real audio work:

- Prefer libogc DSP/AI integration isolated to GameCube audio backend files.
- Keep the frame loop non-blocking.
- Preserve a documented null-audio fallback for memory triage.
- Treat ARAM as an explicit managed resource, not a transparent heap.
- Verify sound precache, ambient sound, weapon sound, shutdown, and map load.

Avoid:

- Desktop SDL/OpenAL assumptions.
- Large unbounded sound caches in MEM1.
- Blocking CD/audio streaming during map load.
