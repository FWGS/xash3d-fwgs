# Live automation control

The goal supervisor reloads `.ai/config/automation-live.json` at the start of
each cycle. Edit that file while the overnight watchdog is running; changes
apply at the next cycle boundary and do not require restarting the runner.

Useful controls:

```json
{
  "pause": false,
  "discovery_mode": "prefer",
  "sleep_sec": 20,
  "AI_RUNTIME_PROBE_TIMEOUT": 90,
  "AIDER_MODEL_TIMEOUT_SEC": 300,
  "AI_STRICT_RUNTIME_PROGRESS": 1,
  "AI_GPU_INDEX": 0,
  "AI_GPU_MIN_FREE_MIB": 1024,
  "AI_GPU_RESUME_FREE_MIB": 1024,
  "AI_GPU_PAUSE_FREE_MIB": 2048,
  "AI_HOST_MIN_AVAILABLE_MIB": 8192,
  "AI_RESOURCE_BACKOFF_SEC": 120,
  "AI_MAX_PATCH_LINES": 240
}
```

Set `pause` to `true` to hold at a safe cycle boundary. Set it back to
`false` to resume. Valid discovery modes are `off`, `after-goals`, `prefer`,
and `only`. Invalid values are ignored. The current applied values are shown
in `.ai/state/autoport-heartbeat.json`.

The watchdog checks GPU free memory with `nvidia-smi` and host available
memory with `free`. If either falls below its threshold, it stops only the
GameCube worker tree, waits, and retries. It does not stop the vLLM server.
GPU memory uses hysteresis: it pauses below `AI_GPU_PAUSE_FREE_MIB` and does
not resume until it reaches `AI_GPU_RESUME_FREE_MIB`, preventing VRAM
fluctuations from repeatedly starting and killing workers.

With `AI_STRICT_RUNTIME_PROGRESS` enabled, a runtime patch is retained only
when the post-probe readiness score advances. Otherwise the supervisor
restores the pre-pass commit and records a discard decision in
`.ai/state/experiment-latest.json`.

Runtime discovery also binds specific evidence to its owning source area. For
example, a `delta.lst`/`Delta_InitFields` failure is routed to server delta
initialization instead of a generic model or renderer file. The patch gate
rejects unrelated targets and obvious no-op fallback edits before they can be
committed. The outer watchdog uses an atomic process lock so a second
watchdog cannot launch a competing runner for the same repository.

Discovery stores rejected experiment keys in
`.ai/state/discovery-hypotheses.json`; after two failed attempts at the same
hypothesis, that experiment is quarantined and discovery moves to another
route. Successful runtime acceptance also requires memory telemetry and no
allocation-failure marker, in addition to map, input, visual, and frame-timing
evidence.
