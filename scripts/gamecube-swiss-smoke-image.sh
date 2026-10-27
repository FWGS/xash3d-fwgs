#!/usr/bin/env bash
# Build a flashable FAT32 Swiss smoke image (boot.dol + smoke-map valve subset).
# Default map c0a0e (MAP_READY on Dolphin after G334 tram essentials + G381).
# Operator can dd/cp the image onto an SD card, or mount it and rsync the tree.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

SMOKE_MAP="${G38_SMOKE_MAP:-c0a0e}"
STAGE_DIR="${SWISS_SMOKE_STAGE:-.ai/logs/swiss-smoke-stage}"
IMG_OUT="${SWISS_SMOKE_IMG:-OUT/xash3d-gc-swiss-smoke.img}"
IMG_MIB="${SWISS_SMOKE_IMG_MIB:-}"
VENV="${SWISS_SMOKE_VENV:-.ai/venv-fat}"
WITH_ISO="${SWISS_SMOKE_WITH_ISO:-1}"
ISO_SRC="${SWISS_SMOKE_ISO:-}"

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-swiss-smoke-image.sh [--stage-only] [--image-only] [--with-iso|--no-iso]

Creates:
  1. Host Swiss layout under .ai/logs/swiss-smoke-stage/ (or SWISS_SMOKE_STAGE)
     with apps/xash3d-gc/boot.dol + xash3d/valve/ (smoke map + gfx.wad)
  2. FAT32 disk image OUT/xash3d-gc-swiss-smoke.img (or SWISS_SMOKE_IMG)
  3. With --with-iso (default): also places a GameCube ISO on the card root
     so Swiss can launch the disc-image route without a second medium.
     Prefer compact smoke ISO via SWISS_SMOKE_ISO=OUT/xash3d-gc-swiss-disc.iso

This layout avoids the prior hardware hang where Swiss SD had boot.dol but no
valve/ (engine spent hours probing missing gfx.wad with a black screen).

Flash to a real SD (destroys existing contents on that device):
  sudo dd if=OUT/xash3d-gc-swiss-smoke.img of=/dev/sdX bs=4M status=progress conv=fsync

Or mount the card and copy the stage tree without wiping:
  rsync -a .ai/logs/swiss-smoke-stage/ /path/to/sd/
EOF
}

STAGE_ONLY=0
IMAGE_ONLY=0
while (( $# > 0 )); do
	case "$1" in
		--stage-only) STAGE_ONLY=1 ;;
		--image-only) IMAGE_ONLY=1 ;;
		--with-iso) WITH_ISO=1 ;;
		--no-iso) WITH_ISO=0 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

if [[ -z "$IMG_MIB" ]]; then
	if [[ "$WITH_ISO" == "1" ]]; then
		IMG_MIB=256
	else
		IMG_MIB=128
	fi
fi

if [[ ! -f OUT/bin/boot.dol ]]; then
	echo "FAIL: OUT/bin/boot.dol missing — run scripts/build-gamecube.sh first" >&2
	exit 1
fi
if [[ ! -d Half-Life/valve ]]; then
	echo "FAIL: Half-Life/valve missing — legal local assets required" >&2
	exit 1
fi

ensure_venv() {
	if [[ ! -x "$VENV/bin/python" ]]; then
		python3 -m venv "$VENV"
		"$VENV/bin/pip" install -q 'setuptools<81' 'pyfatfs>=1.0.0' 'fs>=2.4'
	fi
}

build_stage() {
	echo "==> Staging Swiss smoke tree ($SMOKE_MAP) → $STAGE_DIR"
	rm -rf "$STAGE_DIR"
	mkdir -p "$STAGE_DIR/apps/xash3d-gc" \
		"$STAGE_DIR/xash3d/valve/save" \
		"$STAGE_DIR/xash3d/valve/logs" \
		"$STAGE_DIR/xash3d/valve/screenshots"
	cp -f OUT/bin/boot.dol "$STAGE_DIR/apps/xash3d-gc/boot.dol"
	cat >"$STAGE_DIR/apps/xash3d-gc/meta.xml" <<META
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
  <name>Xash3D GC</name>
  <coder>xash3d-gc</coder>
  <version>0.1-swiss-smoke</version>
  <release_date>$(date +%Y%m%d)</release_date>
  <short_description>Half-Life engine Swiss smoke ($SMOKE_MAP)</short_description>
  <long_description>Boot apps/xash3d-gc/boot.dol. Auto-loads $SMOKE_MAP then returns to Swiss and writes xash3d/valve/logs/swiss-evidence.txt. Z+Start also quits.</long_description>
</app>
META
	python3 - <<PY
from pathlib import Path
import importlib.util, sys
root = Path("$ROOT")
spec = importlib.util.spec_from_file_location(
	"gamecube_disc", root / "scripts/build-gamecube-disc.py")
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)
src = root / "Half-Life" / "valve"
dst = root / "$STAGE_DIR" / "xash3d" / "valve"
staged = mod.stage_smoke_data(src, dst, "$SMOKE_MAP")
errs = mod.validate_smoke_assets(staged, "$SMOKE_MAP")
if errs:
	raise SystemExit("smoke validation failed: " + "; ".join(errs))
