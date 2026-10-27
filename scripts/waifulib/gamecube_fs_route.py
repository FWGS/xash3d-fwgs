# encoding: utf-8
"""TODO-FS-022 / TODO-FS-025: storage-route evidence schema and matrix scoring.

Compact schema (gc-fs-route-v1) fields:
  route, loader, volume, writable, artifact_hash, g508

Routes: sd, carda, cardb (writable FAT), disc (read-only), hybrid (FAT+disc).
"""
from __future__ import annotations

import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

_SCRIPTS = Path(__file__).resolve().parents[1]
if str(_SCRIPTS) not in sys.path:
	sys.path.insert(0, str(_SCRIPTS))

from waifulib.gamecube_storage import (  # noqa: E402
	parse_fat_volume_status,
	parse_g508_status,
	parse_loader_status,
)

SCHEMA_ID = "gc-fs-route-v1"
MATRIX_ROUTES: Tuple[str, ...] = ("sd", "carda", "cardb", "disc", "hybrid")
WRITABLE_ROUTES = frozenset({"sd", "carda", "cardb", "gcprobe"})
G508_VALUES = frozenset({"PASS", "FAIL", "unseen", "N/A"})
LOADERS = frozenset({"swiss", "dolphin", "unknown"})

_VOLUME_FOR_ROUTE = {
	"sd": "sd:/",
	"carda": "carda:/",
	"cardb": "cardb:/",
	"disc": "gcdisc:/",
	"hybrid": "sd:/",
	"gcprobe": "gcprobe:/",
}

_DVD_READY_RE = re.compile(r"DVD mount ready", re.IGNORECASE)
_DISC_ONLY_RE = re.compile(
	r"disc-only mode|no writable storage|SD storage not mounted",
	re.IGNORECASE,
)


def empty_record(route: str = "sd") -> Dict[str, Any]:
	return {
		"schema": SCHEMA_ID,
		"route": route,
		"loader": "unknown",
		"volume": _VOLUME_FOR_ROUTE.get(route, ""),
		"writable": route in WRITABLE_ROUTES or route == "hybrid",
		"artifact_hash": "",
		"g508": "unseen",
	}


def parse_schema_record(raw: str | Dict[str, Any]) -> Dict[str, Any]:
	"""Parse one schema record from JSON object/text or key=value lines."""
	if isinstance(raw, dict):
		data = dict(raw)
	else:
		text = (raw or "").strip()
		if not text:
			raise ValueError("empty schema record")
		if text.startswith("{"):
			data = json.loads(text)
		else:
			data = {}
			for line in text.splitlines():
				line = line.strip()
				if not line or line.startswith("#"):
					continue
				if "=" not in line:
					raise ValueError(f"ambiguous schema line: {line!r}")
				key, value = line.split("=", 1)
				data[key.strip()] = value.strip()

	rec = empty_record(str(data.get("route", "sd")))
	for key in ("schema", "route", "loader", "volume", "artifact_hash", "g508"):
		if key in data and data[key] is not None:
			rec[key] = str(data[key]).strip()
	if "writable" in data:
		val = data["writable"]
		if isinstance(val, bool):
			rec["writable"] = val
		else:
			rec["writable"] = str(val).strip().lower() in ("1", "true", "yes", "on")
	if rec["g508"]:
		rec["g508"] = rec["g508"].upper()
		if rec["g508"] == "NA":
			rec["g508"] = "N/A"
	return rec


def validate_schema_record(rec: Dict[str, Any]) -> List[str]:
	"""Return validation errors; empty list means schema-valid."""
	errors: List[str] = []
	if rec.get("schema") != SCHEMA_ID:
		errors.append(f"schema must be {SCHEMA_ID}")
	route = rec.get("route", "")
	if route not in MATRIX_ROUTES and route != "gcprobe":
		errors.append(f"unknown route {route!r}")
	if rec.get("loader") not in LOADERS:
		errors.append(f"unknown loader {rec.get('loader')!r}")
	g508 = rec.get("g508", "")
	if g508 not in G508_VALUES:
		errors.append(f"invalid g508 {g508!r}")
	writable = bool(rec.get("writable"))
	volume = str(rec.get("volume") or "")

	if route == "disc":
		if writable:
			errors.append("disc route cannot be writable")
		if g508 == "PASS":
			errors.append("disc route cannot claim g508=PASS (use N/A)")
		if volume and not volume.startswith("gcdisc"):
			errors.append("disc volume must be gcdisc:/ or empty")
	elif route == "hybrid":
		if not writable:
			errors.append("hybrid route must be writable")
		if g508 == "N/A":
			errors.append("hybrid route must exercise G508 when writable")
	elif route in ("sd", "carda", "cardb"):
		expected = _VOLUME_FOR_ROUTE[route]
		if volume and volume != expected:
			errors.append(f"{route} volume must be {expected}")
		if not writable and g508 == "PASS":
			errors.append("read-only FAT claim cannot have g508=PASS")
		if writable and g508 == "N/A":
			errors.append("writable FAT route cannot use g508=N/A")
	return errors


