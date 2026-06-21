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
scripts/ai-aider-pass.sh
```

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

Logs are written under `.ai/logs/` and ignored by Git. A failed pass appends a
short entry to `.ai/state/BLOCKERS.md`; review and commit or discard that entry
before trying another pass.

## GameCube port GUI

Launch the supervised Qt control panel with:

```sh
scripts/xash3d-gc-aider-gui.sh
```

The GUI runs the same guarded scripts; it does not bypass clean-tree checks or
review gates. It provides one-pass task editing, bounded multi-pass runs,
verification, DOL/disc builds, and Dolphin launch. Custom one-pass prompts are
stored outside the repository so editing a prompt cannot dirty the worktree.
