#!/usr/bin/env python3
"""G501 experiment run manifest — freeze baseline without Dolphin.

Records branch, commit, automation tier, OGC stack resolution, hypothesis,
and optional ladder/probe attachments under .ai/logs/experiment-<stamp>/.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional


def repo_root() -> Path:
	try:
		out = subprocess.check_output(
			["git", "rev-parse", "--show-toplevel"],
			text=True,
			stderr=subprocess.DEVNULL,
		)
		return Path(out.strip())
	except (subprocess.CalledProcessError, FileNotFoundError):
		return Path(__file__).resolve().parents[1]


def git_value(root: Path, *args: str, default: str = "UNAVAILABLE") -> str:
	try:
		return subprocess.check_output(
			["git", *args],
			cwd=root,
			text=True,
			stderr=subprocess.DEVNULL,
		).strip() or default
	except (subprocess.CalledProcessError, FileNotFoundError):
		return default


def load_json(path: Path) -> Any:
	if not path.is_file():
		return None
	try:
		return json.loads(path.read_text(encoding="utf-8"))
	except json.JSONDecodeError:
		return None


def resolve_ogc_stack(root: Path) -> dict:
	sys.path.insert(0, str(root / "scripts" / "waifulib"))
	try:
		from gamecube_ogc_stack import resolve_ogc_stack as resolve
		return resolve()
	except Exception as exc:  # noqa: BLE001 — host-only; record failure
		return {
			"available": False,
			"stack": None,
			"error": str(exc),
			"devkitpro": os.environ.get("DEVKITPRO", "/opt/devkitpro"),
		}


def worktree_dirty(root: Path) -> bool:
	try:
		status = subprocess.check_output(
			["git", "status", "--porcelain", "--ignore-submodules=untracked"],
			cwd=root,
			text=True,
		)
		return bool(status.strip())
	except (subprocess.CalledProcessError, FileNotFoundError):
		return False


def build_manifest(
	root: Path,
	*,
	hypothesis: str,
	target_files: list[str],
	keep_revert: str,
	ladder_json: Optional[Path] = None,
	probe_log: Optional[Path] = None,
	dry_run: bool = False,
) -> dict:
	tier = load_json(root / ".ai/state/gc-port-automation-tier.json") or {}
	ogc = resolve_ogc_stack(root)
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	manifest = {
		"schema": "xash3d-gc-experiment-manifest/v1",
		"created_at": datetime.now(timezone.utc).isoformat(),
		"stamp": stamp,
		"dry_run": dry_run,
		"git": {
			"branch": git_value(root, "rev-parse", "--abbrev-ref", "HEAD"),
			"commit": git_value(root, "rev-parse", "HEAD"),
			"short": git_value(root, "rev-parse", "--short", "HEAD"),
			"parent": git_value(root, "rev-parse", "HEAD^"),
			"dirty": worktree_dirty(root),
		},
		"tier": {
			"value": tier.get("tier", "UNAVAILABLE"),
			"note": tier.get("note"),
			"source": ".ai/state/gc-port-automation-tier.json",
		},
		"ogc_stack": {
			"available": bool(ogc.get("available")),
			"stack": ogc.get("stack") or "UNAVAILABLE",
			"fat_provider": ogc.get("fat_provider") or "UNAVAILABLE",
			"root": ogc.get("root") or "UNAVAILABLE",
			"error": ogc.get("error"),
		},
		"hypothesis": hypothesis,
		"target_files": target_files,
		"decision": keep_revert,
		"asset_source": os.environ.get("XASH3D_GC_DATA")
			or os.environ.get("XASH3D_GC_ASSET_ROOT")
			or "UNAVAILABLE",
		"attachments": {},
	}
	if ladder_json and ladder_json.is_file():
		manifest["attachments"]["ladder"] = load_json(ladder_json)
	if probe_log and probe_log.exists():
		manifest["attachments"]["probe_log"] = str(probe_log)
	return manifest


def main(argv: Optional[list[str]] = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=None)
	parser.add_argument("--hypothesis", required=True)
	parser.add_argument("--target-file", action="append", default=[], dest="target_files")
	parser.add_argument(
		"--decision",
		choices=("pending", "keep", "revert"),
		default="pending",
		help="keep/revert decision for this candidate (default pending)",
	)
	parser.add_argument("--ladder-json", type=Path, default=None)
	parser.add_argument("--probe-log", type=Path, default=None)
	parser.add_argument("--output-dir", type=Path, default=None)
	parser.add_argument(
		"--dry-run",
		action="store_true",
		help="Print/write under /tmp when unset; still emits a valid manifest.",
	)
	args = parser.parse_args(argv)

	root = (args.repo or repo_root()).resolve()
	manifest = build_manifest(
		root,
		hypothesis=args.hypothesis,
		target_files=list(args.target_files),
		keep_revert=args.decision,
		ladder_json=args.ladder_json,
		probe_log=args.probe_log,
		dry_run=bool(args.dry_run),
	)

	if args.output_dir:
		out_dir = args.output_dir if args.output_dir.is_absolute() else root / args.output_dir
	elif args.dry_run:
		out_dir = Path("/tmp") / f"xash3d-gc-experiment-{manifest['stamp']}"
	else:
		out_dir = root / ".ai" / "logs" / f"experiment-{manifest['stamp']}"

	out_dir.mkdir(parents=True, exist_ok=True)
	path = out_dir / "manifest.json"
	path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

	# Convenience pointer for automation.
	latest = root / ".ai" / "state" / "experiment-latest.json"
	if not args.dry_run:
		latest.parent.mkdir(parents=True, exist_ok=True)
		latest.write_text(json.dumps({
			"manifest": str(path.relative_to(root)) if path.is_relative_to(root) else str(path),
			"stamp": manifest["stamp"],
			"hypothesis": manifest["hypothesis"],
			"commit": manifest["git"]["short"],
			"tier": manifest["tier"]["value"],
			"ogc_stack": manifest["ogc_stack"]["stack"],
		}, indent=2) + "\n", encoding="utf-8")

	print(f"EXPERIMENT_MANIFEST: {path}")
	print(f"  branch={manifest['git']['branch']} commit={manifest['git']['short']}")
	print(f"  tier={manifest['tier']['value']} ogc={manifest['ogc_stack']['stack']}")
	print(f"  hypothesis={manifest['hypothesis']!r} decision={manifest['decision']}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