def load_schema_file(path: Path) -> Dict[str, Any]:
	text = path.read_text(encoding="utf-8", errors="replace")
	rec = parse_schema_record(text)
	errors = validate_schema_record(rec)
	if errors:
		raise ValueError(f"{path}: " + "; ".join(errors))
	return rec


def _read_text(path: Path) -> str:
	return path.read_text(encoding="utf-8", errors="replace")


def find_evidence_file(route: str, input_root: Optional[Path], log_root: Path) -> Optional[Path]:
	"""Prefer explicit INPUT/<route>/ evidence, else newest matching log under log_root."""
	if input_root is not None:
		candidates: List[Path] = []
		for name in ("fs-route.json", "swiss-evidence.txt", "stderr.log"):
			for base in (input_root / route, input_root):
				path = base / name
				if path.is_file():
					candidates.append(path)
		if candidates:
			order = {"fs-route.json": 0, "swiss-evidence.txt": 1, "stderr.log": 2}
			candidates.sort(key=lambda p: (order.get(p.name, 9), -p.stat().st_mtime))
			return candidates[0]

	matches: List[Tuple[float, Path]] = []
	if not log_root.is_dir():
		return None
	for path in log_root.rglob("*"):
		if not path.is_file():
			continue
		if path.name not in ("swiss-evidence.txt", "stderr.log", "fs-route.json"):
			continue
		try:
			text = _read_text(path)
		except OSError:
			continue
		if route == "hybrid":
			needle_ok = bool(
				re.search(r"(sd|carda|cardb):/", text)
				and (_DVD_READY_RE.search(text) or "gcdisc" in text.lower())
			)
		elif route == "disc":
			needle_ok = bool(
				_DVD_READY_RE.search(text)
				or _DISC_ONLY_RE.search(text)
				or "gcdisc:/" in text
			)
		else:
			needle_ok = bool(
				re.search(rf"{re.escape(route)}:/|route={re.escape(route)}\b", text)
			)
		if needle_ok:
			matches.append((path.stat().st_mtime, path))
	if not matches:
		return None
	matches.sort(key=lambda item: item[0], reverse=True)
	return matches[0][1]


def infer_record_from_log(route: str, text: str) -> Dict[str, Any]:
	rec = empty_record(route)
	body = text or ""
	fat = parse_fat_volume_status(body)
	g508 = parse_g508_status(body)
	loader = parse_loader_status(body)

	if loader.get("swiss_return") or "Swiss" in body:
		rec["loader"] = "swiss"
	elif "Dolphin" in body or "gcprobe" in body:
		rec["loader"] = "dolphin"

	if route in ("sd", "carda", "cardb"):
		vol = f"{route}:/"
		rec["volume"] = vol
		rec["writable"] = True
		if g508.get("route") == route and g508.get("ready"):
			rec["g508"] = "PASS"
		elif g508.get("write_failed") and g508.get("route") in (route, None):
			rec["g508"] = "FAIL"
		else:
			rec["g508"] = "unseen"
	elif route == "disc":
		rec["writable"] = False
		rec["volume"] = "gcdisc:/"
		rec["g508"] = "N/A"
	elif route == "hybrid":
		rec["writable"] = True
		preferred = fat.get("preferred") or ((fat.get("volumes") or [None])[0])
		rec["volume"] = preferred or "sd:/"
		fat_route = (preferred or "sd:/").rstrip("/").split(":")[0]
		if g508.get("ready") and g508.get("route") in (fat_route, "sd", "carda", "cardb"):
			rec["g508"] = "PASS"
		elif g508.get("write_failed"):
			rec["g508"] = "FAIL"
		else:
			rec["g508"] = "unseen"
	return rec


