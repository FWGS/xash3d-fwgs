#!/usr/bin/env bash
# Build Xash3D FWGS for Nintendo GameCube (requires devkitPro)
# Swiss-first stack: prefers libogc2 + libdvm under $DEVKITPRO.
#
# Named DOL releases (one statically linked title per DOL):
#   HLSDK_GAMECUBE_GAMEDIR=valve  → OUT/bin/halflife1.dol (+ boot.dol alias)
#   HLSDK_GAMECUBE_GAMEDIR=dod    → OUT/bin/dodgc.dol
# Or: scripts/build-gamecube.sh --game-profile=halflife1|dod
set -e

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
# Keep compiler/configure scratch files off quota-sensitive host /tmp.  Honor
# an explicit caller choice, but make the normal build command self-contained.
if [ -z "${TMPDIR:-}" ] && [ -d /dev/shm ]; then
	export TMPDIR=/dev/shm
fi
# auto|libogc2|libogc — Swiss / Extrems libogc2 is the default preference.
export XASH_GAMECUBE_OGC_STACK="${XASH_GAMECUBE_OGC_STACK:-auto}"

ROOT="$(git rev-parse --show-toplevel)"
HLSDK_DIR="${HLSDK_PORTABLE_DIR:-$ROOT/3rdparty/hlsdk-portable}"
HLSDK_DESTDIR="${HLSDK_GAMECUBE_DESTDIR:-$ROOT/OUT/hlsdk-gamecube}"

# Profile → gamedir / release DOL name.
gamecube_normalize_profile() {
	case "$1" in
		""|valve|halflife1|hl1|hl) echo "valve" ;;
		dod|dodgc|dayofdefeat) echo "dod" ;;
		*) echo "$1" ;;
	esac
}

gamecube_dol_basename() {
	case "$1" in
		valve) echo "halflife1.dol" ;;
		dod) echo "dodgc.dol" ;;
		*) echo "${1}gc.dol" ;;
	esac
}

GAME_PROFILE="${XASH_GAMECUBE_GAME_PROFILE:-}"
FILTERED_ARGS=()
while (( $# )); do
	case "$1" in
		--game-profile=*)
			GAME_PROFILE="${1#--game-profile=}"
			shift
			;;
		--game-profile)
			GAME_PROFILE="${2:-}"
			shift 2
			;;
		--gamedir=*)
			GAME_PROFILE="${1#--gamedir=}"
			shift
			;;
		--gamedir)
			GAME_PROFILE="${2:-}"
			shift 2
			;;
		*)
			FILTERED_ARGS+=("$1")
			shift
			;;
	esac
done
set -- "${FILTERED_ARGS[@]}"

if [ -n "${HLSDK_GAMECUBE_GAMEDIR:-}" ] && [ -z "$GAME_PROFILE" ]; then
	GAME_PROFILE="$HLSDK_GAMECUBE_GAMEDIR"
fi
HLSDK_GAMEDIR="$(gamecube_normalize_profile "${GAME_PROFILE:-valve}")"
export HLSDK_GAMECUBE_GAMEDIR="$HLSDK_GAMEDIR"
DOL_NAME="$(gamecube_dol_basename "$HLSDK_GAMEDIR")"
# DoD online / dual-guest path needs libbba (eth2sp1/BBA). Default on for dod.
if [ "$HLSDK_GAMEDIR" = "dod" ]; then
	export XASH_GAMECUBE_LINK_BBA="${XASH_GAMECUBE_LINK_BBA:-1}"
	echo "GameCube BBA link: XASH_GAMECUBE_LINK_BBA=$XASH_GAMECUBE_LINK_BBA"
fi

HLSDK_SERVER_ARCHIVE="$HLSDK_DESTDIR/$HLSDK_GAMEDIR/dlls/libhl_gamecube_ppc.a"
HLSDK_EXPORTS="$HLSDK_DESTDIR/$HLSDK_GAMEDIR/dlls/gamecube_server_entity_exports.inc"
NM="${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-nm"

if command -v python3 >/dev/null 2>&1; then
	# Keep transient resolver metadata off quota-sensitive host /tmp.  The
	# resolver is read only during this invocation, so tmpfs is preferable.
	GC_STACK_METADATA="${XASH_GAMECUBE_STACK_METADATA:-${TMPDIR:-/dev/shm}/gamecube-ogc-stack.json}"
	export XASH_GAMECUBE_STACK_METADATA="$GC_STACK_METADATA"
	python3 "$ROOT/scripts/waifulib/gamecube_ogc_stack.py" > "$GC_STACK_METADATA"
	echo "GameCube OGC stack resolution:"
	python3 -c 'import json, os; d=json.load(open(os.environ["XASH_GAMECUBE_STACK_METADATA"])); print(" ", d.get("stack"), d.get("root"), "fat="+str(d.get("fat_provider"))); raise SystemExit(0 if d.get("available") else 1)' \
		|| { echo "error: GameCube OGC stack not available under $DEVKITPRO (install libogc2 for Swiss)" >&2; exit 1; }
