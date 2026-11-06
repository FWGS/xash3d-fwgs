# Shelved: edict-table hold for retail New Game (cold route)

Status: not applied. Memory is owned by the Cursor agent; this records the
findings for whoever picks it up. Patch: `gamecube-edict-hold-shelved.patch`
(against 1b2758072f, before 56f7be4c5b moved edicts/baselines/statics to BSS).

## What the patch does

- `GC_HoldEdictTable()` mallocs `sizeof(edict_t) * max_edicts + 4 KiB` at the
  menu handoff (`SCR_StopCinematic`, right after the `menu_handoff` heap mark),
  with a fallback at the start of `SV_GameCubePlayStart_f`.
  `GC_ReleaseEdictHold()` frees it just before the edict `Mem_Calloc` in
  `SV_LoadProgs`, which then gets the same coalesced span.
- GX FIFO 512 -> 256 KiB (libogc default; frees 256 KiB of MEM1 BSS).
- Software surface cache deferred until the first map load on retail GX
  (not with `-gcsoftworld` / `-gcmap`).
- Menu art (`ref_gx zone` in menu scope) and HUD pool slabs go to the boot
  arena first, heap second.

## Measurements (Dolphin, cold route: `DOLPHIN_RETAIL=1 DOLPHIN_REQUIRE_MENU_ACTIONS=1`)

- At `menu_handoff` the heap top is one contiguous ~950 KiB unclaimed span
  (used 3.06 MB, free 86-122 KiB, unclaimed 938-950 KiB).
- By play start the menu has carved ~290 KiB off that span
  (unclaimed 655 KiB), so a hold taken at play start fails, and the edict
  `Mem_Calloc` fails with ~970 KiB free but fragmented
  ("out of memory 706.64 Kb at sv_game.c:6541").
- With the hold taken at `menu_handoff`: hold succeeds, edicts allocate.
  The next failure is the static_entities/baselines allocation:
  `mem FAIL subsystem=Zone Engine size=298.83 Kb at sv_game.c:6543`
  (heap_free 171 KiB, unclaimed 76 KiB).
- Cost of holding 710 KiB through the menu: two `ImageLib Pool` 75 KiB
  failures at img_utils.c:1502 during menu art load (unclaimed 68 KiB).
  The boot arena peaked at 535 KiB of 1 MiB, so more menu art could move there.
- The `DOLPHIN_MENU_NEWGAME=1` (ngr) route loads server progs at boot and is
  not on this path; it fails at `bsp_open` with ~10 KiB avail at the menu
  both with and without this patch.

## Suggested direction

Take the edicts, baselines and statics (706 + ~300 KiB) as one block at the
menu handoff, or let a BSS block double as that hold, and push the remaining
menu art into the boot arena so the menu heap floor (200 KiB) holds.
