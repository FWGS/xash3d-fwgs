# Local Aider Harness

This harness gives Aider a bounded task, persistent read-only context, a real
GameCube build verifier, and conservative Git guardrails.

## Prerequisites

- Aider available on `PATH`.
- The local OpenAI-compatible model server listening on port 8072.
- `OPENAI_API_KEY` set in the shell, never committed to this repository.
- devkitPPC and libogc installed under `DEVKITPRO` (default:
  `/opt/devkitpro`).
- A clean Git worktree. The runner stops instead of absorbing existing work.

## One bounded pass

```sh
export OPENAI_API_BASE=http://127.0.0.1:8072/v1
export OPENAI_API_KEY=your-local-server-key
python3 scripts/aider-token-budget.py --sync-metadata
scripts/ai-aider-pass.sh
```

Autonomous passes use `.aider.automation.conf.yml` (smaller read context) and
`scripts/aider-token-budget.py` to query the live vLLM `/v1/models` endpoint and
set output/context/history budgets before each Aider attempt. Override with
`AIDER_AUTOMATION=0` to use the full interactive `.aider.conf.yml` profile.

## Several supervised passes

```sh
scripts/ai-loop.sh 5
```

Each successful pass must create no more than three commits, touch no more than
20 files or 2,000 lines, avoid tracked-file deletion, update the port plan, and
pass a complete GameCube build. Any failure stops the loop for human review.

Run only the harness and source checks without compiling by setting
`SKIP_GAMECUBE_BUILD=1` when invoking `scripts/ai-verify.sh`. Autonomous passes
always run the real build.

## Run until blocked

For a handoff-style run, start the local Qwable/vLLM model server, export
`OPENAI_API_KEY`, then run:

```sh
scripts/ai-run-until-done.py --chunk-passes 20 --recoverable-retries 8
```

The supervisor checks that the model API is reachable, invokes the evidence
gated goal loop in bounded chunks, and keeps going through recoverable pass
limits, model timeouts, and token-limit retries. It still stops on dirty-tree,
verification, review, or manual-hardware blockers.

Newly completed goals must carry command/log evidence in both the goal ledger
and `docs/GAMECUBE_PORT_PLAN.md`; `scripts/ai-evidence-gate.py` enforces this
during autonomous patch verification.

The model does not rely on vague built-in GameCube knowledge. A compact global
context pack lives under `.ai/prompts/`, and `scripts/ai-goal-loop.py` adds
focused subsystem notes as read-only Aider context for each goal. For example,
networking goals receive `GAMECUBE_NETWORKING_NOTES.md`, audio goals receive
`GAMECUBE_AUDIO_NOTES.md`, and memory-heavy gameplay goals receive
`GAMECUBE_MEMORY_BUDGET.md`. The clean-room GameCube Homebrew Compliance
profile is loaded into model context and checked by
`scripts/gamecube-homebrew-compliance-check.py`. Known bad assumptions are
recorded in `GAMECUBE_FAILURE_MEMORY.md` and enforced where practical by the
verifier.

Dolphin discovery is shared by the GUI, probes, and Qwable/Aider passes through
`scripts/gamecube-env.sh`. Override `DOLPHIN_EXECUTABLE` in `.env` with a native
binary path or use the Flatpak form:

```sh
DOLPHIN_EXECUTABLE=flatpak:org.DolphinEmu.dolphin-emu
```

For campaign/map compatibility evidence, run:

```sh
scripts/gamecube-map-compat-probe.sh c4a1f c0a0e
```

The probe writes ignored TSV/Markdown summaries under `.ai/logs/`. Use
`--all` only when intentionally sweeping every local BSP.

Logs are written under `.ai/logs/` and ignored by Git. A failed pass appends a
short entry to `.ai/state/BLOCKERS.md`; review and commit or discard that entry
before trying another pass.

## GameCube port GUI

Launch the supervised Qt control panel with:

```sh
scripts/xash3d-gc-aider-gui.sh
```

The GUI runs the same guarded scripts; it does not bypass clean-tree checks or
review gates. It runs the goal ledger through a configurable safety pass limit,
shows live Git/submodule/toolchain/content/blocker context, and provides
verification, DOL/disc builds, Dolphin launch, and a supervised Qwable-5
model-server control. The model command defaults to `qwable-5 --host
127.0.0.1 --port 8072` when that executable is available. Otherwise, when
`vllm` is on `PATH`, it loads `DJLougen/Qwable-5-27B-Coder` or the cached local
snapshot and serves it as `qwen-local`, matching `.aider.conf.yml`. Override the
load target with `QWABLE_5_MODEL`, the served name with `QWABLE_5_SERVED_NAME`,
startup limits with `QWABLE_5_MAX_MODEL_LEN`, `QWABLE_5_MAX_NUM_SEQS`, and
`QWABLE_5_GPU_MEMORY_UTILIZATION`, or the full command with
`QWABLE_5_COMMAND`. The API base defaults to `http://127.0.0.1:8072/v1`.
The default `QWABLE_5_MAX_MODEL_LEN` is `65536` so vLLM matches
`.ai/aider-model-metadata.json` and Aider has enough room for focused engine
source context without overloading the local server.
Credentials are still inherited from the launch environment and are not stored
by the GUI.
