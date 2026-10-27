# GameCube Audio Notes

Current state:

- `engine/platform/gamecube/snddma_gamecube.c` implements libogc AI DMA playback at
  48 kHz with a 2048-sample stereo ring buffer by default.
- `-gcnullaudio` keeps the previous silent fallback for memory triage and boot probes.
- `-nosound` disables the entire sound subsystem (unchanged).
- `SOUND_DMA_SPEED` is 48000 on GameCube to match AI hardware rates.

Fallback behavior:

- Real init failure falls back to the null backend automatically.
- `S_UpdateChannels` returns early when `snd.buffer` is NULL (null mode).

Verification checklist:

- Map load with sound precache (no hang)
- `SNDDMA_Init` logs `audio DMA backend ready`
- Shutdown via `SNDDMA_Shutdown` without leak warnings
- Weapon/ambient sounds audible in Dolphin with `-log`

Avoid:

- Desktop SDL/OpenAL assumptions.
- Large unbounded sound caches in MEM1.
- Blocking CD/audio streaming during map load.
