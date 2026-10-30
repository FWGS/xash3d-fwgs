#!/usr/bin/env bash
# Play the GameCube build in the project's Dolphin, optionally muted, with a
# big always-on-top mute toggle window. See docs/GAMECUBE_PLAY_DOLPHIN.md.
set -euo pipefail

usage()
{
	cat <<'EOF'
usage: scripts/play-dolphin.sh [options] [ISO]

Starts the GameCube disc in the project's Dolphin with a mute toggle window.

  --muted          start with Dolphin's audio muted
  --unmuted        start with sound on
  --save-default   remember --muted/--unmuted as the default
  --no-toggle      do not open the mute toggle window
  --build          (re)build the play disc first (OUT/xash3d-gc-play.iso)
  --install-desktop  add "Xash3D GameCube (Dolphin)" to the desktop app menu
  -h, --help       this help

ISO defaults to OUT/xash3d-gc-play.iso (menu New Game with the real
controller; build it with --build or scripts/build-gamecube-disc.py --play).

Mute silences Dolphin's output stream (PulseAudio/PipeWire): the game keeps
mixing audio and running at full speed, only the speakers go quiet. Toggle
it live with the big MUTED / SOUND ON window (click, Space or M there).

Defaults live in ~/.config/xash3d-gc/play.conf (START_MUTED=1|0, default 1).
Dolphin settings for this launcher (controller mapping, video) live in
~/.local/share/xash3d-gc/dolphin; map your controller once under
Controllers > Port 1 > Standard Controller and it is kept.
EOF
}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/xash3d-gc"
CONF="$CONF_DIR/play.conf"
USER_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/xash3d-gc/dolphin"
ISO="$ROOT/OUT/xash3d-gc-play.iso"
START_MUTED=1
TOGGLE=1
SAVE=0
BUILD=0

[[ -f "$CONF" ]] && source "$CONF"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--muted) START_MUTED=1 ;;
		--unmuted) START_MUTED=0 ;;
		--save-default) SAVE=1 ;;
		--no-toggle) TOGGLE=0 ;;
		--build) BUILD=1 ;;
		--install-desktop)
			apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
			mkdir -p "$apps"
			cat > "$apps/xash3d-gc-play.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Xash3D GameCube (Dolphin)
Comment=Play the GameCube build in Dolphin with a mute toggle
Exec=$ROOT/scripts/play-dolphin.sh
Terminal=false
Categories=Game;
EOF
			echo "installed $apps/xash3d-gc-play.desktop"
			exit 0 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
		*) ISO="$(realpath "$1")" ;;
	esac
	shift
done

if (( SAVE )); then
	mkdir -p "$CONF_DIR"
	printf 'START_MUTED=%d\n' "$START_MUTED" > "$CONF"
	echo "saved default: $([[ $START_MUTED == 1 ]] && echo muted || echo unmuted) ($CONF)"
fi

if (( BUILD )) || [[ ! -f "$ISO" ]]; then
	echo "==> building play disc $ISO"
	python3 "$ROOT/scripts/build-gamecube-disc.py" --output "$ISO" --data "$ROOT/Half-Life/valve" \
		--skip-startup-vids --play
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

echo "==> $ISO ($([[ $START_MUTED == 1 ]] && echo muted || echo sound on))"
"$DOLPHIN" -u "$USER_DIR" -e "$ISO" &
DOLPHIN_PID=$!

START_FLAG=$([[ $START_MUTED == 1 ]] && echo --muted || echo --unmuted)
python3 "$ROOT/scripts/dolphin-mute-toggle.py" --pid "$DOLPHIN_PID" "$START_FLAG" \
	$( (( TOGGLE )) || echo --no-window ) &
TOGGLE_PID=$!

wait "$DOLPHIN_PID" || true
[[ -n "$TOGGLE_PID" ]] && kill "$TOGGLE_PID" 2>/dev/null || true
