#!/usr/bin/env bash
set -Eeuo pipefail

# Long-running Continue supervisor for the Xash3D -> Nintendo GameCube port.
#
# Usage:
#   chmod +x run-gamecube-port-agent.sh
#   ./run-gamecube-port-agent.sh ~/Desktop/xash3d-gc
#
# Optional environment variables:
#   HOURS=24                         Maximum wall-clock runtime; 0 = no deadline
#   MAX_PASSES=200                   Maximum Continue passes; 0 = unlimited
#   CONTINUE_BIN=cn                  Continue CLI executable
#   AUTO_FLAG=--auto                 Continue automatic tool approval flag
#   MODEL_PROMPT_FILE=.continue/gamecube-port-task.md
#   STATUS_FILE=docs/gamecube/PORT_STATUS.md
#   STALL_LIMIT=3                    Stop after N no-progress passes
#   PASS_PAUSE_SECONDS=15            Pause between successful passes
#   FAILURE_PAUSE_SECONDS=45         Pause after failed passes
#   CREATE_BRANCH=0                  Create/switch to master
#   COMMIT_DIRTY_BASELINE=0          Commit pre-existing changes before starting
#
# Recommended:
#   tmux new-session -d -s xash3d-gamecube \
#     "cd ~/Desktop/xash3d-gc && HOURS=24 ./run-gamecube-port-agent.sh ."
#
# This supervisor does not modify or include copyrighted Half-Life assets.

REPO="${1:-$PWD}"
REPO="$(realpath "$REPO")"

HOURS="${HOURS:-24}"
MAX_PASSES="${MAX_PASSES:-200}"
CONTINUE_BIN="${CONTINUE_BIN:-cn}"
AUTO_FLAG="${AUTO_FLAG:---auto}"
MODEL_PROMPT_FILE="${MODEL_PROMPT_FILE:-.continue/gamecube-port-task.md}"
STATUS_FILE="${STATUS_FILE:-docs/gamecube/PORT_STATUS.md}"
STALL_LIMIT="${STALL_LIMIT:-3}"
PASS_PAUSE_SECONDS="${PASS_PAUSE_SECONDS:-15}"
FAILURE_PAUSE_SECONDS="${FAILURE_PAUSE_SECONDS:-45}"
CREATE_BRANCH="${CREATE_BRANCH:-0}"
COMMIT_DIRTY_BASELINE="${COMMIT_DIRTY_BASELINE:-0}"

LOG_DIR="$REPO/.agent-logs/gamecube"
STATE_DIR="$REPO/.continue/gamecube-agent"
LOCK_FILE="$STATE_DIR/supervisor.lock"
PASS_COUNTER_FILE="$STATE_DIR/pass-counter"
LAST_FINGERPRINT_FILE="$STATE_DIR/last-fingerprint"
STALL_COUNTER_FILE="$STATE_DIR/stall-counter"
SUMMARY_FILE="$STATE_DIR/last-pass-summary.txt"
WORKING_MEMORY_FILE="$STATE_DIR/working-memory.md"
PASS_CONTEXT_FILE="$STATE_DIR/pass-context.md"
PROMPT_PATH="$REPO/$MODEL_PROMPT_FILE"
STATUS_PATH="$REPO/$STATUS_FILE"
GOALS_PATH="$REPO/.ai/goals/GAMECUBE_PORT_GOALS.md"
PLAN_PATH="$REPO/docs/GAMECUBE_PORT_PLAN.md"

mkdir -p "$LOG_DIR" "$STATE_DIR" "$(dirname "$PROMPT_PATH")"

