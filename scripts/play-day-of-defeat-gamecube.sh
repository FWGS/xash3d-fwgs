#!/usr/bin/env bash
# Launch a Day of Defeat GameCube play disc in Dolphin.
# Requires OUT/bin/dodgc.dol (HLSDK_GAMECUBE_GAMEDIR=dod build) and retail dod/
# assets. This is NOT an HL1 launcher — see scripts/play-dolphin.sh for that.
# DoD gameplay also needs a viable client/server source; see
# docs/gamecube/MULTI_GAME.md (vgui2 research gate).
set -euo pipefail

usage()
{
	cat <<'EOF'
usage: scripts/play-day-of-defeat-gamecube.sh [options] [ISO]

Starts a Day of Defeat GameCube disc in the project's Dolphin with a mute toggle.

Requires:
  OUT/bin/dodgc.dol          (build: HLSDK_GAMECUBE_GAMEDIR=dod scripts/build-gamecube.sh)
  Half-Life/dod/             (or XASH3D_GC_DOD_DATA) retail assets
  Half-Life/valve/           auto-staged as basedir sibling when present

  --muted          start with Dolphin's audio muted
  --unmuted        start with sound on
  --record         record the session with sound to .ai/playtests/ (default)
  --no-record      do not record
  --save-default   remember --muted/--unmuted and --record/--no-record
  --no-toggle      do not open the mute toggle window
  --build          (re)build the DoD play disc first (OUT/xash3d-gc-dod-play.iso)
  --perf           build/launch a profiling play disc with the performance overlay
  --local-cache    launch the completed ISO from the local build cache (no HDD copy)
  --install-desktop  add "Xash3D GameCube DoD (Dolphin)" to the desktop app menu
  -h, --help       this help

ISO defaults to OUT/xash3d-gc-dod-play.iso. For Half-Life 1 use scripts/play-dolphin.sh.

Mute only silences the speakers (PulseAudio/PipeWire): the game keeps
mixing audio and running at full speed, and the recording keeps its sound. Toggle
it live with the big MUTED / SOUND ON window (click, Space or M there).

Recording: video + the game's sound (also while muted) to
.ai/playtests/<date>-play-<commit>.mp4 with a .json sidecar and INDEX.md;
skipped when the drive has less than 5 GB free.

Defaults live in ~/.config/xash3d-gc/play.conf (START_MUTED=1|0, default 0;
RECORD=1|0, default 1).
Dolphin settings for this launcher (controller mapping, video) live in
~/.local/share/xash3d-gc/dolphin; map your controller once under
Controllers > Port 1 > Standard Controller and it is kept.

Large temporary ISO and asset staging files default to
/mnt/datasets/xash3d-gc-iso-build. Override this with
XASH3D_GC_ISO_CACHE=/path/to/cache.
EOF
}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/xash3d-gc"
CONF="$CONF_DIR/play-dod.conf"
USER_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/xash3d-gc/dolphin-dod"
ISO="$ROOT/OUT/xash3d-gc-dod-play.iso"
DOD_DOL="$ROOT/OUT/bin/dodgc.dol"
DOD_DATA_DEFAULT=""
for candidate in \
	"${XASH3D_GC_DOD_DATA:-}" \
	"$ROOT/Half-Life/dod" \
	"$ROOT/Day of Defeat/dod" \
	"$ROOT/dod"
do
	[[ -n "$candidate" && -d "$candidate" ]] || continue
	DOD_DATA_DEFAULT="$candidate"
	break
done
START_MUTED=0
RECORD=1
TOGGLE=1
SAVE=0
BUILD=0
FORCE_BUILD=0
PERF=0
PUBLISH=1
BUILD_LOCK_HELD=0

