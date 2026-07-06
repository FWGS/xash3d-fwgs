"""Parity command."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from re_agent.config.loader import load_config
from re_agent.core.models import HookEntry, ParityStatus
from re_agent.parity.engine import read_hooks, run_parity
from re_agent.utils.address import normalize_address


def cmd_parity(args: argparse.Namespace) -> int:
	config = load_config(Path(args.config))
	source_root = Path(config.project_profile.source_root)
	if not source_root.exists():
		print(f"Error: source root not found: {source_root}", file=sys.stderr)
		return 1

	hooks: list[HookEntry] = []
	if config.project_profile.hooks_csv:
		hooks_path = Path(config.project_profile.hooks_csv)
		if hooks_path.exists():
			hooks = read_hooks(hooks_path)
		else:
			print(f"Warning: hooks CSV not found: {hooks_path}", file=sys.stderr)

	if args.address:
		wanted = {normalize_address(address) for address in args.address}
		matched = [hook for hook in hooks if normalize_address(hook.address) in wanted]
		matched_addrs = {normalize_address(hook.address) for hook in matched}
		for address in wanted - matched_addrs:
			matched.append(HookEntry(class_path="", fn_name="", address=address, reversed=True, locked=False, is_virtual=False))
		hooks = matched
	elif not hooks:
		print("No hooks loaded. Provide --address or configure hooks_csv in project_profile.", file=sys.stderr)
		return 1

	if args.filter:
		regex = re.compile(args.filter)
		hooks = [hook for hook in hooks if regex.search(hook.symbol) or regex.search(hook.class_path)]
	if args.limit:
		hooks = hooks[:args.limit]
	if not hooks:
		print("No hooks selected.")
		return 0

	backend = None
	if not args.skip_ghidra:
		try:
			from re_agent.backend.registry import create_backend
			backend = create_backend(config.backend)
		except Exception as exc:
			print(f"Warning: could not initialize backend ({exc}), running source-only checks", file=sys.stderr)

	results = run_parity(hooks, source_root, config, backend=backend)
	counts = {status.value: 0 for status in ParityStatus}
	for result in results:
		hook = result["hook"]
		status = result["status"]
		status_text = status.value if isinstance(status, ParityStatus) else str(status)
		counts[status_text] = counts.get(status_text, 0) + 1
		print(f"  {hook.symbol} ({hook.address}) -> {status_text.upper()}")

	green = counts.get(ParityStatus.GREEN.value, 0)
	yellow = counts.get(ParityStatus.YELLOW.value, 0)
	red = counts.get(ParityStatus.RED.value, 0)
	print(f"\nSummary: GREEN={green} YELLOW={yellow} RED={red}")

	if args.output:
		output_path = Path(args.output)
		output_path.parent.mkdir(parents=True, exist_ok=True)
		payload = {
			"results": [
				{
					"symbol": result["hook"].symbol,
					"address": result["hook"].address,
					"status": result["status"].value if isinstance(result["status"], ParityStatus) else str(result["status"]),
					"findings": [{"level": finding.level, "reason": finding.reason} for finding in result.get("findings", [])],
				}
				for result in results
			]
		}
		output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
		print(f"Report written to {output_path}")

	return 1 if args.strict_exit and red > 0 else 0