fi

echo "GameCube build profile: gamedir=$HLSDK_GAMEDIR dol=$DOL_NAME"

# dod-gc symlinks engine→this tree and races waf cache (XASH_GAMEDIR flips
# mid-link). Serialize all GameCube engine builds on one flock.
mkdir -p "$ROOT/.ai"
BUILD_LOCK="${XASH_GAMECUBE_BUILD_LOCK:-$ROOT/.ai/build-gamecube.lock}"
exec 9>"$BUILD_LOCK"
if ! flock -n 9; then
	echo "error: another GameCube build holds $BUILD_LOCK (dod-gc/HL1 race)" >&2
	echo "  wait for it to finish, or set XASH_GAMECUBE_BUILD_LOCK to a private path" >&2
	exit 1
fi
echo "GameCube build lock: $BUILD_LOCK"

# dll_gamecube.c includes these wrappers next to the HLSDK archives; their old
# generator is gone, so install the tracked copies (fresh clones need them).
for gc_exp in dlls/gamecube_server_exports.inc cl_dlls/gamecube_client_exports.inc; do
	if [ -d "$HLSDK_DESTDIR/$HLSDK_GAMEDIR/$(dirname "$gc_exp")" ]; then
		cp -f "$ROOT/scripts/gamecube-exports/$(basename "$gc_exp")" "$HLSDK_DESTDIR/$HLSDK_GAMEDIR/$gc_exp"
	fi
done

if [ -s "$HLSDK_SERVER_ARCHIVE" ] && [ -d "$HLSDK_DIR/dlls" ] && [ -x "$NM" ]; then
	python3 "$ROOT/scripts/generate-hlsdk-gamecube-exports.py" \
		--hlsdk-dir "$HLSDK_DIR" \
		--archive "$HLSDK_SERVER_ARCHIVE" \
		--output "$HLSDK_EXPORTS"
elif [ "$HLSDK_GAMEDIR" != "valve" ] && [ ! -s "$HLSDK_SERVER_ARCHIVE" ]; then
	echo "error: missing HLSDK archives for gamedir=$HLSDK_GAMEDIR:" >&2
	echo "  expected $HLSDK_SERVER_ARCHIVE" >&2
	echo "  build with HLSDK_GAMECUBE_GAMEDIR=$HLSDK_GAMEDIR scripts/hlsdk-gamecube-build.sh" >&2
	echo "  (Day of Defeat also needs a viable client/server source — see docs/gamecube/MULTI_GAME.md)" >&2
	exit 1
fi

./waf configure --gamecube \
	-T release \
	--disable-gl --enable-gx \
	--low-memory-mode=2 \
	--disable-werror \
	--gamedir "$HLSDK_GAMEDIR" \
	"$@"

# Catch mid-flight cache poisoning (shared engine tree with dod-gc).
if ! grep -q "XASH_GAMEDIR=\"${HLSDK_GAMEDIR}\"" build/c4che/_cache.py 2>/dev/null; then
	echo "error: waf cache gamedir mismatch after configure (want ${HLSDK_GAMEDIR})" >&2
	grep -n 'XASH_GAMEDIR' build/c4che/_cache.py 2>/dev/null | head -3 >&2 || true
	exit 1
fi

# Serial -j1 avoids exFAT .conf_check_/object races on Expansion drives.
./waf build -j1

# Installation can relink the engine while updating static dependencies.
# Convert only the final installed build, otherwise the disc freshness gate
# correctly rejects a DOL made from the earlier ELF.
./waf install --destdir=OUT

mkdir -p OUT/bin
cp build/engine/xash OUT/bin/xash
# Keep a gamedir-tagged ELF beside the release DOL for multi-profile OUT trees.
cp OUT/bin/xash "OUT/bin/xash-${HLSDK_GAMEDIR}"

if command -v powerpc-eabi-strip >/dev/null 2>&1; then
	if ! powerpc-eabi-strip OUT/bin/xash; then
		echo "warning: powerpc-eabi-strip failed; keeping unstripped OUT/bin/xash" >&2
	fi
	cp OUT/bin/xash "OUT/bin/xash-${HLSDK_GAMEDIR}"
fi

if command -v python3 >/dev/null 2>&1; then
	if python3 "$ROOT/scripts/elf-to-dol.py" OUT/bin/xash "OUT/bin/$DOL_NAME"; then
		echo "DOL generated successfully: OUT/bin/$DOL_NAME"
	else
		echo "error: Python DOL generation failed" >&2
		exit 1
	fi
else
	echo "error: python3 not found; cannot generate DOL" >&2
	exit 1
fi

