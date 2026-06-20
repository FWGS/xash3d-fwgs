# Decisions

- Waf is the canonical build system for this port; do not add a parallel CMake
  or Make build unless a concrete dependency requires it.
- `scripts/build-gamecube.sh` is the canonical end-to-end build command.
- `XASH_GAMECUBE` is selected by Waf and also derived from `__GAMECUBE__` in
  the library-suffix support code.
- The harness requires a clean worktree before an autonomous pass so it cannot
  silently absorb a developer's unfinished changes.
