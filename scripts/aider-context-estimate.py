#!/usr/bin/env python3
"""Estimate whether a budgeted Aider pass fits the local vLLM context window."""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
from pathlib import Path

_BUDGET_MODULE = Path(__file__).with_name("aider-context-budget.py")
_spec = importlib.util.spec_from_file_location("aider_context_budget", _BUDGET_MODULE)
_budget = importlib.util.module_from_spec(_spec)
assert _spec.loader is not None
sys.modules[_spec.name] = _budget
_spec.loader.exec_module(_budget)

DEFAULT_MAX_CONTEXT = int(os.environ.get("AIDER_MODEL_MAX_CONTEXT", "65536"))
RESERVED_OUTPUT_SLACK = 256


def estimate_tokens(editable_bytes: int, read_bytes: int, output_tokens: int) -> int:
	return int((editable_bytes + read_bytes) / _budget.BYTES_PER_TOKEN) + \
		_budget.SYSTEM_OVERHEAD_TOKENS + output_tokens + RESERVED_OUTPUT_SLACK


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path("."))
	parser.add_argument("--attempt", type=int, default=1)
	parser.add_argument("--output-tokens", type=int, default=2048)
	parser.add_argument("--max-context", type=int, default=DEFAULT_MAX_CONTEXT)
	parser.add_argument("--quiet", action="store_true")
	parser.add_argument("specs", nargs="*", help="raw context specs")
	args = parser.parse_args()

	root = args.repo.resolve()
	try:
		specs = [_budget.parse_spec(raw) for raw in args.specs]
	except ValueError as exc:
		print(f"OVER_BUDGET: {exc}", file=sys.stderr)
		return 18

	budgeted = _budget.budget_context(root, specs, args.attempt, args.output_tokens)
	editable_bytes = 0
	read_bytes = 0
	for spec in budgeted:
		size = _budget.file_size(root, spec.path)
		if spec.mode in {"file", "required"}:
			editable_bytes += size
		else:
			read_bytes += size if spec.mode == "read" else 0
		if spec.mode == "slice":
			# Slice excerpts are much smaller than the full source file.
			read_bytes += min(size, 8000)

	total_tokens = estimate_tokens(editable_bytes, read_bytes, args.output_tokens)
	status = "OK" if total_tokens <= args.max_context else "OVER_BUDGET"
	message = (
		f"{status}: attempt={args.attempt} editable={editable_bytes} read={read_bytes} "
		f"output={args.output_tokens} estimate={total_tokens}/{args.max_context}"
	)
	if args.quiet and status == "OK":
		print(message)
	else:
		print(message, file=sys.stderr if status != "OK" else sys.stdout)
	return 0 if status == "OK" else 18


if __name__ == "__main__":
	raise SystemExit(main())
