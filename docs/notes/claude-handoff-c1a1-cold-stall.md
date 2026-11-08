# Handoff: c1a1 cold-boot stall after headcrab.mdl (2026-11-06/07)

Status: received by Cursor. Claude finding; Cursor owns dig/fix.

## Report (Claude)

Cold boot into c1a1 (`-gcmap c1a1`, same path as loading a c1a1 save) stops
during model loads right after `headcrab.mdl`, then Dolphin exits cleanly ~4 s
later. **2 of 2** cold starts. Only cold-load stall that repeats. Via
**changelevel it passes 10/10**.

Logs on ceres:
- `~/.cache/xash3d-gc-bisect/.ai/logs/dolphin-probe-20261106-224202`
  (on disk as **`dolphin-probe-20261106-224200`** — likely 2 s typo)
- “matching r1 run” — **not found under that name** yet; only one log ends
  exactly after headcrab. Nearby c1a1 cold fail `dolphin-probe-20261107-002719`
  dies earlier (ARAM lightmap reserve), different symptom.

## Cursor confirmation from 224200

Last guest lines:

```
real studio loaded 'models/headcrab.mdl' (68.38 Kb) npc=3 view=1 budget=120.77 Kb/1,000 Kb
A signal was received. A second signal will force Dolphin to stop.
```

- Ladder: fail at `entity_spawn` (no MAP_READY).
- No `mem FAIL` / Host_Error — soft hang, probe SIGTERM ~4 s later.
- Next model on a **passing** c1a1 cold boot is **`models/zombie.mdl`**
  (~116 Kb, ~300 ms later). Hang is almost certainly inside/before that load’s
  first log line.

## Diff vs passing cold c1a1 (e.g. 092809 / 215645)

| | Fail 224200 | Pass |
|---|---|---|
| pre-surfaces `heap_free` | **25.05 Kb** | ~76–93 Kb |
| headcrab BSS bump at read | **124.82 Kb** | **120.82 Kb** (+4 Kb extra) |
| after headcrab | silence → signal | zombie → barney → scientist → … → MAP_READY |

Changelevel path still green (models often already resident / different MEM1 shape).

## Probe hygiene

Shared lock `/dev/shm/xash3d-gc-dolphin-probe.lock`; do not `pkill`/`killall` by pattern.
