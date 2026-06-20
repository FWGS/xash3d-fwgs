# Blockers

Record failed build or verification attempts here. Include the date, command,
essential error, and the smallest plausible next investigation. Do not record
speculative failures as facts.

- The existing `OUT/bin/boot.dol` has not been confirmed to boot in Dolphin or
  on physical hardware.

## Resolved locally

- The GameCube platform mapping is committed as `663a601` on the submodule's
  local `gamecube-platform` branch, and parent commit `0f5cf35f` records that
  pointer. The submodule commit still needs to be published to an accessible
  remote before other clones can fetch it.
