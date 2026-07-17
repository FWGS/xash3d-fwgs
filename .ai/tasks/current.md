Continue the Xash3D GameCube port with one bounded patch.

Current mission (first open automatic goal):
G83 — Fix GameCube BSP PointInLeaf and parent-cycle PVS so New Game can leave
the full-vis leaf mark workaround.

Requirements:

- Prefer `engine/common/mod_bmodel.c` and `ref/gx/r_main.c` / `r_misc.c`.
- Prove `Mod_PointInLeaf` + MarkLeaves/FatPVS on `-gcnewgame` `c0a0` without
  hanging Host_Frame.
- Keep `MAP_READY`, `G36_STATUS: PASS`, and nonzero world pixels.
- Update `docs/GAMECUBE_PORT_PLAN.md` with commands and evidence.
- Run `DOLPHIN_NEWGAME=1 DOLPHIN_TIMEOUT=120 scripts/dolphin-boot-probe.sh`.
- Stop after this one patch.
