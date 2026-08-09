#!/usr/bin/env bash
# Prepare a real-hardware validation handoff for the GameCube port.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${G38_LOG_DIR:-.ai/logs/hardware-handoff-$STAMP}"
MANIFEST="$LOG_DIR/artifact-manifest.tsv"
CHECKLIST="$LOG_DIR/operator-checklist.md"
EVIDENCE="$LOG_DIR/evidence-template.md"
SUMMARY="$LOG_DIR/summary.md"

BUILD=0
BUILD_DISC=0
SMOKE_MAP="${G38_SMOKE_MAP:-c0a0e}"

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-hardware-handoff.sh [--build] [--build-disc]

Creates a hardware validation handoff directory under .ai/logs/ with:
  - artifact-manifest.tsv
  - operator-checklist.md
  - evidence-template.md
  - summary.md

This script does not copy or package proprietary Half-Life assets.
EOF
}

while (( $# > 0 )); do
	case "$1" in
		--build) BUILD=1 ;;
		--build-disc) BUILD_DISC=1 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

mkdir -p "$LOG_DIR"

if (( BUILD )); then
	scripts/build-gamecube.sh >"$LOG_DIR/build-gamecube.log" 2>&1 || {
		echo "FAIL: GameCube build failed. See $LOG_DIR/build-gamecube.log" >&2
		exit 1
	}
fi

if (( BUILD_DISC )); then
	if [[ ! -d Half-Life/valve ]]; then
		echo "FAIL: --build-disc requires local legal Half-Life/valve assets." >&2
		exit 1
	fi
	scripts/build-gamecube-disc.py --smoke-map "$SMOKE_MAP" >"$LOG_DIR/build-disc.log" 2>&1 || {
		echo "FAIL: Disc image build failed. See $LOG_DIR/build-disc.log" >&2
		exit 1
	}
fi

hash_file() {
	local path="$1"
	if [[ -s "$path" ]]; then
		sha256sum "$path" | awk '{print $1}'
	else
		printf 'missing'
	fi
}

write_manifest() {
	{
		printf "path\tsize\tsha256\n"
		for path in \
			OUT/bin/boot.dol \
			OUT/bin/xash \
			OUT/bin/gamecube-handoff.txt \
			OUT/xash3d-gc.iso \
			OUT/libref_gx.a \
			OUT/libfilesystem_stdio.a \
			OUT/valve/extras.pk3
		do
			if [[ -e "$path" ]]; then
				printf "%s\t%s\t%s\n" "$path" "$(stat -c '%s' "$path")" "$(hash_file "$path")"
			else
				printf "%s\tmissing\tmissing\n" "$path"
			fi
		done
	} >"$MANIFEST"
}

COMMIT="$(git rev-parse --short HEAD)"
DIRTY=""
if ! git diff --quiet || ! git diff --cached --quiet; then
	DIRTY="-dirty"
fi

write_manifest

cat >"$CHECKLIST" <<EOF
# GameCube Hardware Operator Checklist

