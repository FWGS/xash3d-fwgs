# Blockers

Record failed build or verification attempts here. Include the date, command,
essential error, and the smallest plausible next investigation. Do not record
speculative failures as facts.

- `OUT/bin/boot.dol` boots in Dolphin 2603a and initializes the statically
  linked filesystem module, but it has not been tested on physical hardware.
- A native-format disc image with the Half-Life data passes Dolphin's header
  and FST inspection. Dolphin 2603a Flatpak loads its DOL sections and FST, then
  its host CPU-GPU thread traps on a fixed `ud2` before guest entry. Direct DOL
  boot still works, so the disc path needs validation with another Dolphin
  build or physical homebrew-capable hardware.
- The GameCube build reports a `-Wstringop-overflow` warning in
  `SV_InitEdict`; its validity and impact need a separate focused audit.

## Resolved locally

- The GameCube platform mapping is committed as `663a601` on the submodule's
  local `gamecube-platform` branch, and parent commit `0f5cf35f` records that
  pointer. The submodule commit still needs to be published to an accessible
  remote before other clones can fetch it.

- 2026-06-20-174913: Verification failed after `5b52f58acbb6d8b8cb2d7d805f79f04e63cd2a5f`; see `.ai/logs/aider-pass-2026-06-20-174913.log`.
