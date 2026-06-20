# Blockers

Record failed build or verification attempts here. Include the date, command,
essential error, and the smallest plausible next investigation. Do not record
speculative failures as facts.

- `OUT/bin/boot.dol` boots in Dolphin 2603a and initializes the statically
  linked filesystem module, but it has not been tested on physical hardware.
- The Dolphin test profile has no GameCube-accessible FAT or DVD volume.
  `fatInitDefault()` fails, then the `/` fallback is stripped to an empty path.
- The GameCube build reports a `-Wstringop-overflow` warning in
  `SV_InitEdict`; its validity and impact need a separate focused audit.

## Resolved locally

- The GameCube platform mapping is committed as `663a601` on the submodule's
  local `gamecube-platform` branch, and parent commit `0f5cf35f` records that
  pointer. The submodule commit still needs to be published to an accessible
  remote before other clones can fetch it.