print("staged", staged)
# Ensure Swiss HW auto-quit token is present (stage_smoke_data may omit it).
cfg = dst / "gamecube.cfg"
cfg.write_text("map $SMOKE_MAP\nswissquit\n", encoding="ascii")
print("wrote", cfg, "swissquit")
PY
	if [[ -f OUT/valve/extras.pk3 ]]; then
		cp -f OUT/valve/extras.pk3 "$STAGE_DIR/xash3d/valve/extras.pk3"
	fi
	# Optional Swiss loader (fresh card / Action Replay convenience). Prefer a
	# cached release DOL so operators can boot Swiss then apps/xash3d-gc.
	SWISS_DOL_SRC="${SWISS_DOL:-}"
	if [[ -z "$SWISS_DOL_SRC" ]]; then
		for cand in \
			.ai/cache/swiss/extract/swiss_r2092/DOL/swiss_r2092.dol \
			.ai/cache/swiss/swiss_r2092.dol \
			.ai/cache/swiss/swiss.dol
		do
			if [[ -f "$cand" ]]; then
				SWISS_DOL_SRC="$cand"
				break
			fi
		done
	fi
	if [[ -n "$SWISS_DOL_SRC" && -f "$SWISS_DOL_SRC" ]]; then
		mkdir -p "$STAGE_DIR/apps/swiss"
		cp -f "$SWISS_DOL_SRC" "$STAGE_DIR/swiss.dol"
		cp -f "$SWISS_DOL_SRC" "$STAGE_DIR/apps/swiss/boot.dol"
		# Action Replay auto-boot name (optional; operators may already have one).
		if [[ -f .ai/cache/swiss/extract/swiss_r2092/ActionReplay/AUTOEXEC.DOL ]]; then
			cp -f .ai/cache/swiss/extract/swiss_r2092/ActionReplay/AUTOEXEC.DOL \
				"$STAGE_DIR/AUTOEXEC.DOL"
		fi
		echo "staged Swiss loader from $SWISS_DOL_SRC"
	else
		echo "NOTE: no Swiss DOL in .ai/cache/swiss — stage is Xash-only (loader assumed on card)"
	fi
	if [[ "$WITH_ISO" == "1" ]]; then
		if [[ -z "$ISO_SRC" ]]; then
			if [[ -f OUT/xash3d-gc-swiss-disc.iso ]]; then
				ISO_SRC=OUT/xash3d-gc-swiss-disc.iso
			else
				ISO_SRC=OUT/xash3d-gc.iso
			fi
		fi
		if [[ ! -f "$ISO_SRC" ]]; then
			echo "FAIL: $ISO_SRC missing — build disc or pass --no-iso" >&2
			exit 1
		fi
		iso_bytes="$(stat -c%s "$ISO_SRC")"
		# Leave headroom for valve + DOL on the FAT image (IMG_MIB defaults below).
		img_cap_mib="${IMG_MIB:-256}"
		# Rough: ISO must fit with ~100MiB spare for valve/DOL on default cards.
		if (( iso_bytes > (img_cap_mib - 100) * 1024 * 1024 )); then
			echo "WARN: $ISO_SRC is $((iso_bytes/1024/1024))MiB — too large for ${img_cap_mib}MiB smoke FAT; staging DOL+valve only" >&2
			WITH_ISO=0
			# Shrink default image when ISO was the reason for 256MiB.
			if [[ -z "${SWISS_SMOKE_IMG_MIB:-}" && "$IMG_MIB" == "256" ]]; then
				IMG_MIB=128
			fi
		else
			cp -f "$ISO_SRC" "$STAGE_DIR/xash3d-gc.iso"
			echo "staged ISO from $ISO_SRC ($(du -h "$STAGE_DIR/xash3d-gc.iso" | awk '{print $1}'))"
		fi
	fi
	cat >"$STAGE_DIR/XASH3D-GC-HW.txt" <<EOF
Xash3D GameCube Swiss smoke media
=================================
boot.dol sha256: $(sha256sum OUT/bin/boot.dol | awk '{print $1}')
smoke map: $SMOKE_MAP
routes:
  A) Swiss → apps/xash3d-gc/boot.dol  (FAT valve at xash3d/valve/)
  B) Swiss → xash3d-gc.iso            (disc-only; no FAT valve required)

Expect dark-blue splash within ~1s, then auto map load. With swissquit the DOL
returns to Swiss after playable markers (~6s). Z+Start also quits.

