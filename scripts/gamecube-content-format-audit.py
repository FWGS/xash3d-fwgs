#!/usr/bin/env python3
"""Audit native GoldSrc content-format coverage for the GameCube port."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass(frozen=True)
class FormatSpec:
	name: str
	extensions: tuple[str, ...]
	required: bool
	loader_sources: tuple[str, ...]
	notes: str


@dataclass
class FormatResult:
	name: str
	status: str
	extensions: list[str]
	count: int
	largest_asset: str
	largest_bytes: int
	loader_sources: list[str]
	detail: str
	required: bool


SPECS = (
	FormatSpec("BSP maps", (".bsp",), True, ("engine/common/mod_bmodel.c",),
		"GoldSrc BSP version 30 map/world data."),
	FormatSpec("WAD textures", (".wad",), True, ("filesystem/wad.c", "engine/common/imagelib/img_wad.c"),
		"WAD2/WAD3 texture packages, including world texture dependencies."),
	FormatSpec("PAK archives", (".pak",), True, ("filesystem/pak.c",),
		"Legacy PACK archives; Steam-style local trees may have no loose samples."),
	FormatSpec("Studio models", (".mdl",), True, ("engine/common/mod_studio.c", "ref/gx/r_studio.c"),
		"GoldSrc studio models used by players, NPCs, weapons, and props."),
	FormatSpec("Sprites", (".spr",), True, ("engine/common/mod_sprite.c", "ref/gx/r_sprite.c"),
		"HUD, particle, beam, and map sprites."),
	FormatSpec("Wave audio", (".wav",), True, ("engine/client/sound/s_load.c", "engine/client/soundlib/snd_wav.c"),
		"PCM/ADPCM WAV sound effects, voice, and ambient loops."),
	FormatSpec("Images", (".tga", ".bmp"), True, (
		"engine/common/imagelib/img_tga.c",
		"engine/common/imagelib/img_bmp.c",
		"ref/gx/r_image.c",
	), "TGA/BMP UI, skybox, decal, and texture source images."),
	FormatSpec("Sentences and titles", (".txt",), True, (
		"engine/client/titles.c",
		"engine/common/sounds.c",
	), "titles.txt, sentences.txt, localization, HUD, and plain data files."),
	FormatSpec("Config and scripts", (".cfg", ".rc", ".lst", ".gam", ".scr", ".res"), True, (
		"engine/common/cfgscript.c",
		"engine/common/cmd.c",
		"engine/common/filesystem_engine.c",
	), "Config, startup, mod metadata, resource, and list scripts."),
	FormatSpec("Intro/media video", (".avi", ".webm", ".mp3"), False, (
		"engine/client/cl_video.c",
		"engine/client/avi/avi_gc.c",
		"engine/client/avi/avi_cinepak.c",
		"engine/client/avi/avi_ffmpeg.c",
	), "Media playback is tracked separately from the core G67 asset-loader gate."),
)


def rel(path: Path, root: Path) -> str:
	try:
		return path.relative_to(root).as_posix()
	except ValueError:
		return path.as_posix()


def read_head(path: Path, size: int = 64) -> bytes:
	with path.open("rb") as handle:
		return handle.read(size)


def classify_sample(spec: FormatSpec, path: Path) -> tuple[str, str]:
	try:
		head = read_head(path)
	except OSError as exc:
		return "FAIL", f"cannot read sample: {exc}"
	ext = path.suffix.lower()
	if ext == ".bsp":
		if len(head) < 4:
			return "FAIL", "BSP header too short"
		version = struct.unpack("<i", head[:4])[0]
		return ("PASS", "GoldSrc BSP version 30") if version == 30 else (
			"FAIL", f"unexpected BSP version {version}")
	if ext == ".wad":
		return ("PASS", head[:4].decode("ascii", "replace")) if head[:4] in {b"WAD2", b"WAD3"} else (
			"FAIL", f"unexpected WAD magic {head[:4]!r}")
	if ext == ".pak":
		return ("PASS", "PACK archive") if head[:4] == b"PACK" else (
			"FAIL", f"unexpected PAK magic {head[:4]!r}")
	if ext == ".mdl":
		return ("PASS", "IDST studio model") if head[:4] == b"IDST" else (
			"FAIL", f"unexpected MDL magic {head[:4]!r}")
	if ext == ".spr":
		return ("PASS", "IDSP sprite") if head[:4] == b"IDSP" else (
			"FAIL", f"unexpected SPR magic {head[:4]!r}")
	if ext == ".wav":
		ok = len(head) >= 12 and head[:4] == b"RIFF" and head[8:12] == b"WAVE"
		return ("PASS", "RIFF/WAVE audio") if ok else (
			"FAIL", f"unexpected WAV header {head[:12]!r}")
	if ext == ".bmp":
		return ("PASS", "BMP image") if head[:2] == b"BM" else (
			"FAIL", f"unexpected BMP magic {head[:2]!r}")
	if ext == ".tga":
		return ("PASS", "TGA header present") if len(head) >= 18 else (
			"FAIL", "TGA header too short")
	if ext in {".txt", ".cfg", ".rc", ".lst", ".gam", ".scr", ".res"}:
		if b"\x00" in head:
			return "FAIL", "text/script sample contains NUL bytes in header"
		return "PASS", "plain text/script sample"
	if ext in {".avi", ".webm", ".mp3"}:
		return "WARN", "media sample recorded; native media playback is a separate gate"
	return "WARN", "no magic validator for this extension"


def find_assets(content_root: Path, extensions: tuple[str, ...]) -> list[Path]:
	exts = {ext.lower() for ext in extensions}
	return sorted(
		(path for path in content_root.rglob("*")
			if path.is_file() and path.suffix.lower() in exts),
		key=lambda path: path.as_posix().lower(),
	)


def source_status(root: Path, spec: FormatSpec) -> tuple[bool, str]:
	missing = [source for source in spec.loader_sources if not (root / source).is_file()]
	if missing:
		return False, "missing loader source: " + ", ".join(missing)
	return True, "loader source present"


def audit_format(root: Path, content_root: Path, spec: FormatSpec) -> FormatResult:
	sources_ok, source_detail = source_status(root, spec)
	assets = find_assets(content_root, spec.extensions) if content_root.is_dir() else []
	largest = max(assets, key=lambda path: path.stat().st_size, default=None)
	details: list[str] = [source_detail, spec.notes]
	status = "PASS" if sources_ok else "FAIL"
	if not assets:
		if spec.required:
			status = "WARN" if sources_ok else "FAIL"
			details.append("no local sample found; source coverage only")
		else:
			status = "WARN" if sources_ok else "FAIL"
			details.append("optional media sample not present")
	elif largest is not None:
		sample_status, sample_detail = classify_sample(spec, largest)
		details.append(f"largest sample: {rel(largest, root)} ({largest.stat().st_size} bytes)")
		details.append(sample_detail)
		if sample_status == "FAIL":
			status = "FAIL"
		elif sample_status == "WARN" and status == "PASS":
			status = "WARN"
	return FormatResult(
		name=spec.name,
		status=status,
		extensions=list(spec.extensions),
		count=len(assets),
		largest_asset=rel(largest, root) if largest else "",
		largest_bytes=largest.stat().st_size if largest else 0,
		loader_sources=list(spec.loader_sources),
		detail="; ".join(details),
		required=spec.required,
	)


def write_summary(path: Path, report_path: Path, results: list[FormatResult],
		content_root: Path, root: Path) -> None:
	failed = [item for item in results if item.required and item.status == "FAIL"]
	warned = [item for item in results if item.status == "WARN"]
	with path.open("w", encoding="utf-8") as out:
		out.write("# GameCube GoldSrc Content Format Audit\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Content root: `{rel(content_root, root)}`\n")
		out.write(f"- Report: `{rel(report_path, root)}`\n")
		out.write(f"- Required failures: {len(failed)}\n")
		out.write(f"- Warnings: {len(warned)}\n\n")
		out.write("| Format | Status | Count | Largest Asset | Loader Sources | Detail |\n")
		out.write("|---|---|---:|---|---|---|\n")
		for item in results:
			loaders = ", ".join(f"`{source}`" for source in item.loader_sources)
			largest = f"`{item.largest_asset}` ({item.largest_bytes} bytes)" if item.largest_asset else "(none)"
			detail = item.detail.replace("|", "\\|")
			out.write(f"| {item.name} | {item.status} | {item.count} | {largest} | {loaders} | {detail} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This audit verifies local legal content samples, source loader coverage, "
			"and basic native GoldSrc file signatures. It does not redistribute game "
			"assets and does not replace Dolphin or real-hardware runtime evidence. "
			"PAK may warn on Steam-style installs that ship loose files instead of "
			"legacy PACK archives.\n"
		)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--content-root", type=Path, default=Path("Half-Life/valve"),
		help="legal local Half-Life valve directory to audit")
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	content_root = args.content_root
	if not content_root.is_absolute():
		content_root = root / content_root
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"content-format-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	report_path = log_dir / "report.json"
	summary_path = log_dir / "summary.md"

	results = [audit_format(root, content_root, spec) for spec in SPECS]
	failed = [item for item in results if item.required and item.status == "FAIL"]
	report_path.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"content_root": str(content_root),
		"ok": not failed,
		"results": [asdict(item) for item in results],
	}, indent=2) + "\n", encoding="utf-8")
	write_summary(summary_path, report_path, results, content_root, root)

	print(f"G67 content format audit summary: {summary_path}")
	for item in results:
		print(f"{item.status}: {item.name} - count={item.count} largest={item.largest_asset or '(none)'}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
