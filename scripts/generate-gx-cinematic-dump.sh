#!/usr/bin/env bash
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$ROOT/.ai/gx-cinematic-dump-$STAMP"
mkdir -p "$OUT"
{
 date -Is
 git rev-parse HEAD
 git status --short
 sha256sum "$ROOT/Half-Life/valve/media/valve.gcvid" "$ROOT/OUT/bin/boot.dol" 2>/dev/null || true
} >"$OUT/manifest.txt"
git diff -- engine/client/avi/avi_gc.c engine/platform/gamecube/mem_gamecube.c ref/gx/r_gx_world.c ref/gx/r_draw.c >"$OUT/source-diff.patch" || true
(
 cd "$ROOT"
 DOLPHIN_RETAIL=1 DOLPHIN_SKIP_INTRO=0 DOLPHIN_SKIP_BUILD=1 \
 DOLPHIN_PREBUILT_ISO="${DOLPHIN_PREBUILT_ISO:-OUT/xash3d-gc.iso}" \
 DOLPHIN_TIMEOUT="${DOLPHIN_TIMEOUT:-60}" scripts/dolphin-boot-probe.sh
) >"$OUT/probe.stdout.log" 2>"$OUT/probe.stderr.log" || true
LATEST="$(ls -td "$ROOT"/.ai/logs/dolphin-probe-* 2>/dev/null | head -1 || true)"
if [[ -n "$LATEST" && -d "$LATEST" ]]; then
 cp -a "$LATEST" "$OUT/probe-log"
 find "$LATEST/dolphin-user/Dump/Frames" -maxdepth 1 -type f -name 'framedump_*.tga' | head -25 | while read -r f; do cp "$f" "$OUT/"; done
 if compgen -G "$OUT/framedump_*.tga" >/dev/null; then
  montage "$OUT"/framedump_*.tga -tile 5x -geometry 240x198+2+14 -label '%t' "$OUT/first-25-frames.png" || true
 fi
fi
tar -C "$ROOT/.ai" -czf "$OUT.tar.gz" "$(basename "$OUT")"
echo "$OUT.tar.gz"