log() {
    printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

die() {
    log "ERROR: $*"
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

is_nonnegative_integer() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

require_command git
require_command "$CONTINUE_BIN"
require_command flock
require_command sha256sum
require_command tee
require_command timeout

is_nonnegative_integer "$HOURS" || die "HOURS must be a non-negative integer."
is_nonnegative_integer "$MAX_PASSES" || die "MAX_PASSES must be a non-negative integer."
is_nonnegative_integer "$STALL_LIMIT" || die "STALL_LIMIT must be a non-negative integer."
is_nonnegative_integer "$PASS_PAUSE_SECONDS" || die "PASS_PAUSE_SECONDS must be a non-negative integer."
is_nonnegative_integer "$FAILURE_PAUSE_SECONDS" || die "FAILURE_PAUSE_SECONDS must be a non-negative integer."

[[ -d "$REPO/.git" ]] || die "Not a Git repository: $REPO"

cd "$REPO"

# Prevent two supervisors from editing the same checkout.
exec 9>"$LOCK_FILE"
flock -n 9 || die "Another GameCube supervisor is already running for this checkout."

if [[ ! -f "$PROMPT_PATH" ]]; then
    cat >"$PROMPT_PATH" <<'PROMPT'
You are Continue running Qwen3 Coder Next as an autonomous senior engine-porting agent.

MISSION
Completely migrate the current Xash3D repository so legally owned, unmodified
Half-Life 1 game data can run natively on Nintendo GameCube hardware.

This is an engine and platform port. Do not remake, redesign, reinterpret, or
modernize Half-Life. Do not commit copyrighted Half-Life assets. Do not require
manual modification of BSP, WAD, MDL, SPR, WAV, PAK, configuration, script, or
other original game-data files.

TARGET
- PowerPC Gekko CPU
- Flipper GPU
- Native GX renderer
- devkitPPC and libogc
- GameCube controller input
- GameCube-compatible audio
- DOL output suitable for real hardware
- Approximately 24 MB main memory and 16 MB auxiliary memory
- Big-endian correctness
- Real hardware constraints are authoritative

PERMITTED ASSET HANDLING
The engine may decode or transform original assets in memory and may generate
disposable, versioned, reproducible caches. Original source assets must remain
unchanged and remain the authoritative copy.

MANDATORY FIRST ACTIONS ON EVERY PASS
1. Inspect:
   - git status
   - current branch
   - recent commits
   - docs/gamecube/PORT_STATUS.md
   - docs/gamecube/GAMECUBE_PORT_PLAN.md
   - docs/gamecube/GAMECUBE_PORT_AUDIT.md
   - current build and test logs
   - unfinished TODO/FIXME items related to the active milestone
2. Determine the next incomplete milestone from durable repository state.
3. Do not redo completed work.
4. Continue from the current implementation rather than restarting the port.

MILESTONE ORDER
1. Repository and dependency audit
2. devkitPPC/libogc toolchain and reproducible DOL build
3. GameCube platform bootstrap
4. Core portability, timing, filesystem, memory, and big-endian correctness
5. Static registration of Half-Life client/server game modules
6. GX initialization, framebuffer, FIFO, matrices, depth, and first frame
7. Original texture loading, GX conversion, swizzling, caching, and upload
8. BSP loading, PVS, culling, static GX world geometry, sky, and water
9. Lightmaps, TEV material configurations, fog, alpha test, and blending
10. Studio models, viewmodels, sprites, particles, beams, decals, transparency
11. GameCube controller input, menus, console, HUD, and text
12. Native audio, WAV playback, positional channels, and bounded streaming
13. Single-player integration, AI, scripted sequences, transitions, save/load
14. Memory budgeting, streaming, eviction, display lists, profiling, frame pacing
15. UDP/network backend after single-player is stable
16. Dolphin and real-hardware stabilization
17. Reproducible release candidate and legal asset-copy workflow

REQUIRED DURABLE DOCUMENTS
Maintain and update:
- docs/gamecube/GAMECUBE_PORT_AUDIT.md
- docs/gamecube/GAMECUBE_PORT_PLAN.md
- docs/gamecube/BUILDING_GAMECUBE.md
- docs/gamecube/GX_RENDERER_DESIGN.md
- docs/gamecube/GAME_MODULE_LINKING.md
- docs/gamecube/ENDIANNESS_AUDIT.md
- docs/gamecube/MEMORY_BUDGET.md
- docs/gamecube/ASSET_DEPLOYMENT.md
- docs/gamecube/ASSET_CACHE_FORMAT.md
- docs/gamecube/HARDWARE_TEST_MATRIX.md
- docs/gamecube/PORT_STATUS.md

PORT_STATUS.md must state:
- current milestone
- completed work
- build status
- Dolphin status
- real-hardware status
- known failures
- memory status
- performance status
- next task
- external blockers

WORKING RULES
- Inspect relevant code before editing.
- Preserve existing user changes.
- Keep GameCube-specific code behind clean platform interfaces.
- Prefer small, reviewable, buildable changes.
- Do not implement a generic OpenGL-to-GX wrapper.
- Map Xash3D render concepts deliberately to GX and TEV.
- Do not disable PVS, lightmaps, game modules, or major gameplay systems merely
  to claim progress.
- Do not scatter unexplained GEKKO conditionals when an abstraction is cleaner.
- Do not suppress build errors or silently stub required behavior.
- Temporary bootstrap stubs must be explicit, logged, documented, and removed
  in the appropriate milestone.
- Run targeted host tests when possible.
- Run the GameCube build after relevant changes.
- Keep ELF, DOL, linker map, warnings, binary size, and memory findings visible.
- Use Dolphin for iteration, but do not treat Dolphin-only success as hardware
  completion.
- Never commit original Half-Life assets.
- Create one descriptive git commit per completed logical milestone or coherent
  sub-milestone.
- Never force-push or rewrite unrelated history.

GX REQUIREMENTS
The native GX backend must eventually support:
- BSP world surfaces, PVS, frustum culling, batching/display lists
- original textures and palettes converted at runtime or into disposable caches
- GX tiled/swizzled texture layouts
- appropriate I4/I8/IA4/IA8/RGB565/RGB5A3/CMPR/RGBA8 selection
- base texture × lightmap through explicit TEV configurations
- fullbright behavior where required
- alpha test, translucent and additive blending
- studio models and skeletal animation
- sprites, particles, beams, decals, dynamic effects
- water, sky, fog, UI, console, fonts, HUD, and crosshair
- instrumentation for surfaces, triangles, vertices, texture binds, TEV state
  changes, display-list calls, frame time, and memory use

VALIDATION
After each logical change:
1. Review the diff.
2. Run the smallest relevant test or build.
3. Fix regressions introduced by the change.
4. Update docs/gamecube/PORT_STATUS.md with actual results.
5. Commit the completed coherent change.
6. Continue to the next incomplete item.

COMPLETION
The port is complete only when:
- a reproducible devkitPPC build produces a valid DOL;
- the executable boots through a supported GameCube loader;
- video, GX, input, audio, storage, timing, and memory initialize;
- legally supplied unmodified Half-Life data is detected and loaded;
- BSPs, textures, lightmaps, models, sprites, effects, HUD, menus, and sound work;
- a new single-player game starts and core gameplay, AI, scripts, doors, trains,
  combat, saves, and map transitions are usable;
- memory stays within hardware limits;
- Dolphin results and a real-hardware validation path/result are documented;
- installation and asset-copy instructions are complete;
- no copyrighted Half-Life assets are committed.

STOP ONLY WHEN
- all feasible documented milestones are complete and validated; or
- a genuine external blocker is precisely documented with evidence, attempted
  solutions, affected milestone, and the smallest required user action.

At the end of this pass, print a machine-readable final marker on its own line:

AGENT_RESULT: COMPLETE
or
AGENT_RESULT: BLOCKED
or
AGENT_RESULT: CONTINUE

Then report:
- current branch
- milestone worked
- files changed
- commits created
- builds/tests run
- DOL/ELF paths and sizes
- memory/performance findings
- Dolphin and real-hardware status
- original-asset compatibility status
- unresolved failures
- exact next milestone

Begin now.
PROMPT

    log "Created default task prompt: $PROMPT_PATH"
fi

if [[ "${CREATE_BRANCH:-0}" == "1" ]]; then
    current_branch="$(git branch --show-current)"
    if [[ -z "$current_branch" ]]; then
        die "Detached HEAD detected. Check out or create a branch before running."
    fi

    if [[ "$current_branch" != "master" ]]; then
        if git show-ref --verify --quiet refs/heads/master; then
            if [[ -n "$(git status --porcelain)" ]]; then
                die "Working tree has changes; cannot safely switch to master."
            fi
            git switch master
        else
            git switch -c master
        fi
    fi
fi

if [[ -n "$(git status --porcelain)" ]]; then
    if [[ "$COMMIT_DIRTY_BASELINE" == "1" ]]; then
        git add -A
        git commit -m "chore(gamecube): checkpoint before autonomous porting" || true
    else
        log "WARNING: Repository has pre-existing uncommitted changes."
        log "They will be preserved, but autonomous commits may include them if the agent stages broadly."
        log "Use a clean worktree or set COMMIT_DIRTY_BASELINE=1 after reviewing the changes."
    fi
fi

start_epoch="$(date +%s)"
if (( HOURS > 0 )); then
    deadline_epoch=$((start_epoch + HOURS * 3600))
else
    deadline_epoch=0
fi

pass="$(cat "$PASS_COUNTER_FILE" 2>/dev/null || printf '0')"
stall_count="$(cat "$STALL_COUNTER_FILE" 2>/dev/null || printf '0')"

is_nonnegative_integer "$pass" || pass=0
is_nonnegative_integer "$stall_count" || stall_count=0

repository_fingerprint() {
    local fingerprint_paths=()
    local path
    for path in \
        docs/gamecube \
        engine/platform/gamecube \
        engine/render/gx \
        engine/audio/gamecube \
        engine/input/gamecube \
        cmake/toolchains \
        scripts/gamecube
    do
        [[ -d "$path" ]] && fingerprint_paths+=("$path")
    done

    {
        git rev-parse HEAD
        git status --porcelain=v1
        git diff --stat
        git diff --cached --stat
        if [[ -f "$STATUS_PATH" ]]; then
            sha256sum "$STATUS_PATH"
        else
            printf 'missing-status-file\n'
        fi
        if (( ${#fingerprint_paths[@]} )); then
            find "${fingerprint_paths[@]}" -type f -printf '%p %s %T@\n' 2>/dev/null | sort
        fi
    } | sha256sum | awk '{print $1}'
}

update_working_memory() {
    local logfile="$1"
    local marker="$2"
    local status="$3"
    local task_line blocker_line next_line verify_line files_line

    task_line="$(
        grep -E '^(task|current task|milestone worked)[[:space:]]*[:=-]' "$logfile" \
        | tail -n 1 | sed 's/^[[:space:]]*//' || true
    )"
    files_line="$(
        grep -E '^(files changed|changed files)[[:space:]]*[:=-]' "$logfile" \
        | tail -n 1 | sed 's/^[[:space:]]*//' || true
    )"
    verify_line="$(
        grep -E '^(verification|builds/tests run|tests run)[[:space:]]*[:=-]' "$logfile" \
        | tail -n 1 | sed 's/^[[:space:]]*//' || true
    )"
    blocker_line="$(
        grep -E '^(blocker|unresolved failures)[[:space:]]*[:=-]' "$logfile" \
        | tail -n 1 | sed 's/^[[:space:]]*//' || true
    )"
    next_line="$(
        grep -E '^(next task|exact next milestone)[[:space:]]*[:=-]' "$logfile" \
        | tail -n 1 | sed 's/^[[:space:]]*//' || true
    )"

    {
        printf '# GameCube Agent Working Memory\n\n'
        printf '- Updated: %s\n' "$(date '+%Y-%m-%d %H:%M:%S %Z')"
        printf '- Branch: %s\n' "$(git branch --show-current)"
        printf '- HEAD: %s\n' "$(git rev-parse --short HEAD)"
        printf '- Pass: %s\n' "$pass"
        printf '- Exit status: %s\n' "$status"
        printf '- Result marker: %s\n' "${marker:-missing}"
        [[ -n "$task_line" ]] && printf '- %s\n' "$task_line"
        [[ -n "$files_line" ]] && printf '- %s\n' "$files_line"
        [[ -n "$verify_line" ]] && printf '- %s\n' "$verify_line"
        [[ -n "$blocker_line" ]] && printf '- %s\n' "$blocker_line"
        [[ -n "$next_line" ]] && printf '- %s\n' "$next_line"
        printf '- Log: %s\n' "$logfile"
    } >"$WORKING_MEMORY_FILE"
}

