"""Suspicious-function investigation command."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from re_agent.config.loader import load_config
from re_agent.reports.investigation import analyze_function, validate_report


def cmd_analyze(args: argparse.Namespace) -> int:
	root = Path.cwd()
	config = load_config(Path(args.config))
	try:
		from re_agent.backend.registry import create_backend
		backend = create_backend(config.backend)
		report = analyze_function(root, args.address, config, backend)
	except Exception as exc:
		print(f"Analysis failed: {exc}", file=sys.stderr)
		return 1

	if args.validate:
		validate_report(root, report, args.validation_timeout)

	output = Path(args.output) if args.output else (
		Path(config.output.report_dir) / "investigations" / f"{args.address.lower().replace('0x', '')}.json"
	)
	if not output.is_absolute():
		output = root / output
	output.parent.mkdir(parents=True, exist_ok=True)
	output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
	print(json.dumps(report, indent=2))
	print(f"Report written to {output}")
	if args.validate and not report["acceptance"]["accepted"]:
		return 2
	return 0
