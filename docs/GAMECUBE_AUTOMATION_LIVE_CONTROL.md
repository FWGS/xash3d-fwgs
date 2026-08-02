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
  "AI_MAX_PATCH_LINES": 240
}
```

Set `pause` to `true` to hold at a safe cycle boundary. Set it back to
`false` to resume. Valid discovery modes are `off`, `after-goals`, `prefer`,
and `only`. Invalid values are ignored. The current applied values are shown
in `.ai/state/autoport-heartbeat.json`.
