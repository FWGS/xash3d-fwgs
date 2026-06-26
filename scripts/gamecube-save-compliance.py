#!/usr/bin/env python3
"""Generate G46 save-integrity and destructive-action policy evidence."""

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
	log_dir = args.log_dir or root / ".ai/logs" / f"save-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	save_source = read(root / "engine/server/sv_save.c")
	cmd_source = read(root / "engine/server/sv_cmds.c")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")
	validation = read(root / "docs/GAMECUBE_HARDWARE_VALIDATION.md")
	goals = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")

	checks: list[Check] = []
	checks.append(Check(
		"metadata magic/version",
		"PASS" if contains_all(save_source, ("GC_SAVE_META_MAGIC", "GC_SAVE_META_VERSION", "XASHGC_SAVE_META")) else "FAIL",
		"declares GameCube save sidecar magic and version",
	))
	checks.append(Check(
		"metadata payload integrity",
		"PASS" if contains_all(save_source, ("CRC32_ProcessBuffer", "CRC32_Final", "payload_size=", "payload_crc32=")) else "FAIL",
		"records payload size and CRC32 for the saved .sav file",
	))
	checks.append(Check(
		"metadata context",
		"PASS" if contains_all(save_source, ("map=%s", "build=%s", "storage_route=%s", "g_buildcommit", "GCube_GetWritablePath")) else "FAIL",
		"records map, build hash, and writable storage route",
	))
	checks.append(Check(
		"atomic sidecar commit",
		"PASS" if contains_all(save_source, (".tmp", ".bak", "FS_Rename( tmpPath, metaPath )", "FS_Rename( backupPath, metaPath )")) else "FAIL",
		"writes metadata through temp/backup names before final rename",
	))
	checks.append(Check(
		"metadata rotation cleanup",
		"PASS" if contains_all(save_source, ("oldMeta", "newMeta", "FS_Rename( oldMeta, newMeta )", "FS_Delete( newMeta )")) else "FAIL",
		"rotates/deletes sidecar files with quick/autosave slots",
	))
	checks.append(Check(
		"write confirmation policy",
		"PASS" if contains_all(cmd_source, ("save <savename> confirm", "requires explicit confirmation before creating or overwriting", "GCube_HasWritableStorage")) else "FAIL",
		"GameCube manual saves require explicit confirmation and writable storage",
	))
	checks.append(Check(
		"destructive confirmation policy",
		"PASS" if contains_all(cmd_source, ("killsave <name> confirm", "requires explicit confirmation before deleting", ".sav.gcmeta")) else "FAIL",
		"GameCube delete path requires confirmation and removes metadata sidecars",
	))
	checks.append(Check(
		"automatic destructive writes disabled",
		"PASS" if contains_all(cmd_source, ("quicksave disabled by release save-integrity policy", "autosave skipped by release save-integrity policy")) else "FAIL",
		"quicksave/autosave do not silently rotate save files on GameCube",
	))
	checks.append(Check(
		"docs protocol",
		"PASS" if "Save Integrity Preflight" in validation and "interruption, full-card, removed-card" in validation else "FAIL",
		"hardware protocol names required storage failure cases",
	))
	checks.append(Check(
		"ledger sync",
		"PASS" if "G46 [x]" in goals and "save integrity" in plan.lower() else "FAIL",
		"goal ledger and port plan both describe the G46 state",
	))
	checks.append(Check(
		"hardware storage boundary",
		"WARN",
		"Physical SD/memory-card interruption, full-card, removed-card, corrupt-file, wrong-slot, and incompatible-version evidence still belongs in G38/G53/G66.",
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
		out.write("# GameCube Save Compliance\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			out.write(f"| {check.name} | {check.status} | {check.detail.replace('|', '\\|')} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This closes the automated G46 source/policy preflight. Hardware-complete "
			"save acceptance still requires dated evidence for interruption, full-card, "
			"removed-card, corrupt-file, wrong-slot, incompatible-version, save/load, "
			"quit/relaunch/load, and storage-route behavior on real hardware or the "
			"release-designated persistent storage route.\n"
		)

	print(summary)
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
