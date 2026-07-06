"""Status command."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from re_agent.config.loader import load_config
from re_agent.core.session import Session
from re_agent.reports.tracker import ProgressTracker


def cmd_status(args: argparse.Namespace) -> int:
	config = load_config(Path(args.config))
	session = Session(config.output.session_file)
	tracker = ProgressTracker(session)

	if args.format == "json":
		data = tracker.get_function_table(args.class_name) if args.class_name else session.get_summary()
		print(json.dumps(data, indent=2))
		return 0

	if args.format == "markdown":
		rows = tracker.get_function_table(args.class_name)
		if not rows:
			print("No functions recorded yet.")
			return 0
		print("| Address | Class | Function | Status | Rounds | Time |")
		print("|---------|-------|----------|--------|--------|------|")
		for row in rows:
			print(f"| {row['address']} | {row['class']} | {row['function']} | {row['status']} | {row['rounds']} | {row['timestamp']} |")
		return 0

	if args.class_name:
		print(tracker.print_class_summary(args.class_name))
		for row in tracker.get_function_table(args.class_name):
			print(f"  {row['address']}  {row['function']:40s}  {row['status']:4s}  ({row['rounds']} rounds)")
	else:
		print(tracker.print_summary())
	return 0
