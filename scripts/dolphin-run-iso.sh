#!/usr/bin/env bash
# Launch a GameCube ISO in Dolphin with OSReport logging and auto-boot.
# GUI "Open file" alone often shows a black window until you press Start.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

ISO="${1:-OUT/xash3d-gc.iso}"
if [[ ! -f "$ISO" ]]; then
	echo "ISO not found: $ISO" >&2
	echo "Usage: $0 [path/to/xash3d-gc.iso]" >&2
	exit 1
fi

ISO="$(readlink -f "$ISO")"
USER_DIR="${DOLPHIN_USER_DIR:-$ROOT/.ai/dolphin-user-run}"
DOLPHIN_MMU="${DOLPHIN_MMU:-True}"
DOLPHIN_VIDEO="${DOLPHIN_VIDEO:-OGL}"
mkdir -p "$USER_DIR/Config"

cat > "$USER_DIR/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 0
CPUThread = False
DSPHLE = True
FastDiscSpeed = True
MMU = ${DOLPHIN_MMU}
AccurateCPUCache = ${DOLPHIN_MMU}
SIDevice0 = 6
SIDevice1 = 0
SIDevice2 = 0
SIDevice3 = 0
[Audio]
Enable = True
Volume = 100
[Interface]
ConfirmStop = False
EOF

cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
CORE = True
OSREPORT = True
OSREPORT_HLE = True
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = True
EOF

DOLPHIN_CMD=()
if [[ "${DOLPHIN_EXECUTABLE:-}" == flatpak:* ]]; then
	ID="${DOLPHIN_EXECUTABLE#flatpak:}"
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "$ID")
elif [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
	DOLPHIN_CMD=("$DOLPHIN_EXECUTABLE")
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin-emu)
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin)
elif command -v flatpak >/dev/null 2>&1 && \
	flatpak info "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1; then
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}")
else
	echo "Dolphin not found. Install dolphin-emu or set DOLPHIN_EXECUTABLE." >&2
	exit 2
fi

echo "Launching $ISO"
echo "  Profile: $USER_DIR"
echo "  Video backend: $DOLPHIN_VIDEO (set DOLPHIN_VIDEO=Null for headless probes)"
echo "  Look for: View -> Log -> OSReport  (or Log Window with OSREPORT enabled)"
echo "  Expected guest line: Xash3D GameCube: bootstrap"
echo "  Expected flow: intro AVI (~10 s) then Half-Life main menu"
if [[ "${DOLPHIN_SKIP_BUILD:-0}" != "1" ]]; then
	echo "  Building engine + ISO (set DOLPHIN_SKIP_BUILD=1 to skip)..."
	bash "$ROOT/scripts/build-gamecube.sh"
	ISO="${ISO:-OUT/xash3d-gc.iso}"
	ISO="$(readlink -f "$ISO")"
fi

exec "${DOLPHIN_CMD[@]}" -u "$USER_DIR" -b -e "$ISO" -v "$DOLPHIN_VIDEO"
