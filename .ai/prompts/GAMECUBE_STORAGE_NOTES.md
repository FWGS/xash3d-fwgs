# GameCube Storage Notes

Current policy:

- `gcdisc:/` and the generated ISO/FST content are read-only.
- Legal local Half-Life assets stay outside Git under ignored local paths.
- Generated configs, saves, logs, screenshots, and `.xash_id` must not be
  written to read-only disc paths.

Implementation guidance:

- Route writes to an explicit writable backend when one exists.
- Missing writable storage should fail safely and visibly, not corrupt state or
  loop forever.
- Prefer read-only fallbacks for boot probes until save/load work is active.
- Verify first boot, second boot, missing storage, and corrupted config cases.

Disc staging:

- `scripts/build-gamecube-disc.py` stages legal local assets into a generated
  image. Generated images and proprietary assets remain ignored.
- Case mismatches and missing assets should be detected before runtime when
  possible.
