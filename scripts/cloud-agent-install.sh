#!/usr/bin/env bash
# Idempotent Cloud Agent environment setup for xash3d-gc (Xash3D FWGS GameCube port).
#
# Prepares everything needed to build the flagship GameCube target end to end:
#   * host build toolchain + Python
#   * devkitPPC/libogc GameCube cross toolchain in /opt/devkitpro
#   * the git submodules the GameCube build links against (excludes the very
#     large 3rdparty/dolphin emulator submodule, which is only needed to *run*
#     the DOL, not to build it)
#   * the open-source Half-Life SDK game logic (hlsdk-portable) compiled for PPC
#
# After this runs, build the console image with:
#   XASH3D_GC_SKIP_DISC_BUILD=1 bash scripts/build-gamecube.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPRO

log() { printf '\n==> %s\n' "$*"; }

# ---------------------------------------------------------------------------
# 1. Host build dependencies (idempotent; apt-get install is a no-op if present)
# ---------------------------------------------------------------------------
log "Installing host build dependencies"
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -y
sudo apt-get install -y --no-install-recommends \
	build-essential gcc g++ python3 pkg-config git ca-certificates curl wget \
	libsdl2-dev libfreetype-dev libopus-dev libbz2-dev libvorbis-dev libopusfile-dev libogg-dev

# ---------------------------------------------------------------------------
# 2. devkitPPC + libogc GameCube toolchain
# ---------------------------------------------------------------------------
if [ -x "$DEVKITPRO/devkitPPC/bin/powerpc-eabi-gcc" ]; then
	log "devkitPPC already present: $("$DEVKITPRO/devkitPPC/bin/powerpc-eabi-gcc" --version | head -1)"
else
	log "Installing devkitPPC/libogc toolchain"
	bash "$REPO_ROOT/scripts/cloud-agent-install-devkitpro.sh"
fi

# Persist toolchain environment for every interactive shell / build.
sudo tee /etc/profile.d/devkitpro.sh >/dev/null <<'EOF'
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export DEVKITARM=/opt/devkitpro/devkitARM
case ":$PATH:" in
	*":/opt/devkitpro/devkitPPC/bin:"*) ;;
	*) export PATH="/opt/devkitpro/devkitPPC/bin:$PATH" ;;
esac
EOF

export DEVKITPPC="$DEVKITPRO/devkitPPC"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPPC/bin:$PATH"

# ---------------------------------------------------------------------------
# 3. Git submodules needed for the GameCube build
# ---------------------------------------------------------------------------
# 3rdparty/dolphin is intentionally excluded (multi-GB emulator source only
# required to *run* the DOL, not to build it).
GC_SUBMODULES=(
	3rdparty/libbacktrace/libbacktrace
	3rdparty/library_suffix
	3rdparty/mbedtls/mbedtls
	3rdparty/extras/xash-extras
	3rdparty/MultiEmulator
	3rdparty/vgui_support
	3rdparty/opus/opus
	3rdparty/libogg/libogg
	3rdparty/vorbis/vorbis-src
	3rdparty/opusfile/opusfile
	3rdparty/bzip2/bzip2
	3rdparty/mainui
)

# Some checkouts leave a submodule's gitlink populated but its worktree empty;
# force a checkout so the sources are actually present.
populate() {
	local path="$1"
	if [ -d "$path" ] && [ -z "$(ls -A "$path" 2>/dev/null | grep -v '^\.git$' || true)" ]; then
		git -C "$path" checkout -f HEAD >/dev/null 2>&1 || true
	fi
}

log "Initializing GameCube build submodules"
for sm in "${GC_SUBMODULES[@]}"; do
	if ! git submodule update --init "$sm" >/dev/null 2>&1; then
		echo "note: recorded commit for $sm unavailable, will reconcile below"
	fi
	populate "$sm"
done

# mainui: the superproject may pin a commit that no longer exists upstream.
# The GameCube-aware menu lives on the 'gamecube-probe-sync' branch, whose
# build.h recognises __GAMECUBE__ (master does not and #errors out).
if [ ! -f "3rdparty/mainui/sdk_includes/public/build.h" ] || \
	! grep -q "__GAMECUBE__" 3rdparty/mainui/sdk_includes/public/build.h 2>/dev/null; then
	log "Reconciling mainui to GameCube-aware revision"
	git -C 3rdparty/mainui fetch --depth 1 origin gamecube-probe-sync
	git -C 3rdparty/mainui checkout -f FETCH_HEAD
fi
# mainui's nested miniutl submodule
git -C 3rdparty/mainui submodule update --init miniutl >/dev/null 2>&1 || true
populate 3rdparty/mainui/miniutl

# ---------------------------------------------------------------------------
# 4. Half-Life SDK game logic (hlsdk-portable) built for GameCube
# ---------------------------------------------------------------------------
HLSDK_DIR="${HLSDK_PORTABLE_DIR:-$REPO_ROOT/3rdparty/hlsdk-portable}"
SERVER_ARCHIVE="$REPO_ROOT/OUT/hlsdk-gamecube/valve/dlls/libhl_gamecube_ppc.a"

if [ ! -d "$HLSDK_DIR/.git" ]; then
	log "Cloning hlsdk-portable (mobile_hacks)"
	git clone --depth 1 -b mobile_hacks https://github.com/FWGS/hlsdk-portable "$HLSDK_DIR"
fi

log "Applying GameCube hooks to hlsdk-portable (idempotent)"
python3 "$REPO_ROOT/scripts/hlsdk-gamecube-apply-patch.py"

if [ -s "$SERVER_ARCHIVE" ]; then
	log "HLSDK GameCube archives already built"
else
	log "Building HLSDK GameCube archives"
	bash "$REPO_ROOT/scripts/hlsdk-gamecube-build.sh"
fi

log "Environment ready. Build the console image with:"
echo "    XASH3D_GC_SKIP_DISC_BUILD=1 bash scripts/build-gamecube.sh"