- Commit: \`${COMMIT}${DIRTY}\`
- Handoff directory: \`$LOG_DIR\`
- Manifest: \`$MANIFEST\`
- Smoke map: \`$SMOKE_MAP\`

## Required Files

- \`OUT/bin/boot.dol\`
- Optional smoke ISO: \`OUT/xash3d-gc.iso\`
- Legal local Half-Life assets copied by the operator, not from Git.

## SD / Loader Layout

Preferred loader: **Swiss** with a DOL built against **libogc2** + **libdvm**.
Writable volumes probed in order: \`sd:\` (SD2SP2), \`carda:\` / \`cardb:\`
(SD Gecko). Place assets on the volume you use:

\`\`\`text
sd:/apps/xash3d-gc/boot.dol          # or Swiss browser path
sd:/xash3d/valve/                    # SD2SP2
sd:/xash3d/valve/save/
# — or —
carda:/xash3d/valve/                 # SD Gecko in Slot A
carda:/xash3d/valve/save/
\`\`\`

Copy your legally owned Half-Life \`valve\` assets to \`<vol>:/xash3d/valve/\`.
Do not copy Nintendo SDK files, BIOS/IPL dumps, proprietary Nintendo docs, or
copyrighted game assets into Git or public release archives.

On quit, libogc2 returns to Swiss when the loader stub (\`STUBHAXX\`) is present.

## Dolphin baseline (already green)

Disc-only Dolphin release packet (G508 + G509 + lean audio PCM):
\`.ai/logs/operator-evidence-20260808-audio/\` → **COMPLETE / RELEASE-READY**.

Dolphin cannot satisfy \`--require-swiss\`: upstream has no SD Gecko / SD2SP2 EXI
device, so \`fatInitDefault()\` stays disc-only. Capture Swiss markers on real
hardware, then rebuild the packet:

\`\`\`sh
scripts/gamecube-operator-evidence.sh --require-swiss \\
  --reuse-probe <hardware-osreport-dir> \\
  --reuse-soak .ai/logs/soak-probe-20260808-141117 \\
  --reuse-gameplay .ai/logs/operator-evidence-20260808-audio/merge-continuity
\`\`\`

## Swiss markers for \`--require-swiss\` PASS

OSReport **or** the on-card log must include:

1. \`FAT volume ready sd:/\` (or \`carda:/\` / \`cardb:/\`)
2. \`FAT preferred volume …\`
3. \`FAT shutdown (return to Swiss loader via exit stub)\` on quit (libogc2 +
   \`STUBHAXX\`)

Easiest capture (no USB Gecko / serial needed): after quit-to-Swiss, remount
the SD and ingest:

\`\`\`sh
scripts/gamecube-swiss-evidence.sh --log /path/to/sd
# reads <vol>/xash3d/valve/logs/swiss-evidence.txt written by the DOL
\`\`\`

Preferred stack: rebuild with libogc2 + libdvm
(\`XASH_GAMECUBE_OGC_STACK=libogc2\`; this host may only have classic libogc).

Lean release DOLs stub \`-gcmap\` smoke unless rebuilt with
\`-DXASH_GAMECUBE_ENABLE_GCMAP_PROBES\` (saves several KiB for Swiss path).

## Minimum G38 Test

Retail Flipper policy: no probe argv required. Expect OSReport markers
\`REF_GX static GetRefAPI retail Flipper policy=on\` and
\`retail Flipper policy capture=0\`. Capture diagnostics only with
\`-gcdumpframes\` / \`-gcnewgame\` / etc.

1. Boot \`boot.dol\` through Swiss or another homebrew loader.
2. Record hardware model, video cable/mode, loader version, and controller type.
3. Confirm the port reaches the earliest visible marker or OSReport bootstrap.
4. Confirm whether \`$SMOKE_MAP\` reaches map load, player view, and controller input.
5. Record audio behavior: audible output, silence/null fallback, or hang.
6. Record storage behavior: SD detected, no writable storage, config/save success, or diagnostic.
7. Confirm Swiss markers above; quit back to Swiss and capture FAT shutdown.
8. Run at least five minutes if gameplay is reached.
9. Fill \`$EVIDENCE\` and copy the completed entry into \`docs/GAMECUBE_PORT_PLAN.md\`.
10. Reassemble packet with \`--require-swiss\` using the hardware log dir.

## Failure Labels

Use one label from \`docs/GAMECUBE_HARDWARE_VALIDATION.md\`, such as
\`loader_failure\`, \`no_video\`, \`filesystem_mount_failure\`,
\`asset_lookup_failure\`, \`renderer_failure\`, \`controller_failure\`,
\`audio_failure\`, \`memory_pressure\`, \`bounded_hang\`, \`unbounded_hang\`,
or \`crash\`.
EOF

cat >"$EVIDENCE" <<EOF
### Hardware validation — $(date +%Y-%m-%d) — HW-G38-001

- Tester:
- Commit: ${COMMIT}${DIRTY}
- Build command: scripts/gamecube-hardware-handoff.sh --build --build-disc
- Artifact:
- Artifact manifest: $MANIFEST
- Hardware:
- Loader:
- Loader version:
- Video route:
- Storage route:
- Asset route:
- Controller:
- Result:
- Furthest reached:
- Evidence:
- Failure label:
- Notes:
- Next blocker:
EOF

cat >"$SUMMARY" <<EOF
# GameCube Hardware Handoff

- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
- Commit: \`${COMMIT}${DIRTY}\`
- Log directory: \`$LOG_DIR\`
- Artifact manifest: \`$MANIFEST\`
- Operator checklist: \`$CHECKLIST\`
- Evidence template: \`$EVIDENCE\`

This handoff is repository-side preparation for G38. G38 remains manual until a
completed real-hardware evidence entry is recorded in the port plan. Disc-only
Dolphin automation is already RELEASE-READY
(\`.ai/logs/operator-evidence-20260808-audio/\`); Swiss FAT / return-to-loader
is the remaining hardware gate for \`--require-swiss\`.

## Artifacts

\`\`\`tsv
$(cat "$MANIFEST")
\`\`\`
EOF

cat "$SUMMARY"
