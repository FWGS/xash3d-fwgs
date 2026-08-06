#!/usr/bin/env bash
# Build Xash3D FWGS for Nintendo GameCube (requires devkitPro)
# Swiss-first stack: prefers libogc2 + libdvm under $DEVKITPRO.
set -e

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
# auto|libogc2|libogc — Swiss / Extrems libogc2 is the default preference.
export XASH_GAMECUBE_OGC_STACK="${XASH_GAMECUBE_OGC_STACK:-auto}"

ROOT="$(git rev-parse --show-toplevel)"
HLSDK_DIR="${HLSDK_PORTABLE_DIR:-$ROOT/3rdparty/hlsdk-portable}"
HLSDK_DESTDIR="${HLSDK_GAMECUBE_DESTDIR:-$ROOT/OUT/hlsdk-gamecube}"
HLSDK_GAMEDIR="${HLSDK_GAMECUBE_GAMEDIR:-valve}"
HLSDK_SERVER_ARCHIVE="$HLSDK_DESTDIR/$HLSDK_GAMEDIR/dlls/libhl_gamecube_ppc.a"
HLSDK_EXPORTS="$HLSDK_DESTDIR/$HLSDK_GAMEDIR/dlls/gamecube_server_entity_exports.inc"
NM="${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-nm"

if command -v python3 >/dev/null 2>&1; then
	python3 "$ROOT/scripts/waifulib/gamecube_ogc_stack.py" > /tmp/gamecube-ogc-stack.json
	echo "GameCube OGC stack resolution:"
	python3 -c 'import json; d=json.load(open("/tmp/gamecube-ogc-stack.json")); print(" ", d.get("stack"), d.get("root"), "fat="+str(d.get("fat_provider"))); raise SystemExit(0 if d.get("available") else 1)' \
		|| { echo "error: GameCube OGC stack not available under $DEVKITPRO (install libogc2 for Swiss)" >&2; exit 1; }
fi

if [ -s "$HLSDK_SERVER_ARCHIVE" ] && [ -d "$HLSDK_DIR/dlls" ] && [ -x "$NM" ]; then
	python3 "$ROOT/scripts/generate-hlsdk-gamecube-exports.py" \
		--hlsdk-dir "$HLSDK_DIR" \
		--archive "$HLSDK_SERVER_ARCHIVE" \
		--output "$HLSDK_EXPORTS"
fi

./waf configure --gamecube \
	-T release \
	--disable-gl --disable-soft --enable-gx \
	--low-memory-mode=2 \
	--disable-werror \
	"$@"

./waf build

mkdir -p OUT/bin
cp build/engine/xash OUT/bin/xash

if command -v powerpc-eabi-strip >/dev/null 2>&1; then
	if ! powerpc-eabi-strip OUT/bin/xash; then
		echo "warning: powerpc-eabi-strip failed; keeping unstripped OUT/bin/xash" >&2
	fi
fi

if command -v python3 >/dev/null 2>&1; then
	if python3 "$ROOT/scripts/elf-to-dol.py" OUT/bin/xash OUT/bin/boot.dol; then
		echo "DOL generated successfully using Python script"
	else
		echo "error: Python DOL generation failed" >&2
		exit 1
	fi
else
	echo "error: python3 not found; cannot generate DOL" >&2
	exit 1
fi

# Artifact / linkage metadata for hardware handoff.
{
	echo "xash3d-gc hardware handoff"
	echo "built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host=$(uname -n 2>/dev/null || echo unknown)"
	echo "renderer=REF_GX"
	echo "policy=retail-flipper"
	echo "loader=swiss"
	if [[ -f /tmp/gamecube-ogc-stack.json ]]; then
		python3 -c 'import json; d=json.load(open("/tmp/gamecube-ogc-stack.json")); print("ogc_stack=%s" % d.get("stack")); print("ogc_root=%s" % d.get("root")); print("fat_provider=%s" % d.get("fat_provider"))'
	fi
	echo "elf=OUT/bin/xash"
	echo "dol=OUT/bin/boot.dol"
	if [ -f OUT/bin/xash ]; then
		echo "elf_bytes=$(wc -c < OUT/bin/xash | tr -d ' ')"
	fi
	if [ -f OUT/bin/boot.dol ]; then
		echo "dol_bytes=$(wc -c < OUT/bin/boot.dol | tr -d ' ')"
		command -v powerpc-eabi-nm >/dev/null 2>&1 && \
			powerpc-eabi-nm OUT/bin/xash 2>/dev/null | grep -E 'GetRefAPI|GC_UseGxWorldDraw|GC_IsCaptureDiagnostics' | head -20 \
			| sed 's/^/sym /' || true
	fi
} | tee OUT/bin/gamecube-handoff.txt
echo "Handoff metadata: OUT/bin/gamecube-handoff.txt"

./waf install --destdir=OUT

echo "GameCube build installed to OUT/"

GC_DATA="${XASH3D_GC_DATA:-Half-Life/valve}"
GC_ISO="${XASH3D_GC_ISO:-OUT/xash3d-gc.iso}"
GC_SMOKE_MAP="${XASH3D_GC_SMOKE_MAP:-}"
GC_SKIP_DISC_BUILD="${XASH3D_GC_SKIP_DISC_BUILD:-0}"

if [ "$GC_SKIP_DISC_BUILD" = "1" ]; then
	echo "Skipping disc build (XASH3D_GC_SKIP_DISC_BUILD=1)."
	echo "For disc packaging, run: python3 scripts/build-gamecube-disc.py --output OUT/xash3d-gc.iso --data ${GC_DATA}"
	echo "For DOL testing, provide Half-Life assets at sd:/xash3d/valve/ before launching OUT/bin/boot.dol."
	exit 0
fi

if [ -d "$GC_DATA" ]; then
	DISC_ARGS=(--output "$GC_ISO" --data "$GC_DATA")
	if [ -n "$GC_SMOKE_MAP" ]; then
		DISC_ARGS+=(--smoke-map "$GC_SMOKE_MAP")
	fi
	echo "Building GameCube disc from $GC_DATA ..."
	if python3 "$ROOT/scripts/build-gamecube-disc.py" "${DISC_ARGS[@]}"; then
		echo "Disc image ready: $GC_ISO"
	else
		echo "Disc build failed. Engine build is still in OUT/bin/." >&2
		exit 1
	fi
else
	echo "Half-Life data not found at $GC_DATA; skipping disc build."
	echo "Set XASH3D_GC_DATA or install retail assets, then run:"
	echo "  python3 scripts/build-gamecube-disc.py --output OUT/xash3d-gc.iso --data Half-Life/valve"
fi

echo "For DOL testing, provide Half-Life assets at sd:/xash3d/valve/ before launching OUT/bin/boot.dol."
