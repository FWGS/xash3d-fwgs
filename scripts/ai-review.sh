#!/usr/bin/env bash
set -euo pipefail

echo "== AI review =="

changed="$(git diff --name-only HEAD~1..HEAD || true)"
echo "$changed"

# No huge patch
lines="$(git diff --shortstat HEAD~1..HEAD | grep -oE '[0-9]+ insertion|[0-9]+ deletion' | awk '{s+=$1} END{print s+0}')"
if [ "$lines" -gt 400 ]; then
  echo "Rejecting: patch too large: $lines changed lines"
  exit 1
fi

# No deletions unless explicitly allowed
if git diff --name-status HEAD~1..HEAD | grep -q '^D'; then
  echo "Rejecting: deleted files detected"
  git diff --name-status HEAD~1..HEAD | grep '^D'
  exit 1
fi

# Port plan must be updated
if ! git diff --name-only HEAD~1..HEAD | grep -q '^docs/GAMECUBE_PORT_PLAN.md$'; then
  echo "Rejecting: docs/GAMECUBE_PORT_PLAN.md was not updated"
  exit 1
fi

# No proprietary SDK references
if git grep -n -i -E 'dolphin sdk|revolution sdk|official nintendo sdk' HEAD -- . ':!docs/GAMECUBE_PORT_PLAN.md'; then
  echo "Rejecting: proprietary SDK reference found"
  exit 1
fi

echo "review: OK"
