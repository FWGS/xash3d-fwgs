#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUI="$ROOT/scripts/xash3d-gc-aider-gui.py"

if python3 -c 'import PyQt6' >/dev/null 2>&1; then
	exec python3 "$GUI" "$@"
fi

GOPEX_PYTHON="${GOPEX_PYTHON:-$HOME/GopexLLC/.venv/bin/python}"
if [[ -x "$GOPEX_PYTHON" ]] && "$GOPEX_PYTHON" -c 'import PyQt6' >/dev/null 2>&1; then
	exec "$GOPEX_PYTHON" "$GUI" "$@"
fi

echo "PyQt6 is required. Install it for python3 or set GOPEX_PYTHON." >&2
exit 1