[[ -f "$CONF" ]] && source "$CONF"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--muted) START_MUTED=1 ;;
		--unmuted) START_MUTED=0 ;;
		--record) RECORD=1 ;;
		--no-record) RECORD=0 ;;
		--save-default) SAVE=1 ;;
		--no-toggle) TOGGLE=0 ;;
		--build) BUILD=1; FORCE_BUILD=1 ;;
		--perf) PERF=1; BUILD=1; FORCE_BUILD=1; ISO="$ROOT/OUT/xash3d-gc-dod-play-perf.iso" ;;
		--local-cache) PUBLISH=0; BUILD=1; FORCE_BUILD=1 ;;
		--install-desktop)
			apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
			mkdir -p "$apps"
			cat > "$apps/xash3d-gc-dod-play.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Xash3D GameCube DoD (Dolphin)
Comment=Play Day of Defeat GameCube build in Dolphin (requires dodgc.dol)
Exec=$ROOT/scripts/play-day-of-defeat-gamecube.sh
Terminal=false
Categories=Game;
EOF
			echo "installed $apps/xash3d-gc-dod-play.desktop"
			exit 0 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
		*) ISO="$(realpath "$1")" ;;
	esac
	shift
done

if (( SAVE )); then
	mkdir -p "$CONF_DIR"
	printf 'START_MUTED=%d\nRECORD=%d\n' "$START_MUTED" "$RECORD" > "$CONF"
	echo "saved default: $([[ $START_MUTED == 1 ]] && echo muted || echo unmuted), $([[ $RECORD == 1 ]] && echo recording || echo not recording) ($CONF)"
fi

ENGINE_BIN="$ROOT/build/engine/xash"
BOOT_DOL="$DOD_DOL"
if [[ ! -f "$DOD_DOL" ]]; then
	echo "error: missing $DOD_DOL" >&2
	echo "Day of Defeat uses a separate statically linked DOL." >&2
	echo "  HLSDK_GAMECUBE_GAMEDIR=dod scripts/hlsdk-gamecube-build.sh" >&2
	echo "  HLSDK_GAMECUBE_GAMEDIR=dod scripts/build-gamecube.sh" >&2
	echo "Steam DoD also needs a non-vgui2 game client — see docs/gamecube/MULTI_GAME.md" >&2
	exit 1
fi
if [[ -z "$DOD_DATA_DEFAULT" ]]; then
	echo "error: Day of Defeat assets not found (tried Half-Life/dod, Day of Defeat/dod, \$XASH3D_GC_DOD_DATA)" >&2
	echo "Stage retail dod/ beside valve/, then rebuild. HL1 play stays scripts/play-dolphin.sh." >&2
	exit 1
fi
if [[ -f "$BOOT_DOL" && -f "$ISO" && "$BOOT_DOL" -nt "$ISO" ]]; then
	BUILD=1
	echo "==> DoD play ISO is older than dodgc.dol; rebuilding"