update_pass_context() {
    python3 - "$PASS_CONTEXT_FILE" "$WORKING_MEMORY_FILE" "$STATUS_PATH" "$GOALS_PATH" "$PLAN_PATH" <<'PY'
from pathlib import Path
import re
import sys

out_path = Path(sys.argv[1])
memory_path = Path(sys.argv[2])
status_path = Path(sys.argv[3])
goals_path = Path(sys.argv[4])
plan_path = Path(sys.argv[5])

def read(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")

def section(text: str, heading: str, limit: int = 80) -> list[str]:
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.strip() == heading:
            start = i
            break
    if start is None:
        return []
    collected = [lines[start]]
    for line in lines[start + 1:]:
        if line.startswith("## ") and line.strip() != heading:
            break
        collected.append(line)
        if len(collected) >= limit:
            break
    return collected

def grep_lines(text: str, pattern: str, limit: int = 20) -> list[str]:
    rx = re.compile(pattern)
    out = []
    for line in text.splitlines():
        if rx.search(line):
            out.append(line)
            if len(out) >= limit:
                break
    return out

memory = read(memory_path).strip()
status = read(status_path)
goals = read(goals_path)
plan = read(plan_path)

parts: list[str] = ["# GameCube Pass Context", ""]

if memory:
    parts += ["## Working memory", memory, ""]

if status:
    parts += ["## Port status excerpt"]
    parts += status.splitlines()[:80]
    parts += [""]

goal_focus = section(goals, "## Current focus (2026-07-18)", limit=120)
if goal_focus:
    parts += goal_focus + [""]

immediate_queue = grep_lines(goals, r"Immediate source queue|^\d+\.\s+\*\*G|^### G47[1-9]:|^### G480:", limit=80)
if immediate_queue:
    parts += ["## Queue excerpts"] + immediate_queue + [""]

plan_lines = grep_lines(
    plan,
    r"Current automatic goal arc:|Endgame / release goals|Current focus|Immediate source queue|Next automatic goal|G47[1-9]|G480",
    limit=40,
)
if plan_lines:
    parts += ["## Plan hints"] + plan_lines + [""]

parts += [
    "## Context rules",
    "- Use this file as the default startup context instead of loading large planning documents.",
    "- If more detail is needed, grep targeted ranges from the durable source files.",
    "- Do not treat this file as proof; verify against current repository state before claiming progress.",
    "",
]

out_path.write_text("\n".join(parts).rstrip() + "\n", encoding="utf-8")
PY
}

completion_from_repository() {
    [[ -f "$STATUS_PATH" ]] || return 1

    grep -qiE \
      '(^|[^A-Za-z])(release candidate|all milestones complete|port complete|status:[[:space:]]*complete)([^A-Za-z]|$)' \
      "$STATUS_PATH"
}

blocker_from_repository() {
    [[ -f "$STATUS_PATH" ]] || return 1

    grep -qiE \
      'external blocker:[[:space:]]*(yes|confirmed|unresolved)|status:[[:space:]]*blocked' \
      "$STATUS_PATH"
}

log "Repository: $REPO"
log "Branch: $(git branch --show-current)"
log "Prompt: $PROMPT_PATH"
log "Status: $STATUS_PATH"
log "Maximum hours: $HOURS"
log "Maximum passes: $MAX_PASSES"
log "Starting at pass: $((pass + 1))"

while :; do
    now="$(date +%s)"

    if (( deadline_epoch > 0 && now >= deadline_epoch )); then
        log "Reached HOURS=$HOURS deadline."
        break
    fi

    if (( MAX_PASSES > 0 && pass >= MAX_PASSES )); then
        log "Reached MAX_PASSES=$MAX_PASSES."
        break
    fi

    if completion_from_repository; then
        log "Repository status indicates all milestones are complete."
        break
    fi

    if blocker_from_repository; then
        log "Repository status indicates a documented external blocker."
        break
    fi

    pass=$((pass + 1))
    printf '%s\n' "$pass" >"$PASS_COUNTER_FILE"

    timestamp="$(date '+%Y%m%d-%H%M%S')"
    logfile="$LOG_DIR/pass-$(printf '%04d' "$pass")-$timestamp.log"
    before_fingerprint="$(repository_fingerprint)"
    update_pass_context

    log "Starting pass $pass."
    log "Log: $logfile"
    {
        printf '[%s] pass=%s start\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$pass"
        printf '[%s] branch=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$(git branch --show-current)"
        printf '[%s] head=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$(git rev-parse --short HEAD)"
        printf '[%s] prompt=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$PROMPT_PATH"
        printf '[%s] status=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$STATUS_PATH"
    } >>"$logfile"

    # Give each individual Continue invocation a generous ceiling while keeping
    # the outer supervisor in control. Eight hours supports deeper bounded
    # repair/build cycles while still terminating truly wedged passes.
    set +e
    timeout --signal=INT --kill-after=60s 8h \
        "$CONTINUE_BIN" \
        -p "$(cat "$PROMPT_PATH")" \
        "$AUTO_FLAG" \
        2>&1 | tee "$logfile"
    continue_status=${PIPESTATUS[0]}
    set -e

    after_fingerprint="$(repository_fingerprint)"
    result_marker="$(
        grep -E 'AGENT_RESULT:[[:space:]]*(COMPLETE|BLOCKED|CONTINUE)' "$logfile" \
        | tail -n 1 \
        | sed -E 's/.*AGENT_RESULT:[[:space:]]*//' \
        || true
    )"

    {
        printf 'pass=%s\n' "$pass"
        printf 'timestamp=%s\n' "$timestamp"
        printf 'exit_status=%s\n' "$continue_status"
        printf 'result_marker=%s\n' "${result_marker:-missing}"
        printf 'before_fingerprint=%s\n' "$before_fingerprint"
        printf 'after_fingerprint=%s\n' "$after_fingerprint"
        printf 'head=%s\n' "$(git rev-parse HEAD)"
        printf 'branch=%s\n' "$(git branch --show-current)"
        printf 'log=%s\n' "$logfile"
    } >"$SUMMARY_FILE"
    update_working_memory "$logfile" "${result_marker:-missing}" "$continue_status"

    if [[ "$before_fingerprint" == "$after_fingerprint" ]]; then
        stall_count=$((stall_count + 1))
        printf '%s\n' "$stall_count" >"$STALL_COUNTER_FILE"
        log "Pass $pass made no detectable repository progress. Stall count: $stall_count/$STALL_LIMIT."
    else
        stall_count=0
        printf '0\n' >"$STALL_COUNTER_FILE"
        printf '%s\n' "$after_fingerprint" >"$LAST_FINGERPRINT_FILE"
        log "Pass $pass changed repository state."
    fi

    case "$result_marker" in
        COMPLETE)
            if completion_from_repository; then
                log "Agent and repository status both report completion."
                break
            fi
            log "Agent reported COMPLETE, but durable status does not confirm it. Continuing for verification."
            ;;
        BLOCKED)
            if blocker_from_repository; then
                log "Agent and repository status both report a genuine external blocker."
                break
            fi
            log "Agent reported BLOCKED without durable blocker documentation. Continuing for another grounded pass."
            ;;
        CONTINUE|"")
            ;;
    esac

    if (( STALL_LIMIT > 0 && stall_count >= STALL_LIMIT )); then
        log "Stopping after $stall_count consecutive no-progress passes."
        log "Review $SUMMARY_FILE and the latest logs before resuming."
        break
    fi

    if (( continue_status != 0 )); then
        log "Continue exited with status $continue_status."
        log "Waiting $FAILURE_PAUSE_SECONDS seconds before retrying."
        sleep "$FAILURE_PAUSE_SECONDS"
    else
        sleep "$PASS_PAUSE_SECONDS"
    fi
done

log "Supervisor stopped."
log "Final branch: $(git branch --show-current)"
log "Final HEAD: $(git rev-parse --short HEAD)"
log "Working tree:"
git status --short || true

if [[ -f "$STATUS_PATH" ]]; then
    log "Current GameCube port status:"
    tail -n 80 "$STATUS_PATH" || true
else
    log "No $STATUS_FILE exists yet."
fi

log "Logs: $LOG_DIR"
log "Last pass summary: $SUMMARY_FILE"