def score_route(
	route: str,
	*,
	log_text: str = "",
	schema_rec: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
	"""Score one matrix route. status is PASS or FAIL."""
	if schema_rec is not None:
		errors = validate_schema_record(schema_rec)
		if errors:
			return {
				"route": route,
				"status": "FAIL",
				"g508": schema_rec.get("g508", "unseen"),
				"writable": bool(schema_rec.get("writable")),
				"reason": "schema: " + "; ".join(errors),
				"record": schema_rec,
			}
		rec = dict(schema_rec)
	else:
		rec = infer_record_from_log(route, log_text)

	body = log_text or ""
	fat = parse_fat_volume_status(body)
	g508 = parse_g508_status(body)
	dvd = bool(_DVD_READY_RE.search(body))
	disc_only = bool(_DISC_ONLY_RE.search(body))

	status = "FAIL"
	reason = "missing evidence"

	if route in ("sd", "carda", "cardb"):
		vol = f"{route}:/"
		has_fat = vol in (fat.get("volumes") or []) or f"{route}:/" in body
		g508_ok = (g508.get("route") == route and g508.get("ready")) or rec.get("g508") == "PASS"
		if schema_rec is not None and not body:
			status = "PASS" if rec.get("g508") == "PASS" and rec.get("writable") else "FAIL"
			reason = "schema record" if status == "PASS" else "schema g508 not PASS"
		elif g508_ok and (has_fat or rec.get("g508") == "PASS"):
			status = "PASS"
			reason = "G508 ready"
			rec["g508"] = "PASS"
		elif not body and schema_rec is None:
			reason = "missing"
		else:
			reason = "G508 not ready for writable route"
			if rec.get("g508") == "unseen" and g508.get("write_failed"):
				rec["g508"] = "FAIL"
	elif route == "disc":
		false_fat_g508 = bool(
			g508.get("ready") and g508.get("route") in ("sd", "carda", "cardb")
		)
		if false_fat_g508 and not fat.get("ok"):
			status = "FAIL"
			reason = "ambiguous: G508 FAT claim without FAT volume"
		elif schema_rec is not None and not body:
			status = "PASS" if (not rec.get("writable") and rec.get("g508") == "N/A") else "FAIL"
			reason = "schema disc semantics" if status == "PASS" else "schema disc invalid"
		elif dvd or disc_only:
			status = "PASS"
			reason = "disc-only/DVD ready"
			rec["g508"] = "N/A"
		else:
			reason = "disc markers missing"
	elif route == "hybrid":
		fat_ok = bool(fat.get("ok")) or any(
			f"{r}:/" in body for r in ("sd", "carda", "cardb")
		)
		g508_ok = bool(
			g508.get("ready") and g508.get("route") in ("sd", "carda", "cardb", "gcprobe")
		)
		if schema_rec is not None and not body:
			status = "PASS" if rec.get("writable") and rec.get("g508") == "PASS" else "FAIL"
			reason = "schema hybrid" if status == "PASS" else "schema hybrid incomplete"
		elif fat_ok and dvd and (g508_ok or rec.get("g508") == "PASS"):
			status = "PASS"
			reason = "FAT+DVD+G508"
			rec["g508"] = "PASS"
		elif not body and schema_rec is None:
			reason = "missing"
		else:
			missing = []
			if not fat_ok:
				missing.append("FAT")
			if not dvd:
				missing.append("DVD")
			if not g508_ok and rec.get("g508") != "PASS":
				missing.append("G508")
			reason = "hybrid missing " + "+".join(missing or ["evidence"])

	return {
		"route": route,
		"status": status,
		"g508": rec.get("g508", "unseen"),
		"writable": bool(rec.get("writable")),
		"reason": reason,
		"record": rec,
	}


def evaluate_matrix(
	*,
	input_root: Optional[Path] = None,
	log_root: Path,
	routes: Sequence[str] = MATRIX_ROUTES,
) -> Dict[str, Any]:
	rows: List[Dict[str, Any]] = []
	for route in routes:
		evidence = find_evidence_file(route, input_root, log_root)
		schema_rec = None
		log_text = ""
		evidence_s = "missing"
		if evidence is not None:
			evidence_s = str(evidence)
			if evidence.name == "fs-route.json":
				schema_rec = load_schema_file(evidence)
			else:
				log_text = _read_text(evidence)
				sibling = evidence.parent / "fs-route.json"
				if sibling.is_file():
					schema_rec = load_schema_file(sibling)
		row = score_route(route, log_text=log_text, schema_rec=schema_rec)
		row["evidence"] = evidence_s
		rows.append(row)

	fails = sum(1 for r in rows if r["status"] != "PASS")
	return {
		"rows": rows,
		"pass": len(rows) - fails,
		"fail": fails,
		"ok": fails == 0,
		"verify": "PASS" if fails == 0 else "FAIL",
	}


def format_tsv(report: Dict[str, Any]) -> str:
	lines = ["route\tevidence\twritable\tg508\tstatus\treason", "-----"]
	for row in report["rows"]:
		lines.append(
			"\t".join(
				[
					row["route"],
					row["evidence"],
					"1" if row["writable"] else "0",
					str(row["g508"]),
					row["status"],
					row["reason"],
				]
			)
		)
	lines.append("")
	lines.append(f"FS022_VERIFY: {report['verify']}")
	return "\n".join(lines) + "\n"


def format_summary_md(report: Dict[str, Any]) -> str:
	lines = [
		"# FS-022 Storage Route Matrix",
		"",
		f"- Pass: {report['pass']}",
		f"- Fail: {report['fail']}",
		"",
		"| Route | Writable | G508 | Status | Evidence | Reason |",
		"|---|---|---|---|---|---|",
	]
	for row in report["rows"]:
		lines.append(
			"| {route} | {writable} | {g508} | {status} | `{evidence}` | {reason} |".format(
				route=row["route"],
				writable="yes" if row["writable"] else "no",
				g508=row["g508"],
				status=row["status"],
				evidence=row["evidence"],
				reason=row["reason"],
			)
		)
	lines += [
		"",
		f"**FS022_VERIFY: {report['verify']}**",
		"",
		"## Hardware sign-off (still required)",
		"",
		"- [ ] Swiss SD2SP2 (`sd:/`) G508 config round-trip",
		"- [ ] SD Gecko Slot A (`carda:/`) when claimed",
		"- [ ] SD Gecko Slot B (`cardb:/`) when claimed",
		"- [ ] Disc-only boot without inventing a writable route",
		"- [ ] Hybrid FAT+disc boot with assets on disc and saves on FAT",
		"",
	]
	return "\n".join(lines)


def main(argv: Optional[Sequence[str]] = None) -> int:
	import argparse

	parser = argparse.ArgumentParser(description="FS-022 storage route matrix / FS-025 schema")
	parser.add_argument("evidence_root", nargs="?", default="", help="optional evidence root")
	parser.add_argument("--log-root", default=os.environ.get("FS022_LOG_ROOT", ".ai/logs"))
	parser.add_argument("--out", default="", help="write summary.md + matrix.tsv here")
	parser.add_argument("--validate", default="", help="validate one fs-route.json or record file")
	parser.add_argument("--strict", action="store_true", help="exit 1 unless FS022_VERIFY PASS")
	args = parser.parse_args(list(argv) if argv is not None else None)

	if args.validate:
		path = Path(args.validate)
		text = path.read_text(encoding="utf-8", errors="replace") if path.is_file() else args.validate
		rec = parse_schema_record(text)
		errors = validate_schema_record(rec)
		if errors:
			print("FAIL:", "; ".join(errors))
			return 1
		print(json.dumps(rec, indent=2))
		print("FS025_SCHEMA: PASS")
		return 0

	input_root = Path(args.evidence_root) if args.evidence_root else None
	log_root = Path(args.log_root)
	report = evaluate_matrix(input_root=input_root, log_root=log_root)
	tsv = format_tsv(report)
	print(tsv, end="")

	if args.out:
		out_dir = Path(args.out)
	elif os.environ.get("FS022_OUT"):
		out_dir = Path(os.environ["FS022_OUT"])
	else:
		stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
		out_dir = Path(f".ai/logs/storage-route-matrix-{stamp}")
	out_dir.mkdir(parents=True, exist_ok=True)
	(out_dir / "matrix.tsv").write_text(tsv, encoding="utf-8")
	(out_dir / "summary.md").write_text(format_summary_md(report), encoding="utf-8")
	print(f"Summary: {out_dir / 'summary.md'}")

	strict = args.strict or os.environ.get("FS022_STRICT", "").strip().lower() in ("1", "true", "yes")
	if strict and not report["ok"]:
		return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
