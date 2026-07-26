#!/usr/bin/env bash







set -Eeuo pipefail

REPO="${1:-$HOME/Desktop/xash3d-gamecube}"
IDLE_MINUTES="${IDLE_MINUTES:-25}"

cd "$REPO"

while sleep 60; do
    timeout_pid="$(pgrep -f '^timeout --signal=INT --kill-after=60s .* cn -p' | head -1 || true)"
    [[ -n "$timeout_pid" ]] || continue

    pass="$(cat .continue/gamecube/pass-counter 2>/dev/null || echo 0)"
    pattern="pass-$(printf '%04d' "$pass")-*.log"
    log_file="$(find .agent-logs/gamecube -type f -name "$pattern" | sort | tail -1)"

    [[ -n "$log_file" ]] || continue

    recent_repo_change="$(
        find .ai docs engine ref scripts public \
            -type f -mmin "-$IDLE_MINUTES" \
            -print -quit 2>/dev/null || true
    )"

    non_heartbeat="$(
        tail -n 100 "$log_file" |
        grep -v 'still active; HEAD=' |
        tail -n 1 || true
    )"

    log_age="$(( $(date +%s) - $(stat -c %Y "$log_file") ))"
    if [[ -z "$recent_repo_change" ]] \
       && [[ -z "$non_heartbeat" ]] \
       && (( log_age < 90 )); then
        # The log is being touched only by heartbeat messages.
        heartbeat_count="$(
            tail -n "$((IDLE_MINUTES * 2 + 2))" "$log_file" |
            grep -c 'still active; HEAD=' || true
        )"

        if (( heartbeat_count >= IDLE_MINUTES * 2 )); then
            echo "Pass $pass appears idle for ${IDLE_MINUTES}m; interrupting PID $timeout_pid"
            kill -INT "$timeout_pid"
            sleep 120
        fi
    fi
done