fi
if (( BUILD )) || [[ ! -f "$ISO" ]]; then
	# Never let Dolphin observe the image while the large data tree is still
	# being copied.  Serialize concurrent launcher windows and publish only a
	# completed image with an atomic rename.
	mkdir -p "$ROOT/OUT"
	# Remove only abandoned atomic publish files created by this launcher.
	# The age guard avoids touching a copy that another launcher may still own.
	find "$ROOT/OUT" -maxdepth 1 -type f -name 'xash3d-gc-dod-play*.iso.tmp.*' \
		-mtime +2 -delete 2>/dev/null || true
	exec 9>"$ROOT/OUT/.play-dod-disc.lock"
	flock 9
	BUILD_LOCK_HELD=1
	if (( ! FORCE_BUILD )) && [[ -f "$ISO" && "$BOOT_DOL" -ot "$ISO" ]]; then
		BUILD=0
	else
		echo "==> building DoD play disc $ISO"
		# Some tmpfs mounts report plenty of free space but impose a per-user
		# quota; xorriso then fails near the end of a build with "Disk quota
		# exceeded". Keep all large transient data on the datasets volume by
		# default, and allow an explicit fast-cache override when available.
		BUILD_CACHE="${XASH3D_GC_ISO_CACHE:-/mnt/datasets/xash3d-gc-iso-build}"
		mkdir -p "$BUILD_CACHE"
		# Keep failed/interrupted builds from accumulating indefinitely. Only
		# remove our own old generated images, and never touch retail content.
		find "$BUILD_CACHE" -maxdepth 1 -type f \
			\( -name 'xash3d-gc-dod-play-*.iso' -o -name 'xash3d-gc-dod-play-*.iso.tmp' \) \
			-mtime +2 -delete 2>/dev/null || true
		free_bytes() { df -P -B1 "$1" 2>/dev/null | awk 'NR==2 {print $4}'; }
		cache_free="$(free_bytes "$BUILD_CACHE")"
		if [[ "$cache_free" =~ ^[0-9]+$ ]] && (( cache_free < 1200000000 )); then
			echo "error: less than 1.2 GB free in ISO build cache: $BUILD_CACHE" >&2
			exit 1
		fi
		BUILD_DATA="$BUILD_CACHE/dod"
		echo "==> synchronizing DoD retail content to local build cache ($DOD_DATA_DEFAULT)"
		rsync -a --delete "$DOD_DATA_DEFAULT/" "$BUILD_DATA/"
		# DoD liblist typically falls back to valve/; stage sibling when present.
		if [[ -d "$ROOT/Half-Life/valve" ]]; then
			mkdir -p "$BUILD_CACHE/valve"
			rsync -a --delete "$ROOT/Half-Life/valve/" "$BUILD_CACHE/valve/"
		fi
		tmp_iso="$BUILD_CACHE/xash3d-gc-dod-play-$$.iso"
		trap 'rm -f "$tmp_iso"' EXIT
		# build-gamecube-disc.py stages the retail tree through tempfile too.
		# Point it at the same cache so neither staging nor xorriso can exhaust
		# the tmpfs user quota. Auto-extra picks up valve/ beside dod/.
		TMPDIR="$BUILD_CACHE" python3 "$ROOT/scripts/build-gamecube-disc.py" \
			--dol "$BOOT_DOL" --output "$tmp_iso" --data "$BUILD_DATA" \
			--gamedir dod --play $([[ $PERF == 1 ]] && echo --perf)
		if (( PUBLISH )); then
			publish_free="$(free_bytes "$(dirname "$ISO")")"
			if [[ "$publish_free" =~ ^[0-9]+$ ]] && (( publish_free < 1000000000 )); then
				echo "error: less than 1 GB free beside published ISO: $(dirname "$ISO")" >&2
				echo "completed local ISO retained at $tmp_iso; rerun with --local-cache to play it" >&2
				exit 1
			fi
			publish_tmp="$ISO.tmp.$$"
			cp --reflink=auto "$tmp_iso" "$publish_tmp"
			mv -f "$publish_tmp" "$ISO"
			trap - EXIT
		else
			ISO="$tmp_iso"
			trap - EXIT
			echo "==> using local ISO cache; no HDD publish copy"
		fi
	fi
fi
if (( BUILD_LOCK_HELD )); then
	flock -u 9
	exec 9>&-
fi

DOLPHIN="$ROOT/3rdparty/dolphin/build/Binaries/dolphin-emu"
[[ -x "$DOLPHIN" ]] || DOLPHIN="$(command -v dolphin-emu || true)"
[[ -n "$DOLPHIN" ]] || { echo "error: no Dolphin found (build 3rdparty/dolphin or install dolphin-emu)" >&2; exit 1; }

mkdir -p "$USER_DIR/Config"

# First run: carry over the controller mapping from the normal Dolphin profile.
if [[ ! -f "$USER_DIR/Config/GCPadNew.ini" ]]; then
	for src in "$HOME/.config/dolphin-emu/GCPadNew.ini" "$HOME/.local/share/dolphin-emu/Config/GCPadNew.ini"; do
		[[ -f "$src" ]] && { cp "$src" "$USER_DIR/Config/GCPadNew.ini"; break; }
	done
fi

