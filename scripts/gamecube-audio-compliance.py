#!/usr/bin/env python3
"""Generate G48 audio failure, latency, and clipping preflight evidence."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass
class Check:
	name: str
	status: str
	detail: str
	required: bool = True


def read(path: Path) -> str:
	return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def contains_all(text: str, needles: tuple[str, ...]) -> bool:
	return all(needle in text for needle in needles)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"audio-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	audio = read(root / "engine/platform/gamecube/snddma_gamecube.c")
	sound_h = read(root / "engine/client/sound.h")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")
	validation = read(root / "docs/GAMECUBE_HARDWARE_VALIDATION.md")
	goals = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")

	checks: list[Check] = []
	checks.append(Check(
		"nonfatal init fallback",
		"PASS" if contains_all(audio, ("SNDDMA_Init", "GCube_RealAudioInit", "GCube_NullAudioInit", "init failed, using null fallback", "-gcnullaudio")) else "FAIL",
		"ASND init failure and explicit -gcnullaudio both preserve a silent fallback",
	))
	checks.append(Check(
		"bounded ASND latency",
		"PASS" if contains_all(audio, ("GC_AUDIO_CHUNK_SAMPLES", "512", "GC_AUDIO_DEFAULT_SAMPLES", "2048", "SOUND_DMA_SPEED")) and contains_all(sound_h, ("#define SOUND_DMA_SPEED SOUND_48k",)) else "FAIL",
		"uses bounded 512-sample chunks and a documented 2048-sample stereo ring at 48 kHz",
	))
	checks.append(Check(
		"double-buffered playback",
		"PASS" if contains_all(audio, ("gc_audio_chunk[2]", "GC_AudioVoiceCallback", "gc_audio_play_chunk ^ 1", "ASND_AddVoice")) else "FAIL",
		"feeds ASND with double-buffered chunks from the ring buffer",
	))
	checks.append(Check(
		"wraparound-safe ring copy",
		"PASS" if contains_all(audio, ("wrapped = pos + bytes - size", "memcpy( out, ring + pos, remaining )", "memcpy( out + remaining, ring, wrapped )", "snd.samplepos = wrapped >> 1")) else "FAIL",
		"handles ring-buffer wraparound without unbounded stalls or overreads",
	))
	checks.append(Check(
		"nonzero/clipping telemetry",
		"PASS" if contains_all(audio, ("GC_AudioPeak", "gc_audio_nonzero_chunks", "gc_audio_last_peak", "audio submitted nonzero PCM", "audio still silent")) else "FAIL",
		"records peak and nonzero PCM telemetry to distinguish backend readiness from actual mixed audio",
	))
	checks.append(Check(
		"deferred active-map start",
		"PASS" if contains_all(audio, ("cls.state == ca_active", "audio voice started", "ASND_Pause( 0 )")) else "FAIL",
		"defers voice start until active gameplay to avoid boot/map-load stalls",
	))
	checks.append(Check(
		"shutdown cleanup",
		"PASS" if contains_all(audio, ("SNDDMA_Shutdown", "ASND_StopVoice", "audio shutdown chunks=", "ASND_End", "Mem_Free( snd.buffer )")) else "FAIL",
		"stops ASND, reports telemetry, and frees the ring buffer on shutdown",
	))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G48 [x]" in goals and "G48" in plan and "Audio Preflight" in validation else "FAIL",
		"goal ledger, port plan, and hardware protocol describe the G48 state",
	))
	checks.append(Check(
		"audible evidence boundary",
		"WARN",
		"Weapon, ambient, menu/error, and shutdown sounds still need dated audible Dolphin or hardware/operator evidence.",
		required=False,
	))

	failed = [check for check in checks if check.required and check.status != "PASS"]
	report.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": not failed,
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")

	with summary.open("w", encoding="utf-8") as out:
		out.write("# GameCube Audio Compliance\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			detail = check.detail.replace("|", "\\|")
			out.write(f"| {check.name} | {check.status} | {detail} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This closes the automated G48 source/policy preflight. Release-complete "
			"audio still requires dated audible evidence for weapon sound, ambient "
			"loops, menu/error sound, shutdown, and no severe clipping on Dolphin "
			"or real GameCube-compatible hardware.\n"
		)

	print(summary)
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
