#!/usr/bin/env bash
set -euo pipefail

echo "== AI review =="

changed="$(git diff --name-only HEAD~1..HEAD || true)"
echo "$changed"

# Commit messages are generated deterministically by the harness. Keep them
# conventional and single-line so model deliberation can never enter history.
subject="$(git log -1 --format=%s)"
message="$(git log -1 --format=%B)"
nonempty_lines="$(printf '%s\n' "$message" | sed '/^[[:space:]]*$/d' | wc -l)"
if (( ${#subject} > 72 )) || (( nonempty_lines != 1 )) || \
  [[ ! "$subject" =~ ^(fix|feat|build|chore|ci|docs|style|refactor|perf|test):\ [[:alnum:]] ]]; then
  echo "Rejecting: commit message must be one conventional line (72 chars max)"
  printf '%s\n' "$message"
  exit 1
fi

if printf '%s\n' "$message" | grep -Eiq \
  '(<think>|self-correction|output generation|the user wants|tokens: [0-9])'; then
  echo "Rejecting: model deliberation detected in commit message"
  exit 1
fi

# No huge patch. Binary files contribute zero here but remain visible in the
# changed-file list above for human review.
lines="$(git diff --numstat HEAD~1..HEAD | awk \
  '$1 != "-" && $2 != "-" { total += $1 + $2 } END { print total + 0 }')"
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

# Port plan must be updated
if ! git diff --name-only HEAD~1..HEAD | grep -qx 'docs/GAMECUBE_PORT_PLAN.md'; then
  echo "Rejecting: docs/GAMECUBE_PORT_PLAN.md was not updated"
  exit 1
fi

# No proprietary SDK references
if git grep -n -i -E 'dolphin [s]dk|revolution [s]dk|official nintendo [s]dk' HEAD -- . ':!docs/GAMECUBE_PORT_PLAN.md'; then
  echo "Rejecting: proprietary SDK reference found"
  exit 1
fi

echo "review: OK"