If swiss.dol / apps/swiss/boot.dol is present, boot that first (or AUTOEXEC.DOL
via Action Replay), then launch apps/xash3d-gc/boot.dol.

After remount on PC:
  scripts/gamecube-swiss-hw-complete.sh --log <sd-mount>
EOF
	# Engine probes sd:/xash3d/valve/gfx.wad — refuse incomplete stages.
	if [[ ! -f "$STAGE_DIR/xash3d/valve/gfx.wad" || ! -f "$STAGE_DIR/xash3d/valve/liblist.gam" ]]; then
		echo "FAIL: smoke stage missing gfx.wad/liblist.gam (would black-hang on hardware)" >&2
		exit 1
	fi
	{
		sha256sum "$STAGE_DIR/apps/xash3d-gc/boot.dol"
		du -sh "$STAGE_DIR" "$STAGE_DIR/xash3d/valve"
		find "$STAGE_DIR" -type f | wc -l
		printf 'READY: rsync -a %s/ <sd-mount>/\n' "$STAGE_DIR"
	} | tee "$STAGE_DIR/MANIFEST.txt"
}

build_image() {
	ensure_venv
	if [[ ! -d "$STAGE_DIR/apps/xash3d-gc" ]]; then
		echo "FAIL: stage missing — run without --image-only first" >&2
		exit 1
	fi
	if [[ "$WITH_ISO" == "1" && ! -f "$STAGE_DIR/xash3d-gc.iso" ]]; then
		if [[ ! -f OUT/xash3d-gc.iso ]]; then
			echo "FAIL: OUT/xash3d-gc.iso missing — build disc or pass --no-iso" >&2
			exit 1
		fi
		cp -f OUT/xash3d-gc.iso "$STAGE_DIR/xash3d-gc.iso"
	fi
	if [[ ! -f "$STAGE_DIR/xash3d/valve/gfx.wad" ]]; then
		echo "FAIL: stage missing xash3d/valve/gfx.wad (would black-hang on hardware)" >&2
		exit 1
	fi
	echo "==> Building FAT32 image ${IMG_MIB}MiB → $IMG_OUT"
	mkdir -p "$(dirname "$IMG_OUT")"
	rm -f "$IMG_OUT"
	truncate -s "${IMG_MIB}M" "$IMG_OUT"
	# XASHSMOKE: distinct from live Swiss cards; host tools still accept real
	# XASH3DGC remounts after older dd images while skipping loop verify mounts.
	mkfs.vfat -F 32 -n XASHSMOKE "$IMG_OUT" >/dev/null
	"$VENV/bin/python" - <<PY
from pathlib import Path
from pyfatfs.PyFatFS import PyFatFS

root = Path("$ROOT")
stage = root / "$STAGE_DIR"
img = root / "$IMG_OUT"
fs = PyFatFS(str(img), read_only=False, preserve_case=True)

def ensure_dir(path: str) -> None:
	parts = [p for p in path.strip("/").split("/") if p]
	cur = ""
	for part in parts:
		cur = f"{cur}/{part}" if cur else part
		if not fs.exists(cur):
			fs.makedir(cur)

copied = 0
for path in stage.rglob("*"):
	if path.name == "MANIFEST.txt":
		continue
	rel = path.relative_to(stage).as_posix()
	if path.is_dir():
		ensure_dir(rel)
		continue
	parent = path.parent.relative_to(stage).as_posix()
	if parent != ".":
		ensure_dir(parent)
	with fs.open(rel, "wb") as out:
		out.write(path.read_bytes())
	copied += 1
fs.close()
print(f"populated files={copied}")
PY
	# Verify critical paths
	"$VENV/bin/python" - <<PY
from pyfatfs.PyFatFS import PyFatFS
fs = PyFatFS("$IMG_OUT", read_only=True)
need = [
	"apps/xash3d-gc/boot.dol",
	"xash3d/valve/gfx.wad",
	"xash3d/valve/liblist.gam",
	"xash3d/valve/maps/$SMOKE_MAP.bsp",
	"xash3d/valve/logs",
]
with_iso = "$WITH_ISO" == "1"
if with_iso:
	need.append("xash3d-gc.iso")
missing = [p for p in need if not fs.exists(p)]
fs.close()
if missing:
	raise SystemExit("image missing paths: " + ", ".join(missing))
print("verify OK:", ", ".join(need))
PY
	sha256sum "$IMG_OUT"
	ls -lh "$IMG_OUT"
	echo "Flash: sudo dd if=$IMG_OUT of=/dev/sdX bs=4M status=progress conv=fsync"
}

if (( IMAGE_ONLY == 0 )); then
	build_stage
fi
if (( STAGE_ONLY == 0 )); then
	build_image
fi