# HL1 keeps boot.dol as a stable Swiss/docs alias of halflife1.dol.
if [ "$HLSDK_GAMEDIR" = "valve" ]; then
	cp -f "OUT/bin/$DOL_NAME" OUT/bin/boot.dol
	echo "HL1 alias: OUT/bin/boot.dol → $DOL_NAME"
fi

# Artifact / linkage metadata for hardware handoff.
{
	echo "xash3d-gc hardware handoff"
	echo "built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host=$(uname -n 2>/dev/null || echo unknown)"
	echo "renderer=REF_GX"
	echo "policy=retail-flipper"
	echo "loader=swiss"
	echo "gamedir=$HLSDK_GAMEDIR"
	echo "dol_name=$DOL_NAME"
	if [[ -f "$XASH_GAMECUBE_STACK_METADATA" ]]; then
		python3 -c 'import json, os; d=json.load(open(os.environ["XASH_GAMECUBE_STACK_METADATA"])); print("ogc_stack=%s" % d.get("stack")); print("ogc_root=%s" % d.get("root")); print("fat_provider=%s" % d.get("fat_provider"))'
	fi
	echo "elf=OUT/bin/xash"
	echo "elf_profile=OUT/bin/xash-${HLSDK_GAMEDIR}"
	echo "dol=OUT/bin/$DOL_NAME"
	if [ "$HLSDK_GAMEDIR" = "valve" ]; then
		echo "dol_alias=OUT/bin/boot.dol"
	fi
	if [ -f OUT/bin/xash ]; then
		echo "elf_bytes=$(wc -c < OUT/bin/xash | tr -d ' ')"
	fi
	if [ -f "OUT/bin/$DOL_NAME" ]; then
		echo "dol_bytes=$(wc -c < "OUT/bin/$DOL_NAME" | tr -d ' ')"
		command -v powerpc-eabi-nm >/dev/null 2>&1 && \
			powerpc-eabi-nm OUT/bin/xash 2>/dev/null | grep -E 'GetRefAPI|GC_UseGxWorldDraw|GC_IsCaptureDiagnostics' | head -20 \
			| sed 's/^/sym /' || true
	fi
} | tee OUT/bin/gamecube-handoff.txt
echo "Handoff metadata: OUT/bin/gamecube-handoff.txt"

echo "GameCube build installed to OUT/"

# Default data path follows the profile; HL1 stays Half-Life/valve.
case "$HLSDK_GAMEDIR" in
	valve) GC_DATA_DEFAULT="Half-Life/valve" ;;
	dod) GC_DATA_DEFAULT="Half-Life/dod" ;;
	*) GC_DATA_DEFAULT="Half-Life/$HLSDK_GAMEDIR" ;;
esac
GC_DATA="${XASH3D_GC_DATA:-$GC_DATA_DEFAULT}"
GC_ISO="${XASH3D_GC_ISO:-OUT/xash3d-gc.iso}"
GC_SMOKE_MAP="${XASH3D_GC_SMOKE_MAP:-}"
GC_SKIP_DISC_BUILD="${XASH3D_GC_SKIP_DISC_BUILD:-0}"

if [ "$GC_SKIP_DISC_BUILD" = "1" ]; then
	echo "Skipping disc build (XASH3D_GC_SKIP_DISC_BUILD=1)."
	echo "For disc packaging, run: python3 scripts/build-gamecube-disc.py --output OUT/xash3d-gc.iso --data ${GC_DATA} --gamedir ${HLSDK_GAMEDIR} --dol OUT/bin/${DOL_NAME}"
	echo "For DOL testing, provide assets at sd:/xash3d/${HLSDK_GAMEDIR}/ before launching OUT/bin/${DOL_NAME}."
	exit 0
fi

if [ -d "$GC_DATA" ]; then
	DISC_ARGS=(--output "$GC_ISO" --data "$GC_DATA" --gamedir "$HLSDK_GAMEDIR" --dol "OUT/bin/$DOL_NAME")
	if [ -n "$GC_SMOKE_MAP" ]; then
		DISC_ARGS+=(--smoke-map "$GC_SMOKE_MAP")
	fi
	echo "Building GameCube disc from $GC_DATA (gamedir=$HLSDK_GAMEDIR) ..."
	if python3 "$ROOT/scripts/build-gamecube-disc.py" "${DISC_ARGS[@]}"; then
		echo "Disc image ready: $GC_ISO"
	else
		echo "Disc build failed. Engine build is still in OUT/bin/." >&2
		exit 1
	fi
else
	echo "Game data not found at $GC_DATA; skipping disc build."
	echo "Set XASH3D_GC_DATA or install retail assets, then run:"
	echo "  python3 scripts/build-gamecube-disc.py --output OUT/xash3d-gc.iso --data ${GC_DATA_DEFAULT} --gamedir ${HLSDK_GAMEDIR} --dol OUT/bin/${DOL_NAME}"
fi

echo "For DOL testing, provide assets at sd:/xash3d/${HLSDK_GAMEDIR}/ before launching OUT/bin/${DOL_NAME}."
