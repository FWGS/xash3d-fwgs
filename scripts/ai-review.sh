#!/usr/bin/env bash
set -euo pipefail

echo "== AI review =="

changed="$(git diff --name-only HEAD~1..HEAD || true)"
echo "$changed"

source_only_discovery_ok() {
  [[ "${AI_REVIEW_ALLOW_SOURCE_ONLY_DISCOVERY:-0}" == "1" ]] || return 1
  [[ -n "$changed" ]] || return 1
  if grep -Evq '^(engine/|ref/|common/|filesystem/|public/|stub/)' <<<"$changed"; then
    return 1
  fi
  return 0
}

# Commit messages are generated deterministically by the harness. Keep the
# subject conventional; allow a structured body for goal evidence and context.
subject="$(git log -1 --format=%s)"
message="$(git log -1 --format=%B)"
if (( ${#subject} > 72 )) || \
  [[ ! "$subject" =~ ^(fix|feat|build|chore|ci|docs|style|refactor|perf|test):\ [[:alnum:]] ]]; then
  echo "Rejecting: commit subject must be conventional and 72 chars max"
  printf '%s\n' "$message"
  exit 1
fi

if printf '%s\n' "$message" | grep -Eiq \
  '(<think>|self-correction|output generation|the user wants|tokens: [0-9]|SEARCH/REPLACE|<<<<<<<|>>>>>>>)'; then
  echo "Rejecting: model deliberation detected in commit message"
  exit 1
fi

# No huge patch. Binary files contribute zero here but remain visible in the
# changed-file list above for human review. Exclude local GUI WIP from sizing.
lines="$(git diff --numstat HEAD~1..HEAD | awk \
	'$3 != "scripts/xash3d-gc-aider-gui.py" && $1 != "-" && $2 != "-" { total += $1 + $2 } END { print total + 0 }')"
if [ "$lines" -gt 400 ]; then
  echo "Rejecting: patch too large: $lines changed lines"
  exit 1
fi

# No deleted files
if git diff --diff-filter=D --quiet HEAD~1..HEAD --; then
  :
else
  echo "Rejecting: deleted files detected"
  git diff --name-status --diff-filter=D HEAD~1..HEAD
  exit 1
fi

# Port plan must be updated unless a deliberately sliced G24 renderer pass kept
# the large plan out of model context to avoid local token-limit failures.
if ! git diff --name-only HEAD~1..HEAD | grep -qx 'docs/GAMECUBE_PORT_PLAN.md'; then
  if source_only_discovery_ok; then
    echo "review: accepting source-only discovery commit without port plan"
  elif [[ "$subject" == "chore: update GameCube porting GUI" ]] && \
    [[ -z "$(git diff --name-only HEAD~1..HEAD | grep -Ev '^(scripts/xash3d-gc-aider-gui\.(py|sh))$' || true)" ]]; then
    echo "review: accepting standalone GUI maintenance commit without port plan"
  elif [[ "$subject" =~ ^feat:\ (wire|bound|stabilize|reduce|simplify)\ GameCube\ .* ]] && \
    git diff --name-only HEAD~1..HEAD | grep -Eq '^(ref/gx/|engine/platform/gamecube/vid_gamecube\.c|engine/client/cl_sprite\.c)'; then
    echo "review: accepting sliced G24 renderer source pass without port plan"
  elif [[ "$subject" == "perf: improve GameCube frame budget" ]] && \
    [[ -z "$(git diff --name-only HEAD~1..HEAD | grep -Ev '^(engine/|ref/)' || true)" ]]; then
    echo "review: accepting sliced G36 perf pass without port plan"
  else
    echo "Rejecting: docs/GAMECUBE_PORT_PLAN.md was not updated"
    exit 1
  fi
fi

# No proprietary SDK references
if git grep -n -i -E 'dolphin [s]dk|revolution [s]dk|official nintendo [s]dk' HEAD -- . ':!docs/GAMECUBE_PORT_PLAN.md'; then
  echo "Rejecting: proprietary SDK reference found"
  exit 1
fi

echo "review: OK"
