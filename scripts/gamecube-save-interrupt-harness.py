#!/usr/bin/env python3
"""TODO-SAVE-022: gcprobe .new/.bak rename-interrupt harness + hardware checklist."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from waifulib.gamecube_probe_save import (  # noqa: E402
	SAVE022_MARKERS,
	simulate_save022_rename_interrupt,
)


HARDWARE_CHECKLIST = [
	"Writable route mounted (sd:/, carda:/, or cardb:/)",
	"Write config.cfg via menu Options or Host_WriteConfig path",
	"Confirm config.cfg.new appears during write (Swiss/FAT browser or OSReport)",
	"Power-loss or forced rename failure while .new exists and config→.bak done",
	"Reboot: config.cfg present (recovered from .new or restored from .bak)",
	"No truncated empty config.cfg; gameplay bindings still load",
	"Full-card case: write fails non-fatally; prior config untouched",
	"Record route + dated evidence in docs/GAMECUBE_HARDWARE_VALIDATION.md",
]


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=ROOT)
	parser.add_argument("--log-dir", type=Path)
	parser.add_argument(
		"--probe-log",
		type=Path,
		help="optional Dolphin stderr/probe log to classify SAVE-022 markers",
	)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"save-interrupt-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)

	sim = simulate_save022_rename_interrupt()
	probe_text = ""
	if args.probe_log and args.probe_log.is_file():
		probe_text = args.probe_log.read_text(encoding="utf-8", errors="replace")
	elif args.probe_log:
		print(f"probe log missing: {args.probe_log}", file=sys.stderr)
		return 1

	markers_seen = {
		name: (token in probe_text) for name, token in SAVE022_MARKERS.items()
	} if probe_text else {}

	host_ok = bool(sim.get("ok") and sim.get("fault_fired") and sim.get("config"))
	guest_ok: bool | None = None
	if probe_text:
		guest_ok = markers_seen.get("recovered", False) or (
			markers_seen.get("fault", False)
			and (markers_seen.get("recovered_new", False) or markers_seen.get("restored_bak", False))
		)

	report = {
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": host_ok and guest_ok is not False,
		"host_simulation": {
			"ok": sim.get("ok"),
			"fault_fired": sim.get("fault_fired"),
			"recovered_from": sim.get("recovered_from"),
			"config_preview": (sim.get("config") or b"")[:80].decode("utf-8", "replace"),
		},
		"probe_markers": markers_seen,
		"hardware_checklist": HARDWARE_CHECKLIST,
		"dolphin_hint": "DOLPHIN_NEWGAME=1 DOLPHIN_SAVE_INTERRUPT=1 DOLPHIN_SMOKE_MAP=c0a0e scripts/dolphin-boot-probe.sh",
	}

	(report_path := log_dir / "report.json").write_text(
		json.dumps(report, indent=2) + "\n", encoding="utf-8"
	)
	summary = log_dir / "summary.md"
	guest_label = "PASS" if guest_ok else ("UNSEEN" if guest_ok is None else "FAIL")
	with summary.open("w", encoding="utf-8") as out:
		out.write("# SAVE-022 Save Interrupt Harness\n\n")
		out.write(f"- Generated: {report['generated']}\n")
		out.write(f"- Host simulation: {'PASS' if host_ok else 'FAIL'}\n")
		out.write(f"- Guest probe markers: {guest_label}\n")
		out.write(f"- Report: `{report_path}`\n\n")
		out.write("## Host simulation\n\n")
		out.write(f"- fault_fired: `{sim.get('fault_fired')}`\n")
		out.write(f"- recovered_from: `{sim.get('recovered_from')}`\n")
		out.write(f"- config readable: `{bool(sim.get('config'))}`\n\n")
		out.write("## Hardware checklist (TODO-SAVE-010 / SAVE-022)\n\n")
		for item in HARDWARE_CHECKLIST:
			out.write(f"- [ ] {item}\n")
		out.write("\n## Dolphin\n\n")
		out.write(f"`{report['dolphin_hint']}`\n")

	print(summary)
	return 0 if report["ok"] else 1


if __name__ == "__main__":
	raise SystemExit(main())