# Dolphin's internal mute stays off; the launcher mutes Dolphin's output
# stream instead (no global hotkeys, works without window focus).
python3 - "$USER_DIR/Config/Dolphin.ini" <<'PY'
import configparser, sys
path = sys.argv[1]
cfg = configparser.ConfigParser(strict=False, interpolation=None)
cfg.optionxform = str
cfg.read(path)
for section, key, value in (
		("DSP", "Muted", "False"),
		("DSP", "Volume", cfg.get("DSP", "Volume", fallback="100")),
		# Same core as the retail Dolphin probe: the build needs MMU emulation.
		("Core", "CPUCore", "1"),
		("Core", "CPUThread", "True"),
		("Core", "DSPHLE", "True"),
		("Core", "FastDiscSpeed", "True"),
		("Core", "MMU", "True"),
		("Core", "AccurateCPUCache", "True"),
		("Core", "SIDevice0", cfg.get("Core", "SIDevice0", fallback="6")),
		("Interface", "ConfirmStop", "False")):
	if not cfg.has_section(section):
		cfg.add_section(section)
	cfg.set(section, key, value)
with open(path, "w") as f:
	cfg.write(f, space_around_delimiters=True)
PY

export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY

# Recording: Dolphin plays into a private null sink that is recorded; the
# mute window plays it on the speakers through a loopback it adds or removes,
# so the recording keeps the game's sound while muted.
REC_PY="$ROOT/scripts/playtest-record.py"
SINK_MODS=
SINK=
REC_PID=
if (( RECORD )) && command -v ffmpeg >/dev/null && command -v pactl >/dev/null; then
	SINK="xashplay$$"
	if SINK_MODS="$(python3 "$REC_PY" sink create "$SINK")"; then
		export PULSE_SINK="$SINK"
	else
		SINK_MODS=
		SINK=
	fi
fi
cleanup()
{
	[[ -n "$REC_PID" ]] && { timeout 120 tail --pid="$REC_PID" -f /dev/null 2>/dev/null || true; }
	[[ -n "$SINK_MODS" ]] && python3 "$REC_PY" sink destroy $SINK_MODS
	SINK_MODS=
}
trap cleanup EXIT

echo "==> $ISO ($([[ $START_MUTED == 1 ]] && echo muted || echo sound on)$([[ -n $SINK_MODS ]] && echo ", recording"))"
"$DOLPHIN" -u "$USER_DIR" -e "$ISO" &
DOLPHIN_PID=$!

if [[ -n "$SINK_MODS" ]]; then
	python3 "$REC_PY" record --pid "$DOLPHIN_PID" --sink "$SINK" --route play --disc "$ISO" &
	REC_PID=$!
fi

START_FLAG=$([[ $START_MUTED == 1 ]] && echo --muted || echo --unmuted)
TOGGLE_ARGS=(--pid "$DOLPHIN_PID" "$START_FLAG")
[[ -n "$SINK" ]] && TOGGLE_ARGS+=(--game-sink "$SINK")
if [[ -n "$SINK" && -n "${XASH_PLAY_LOOPBACK_SINK:-}" ]]; then
	TOGGLE_ARGS+=(--speaker-sink "$XASH_PLAY_LOOPBACK_SINK")
fi
if (( TOGGLE )); then
	[[ -n "$REC_PID" ]] && TOGGLE_ARGS+=(--rec-pid "$REC_PID")
else
	# --no-toggle hides the control window; it must still enforce the selected
	# audio state. Otherwise PulseAudio can restore Dolphin's old muted stream,
	# and recording mode leaves its private sink disconnected from the speakers.
	TOGGLE_ARGS+=(--no-window)
fi
python3 "$ROOT/scripts/dolphin-mute-toggle.py" "${TOGGLE_ARGS[@]}" &
TOGGLE_PID=$!

wait "$DOLPHIN_PID" || true
[[ -n "$TOGGLE_PID" ]] && kill "$TOGGLE_PID" 2>/dev/null || true
