"""Reverse command."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from re_agent.config.loader import load_config
from re_agent.core.models import FunctionTarget
from re_agent.reports.formatter import format_result


def cmd_reverse(args: argparse.Namespace) -> int:
	config = load_config(Path(args.config))
	if args.max_rounds is not None:
		config.orchestrator.max_review_rounds = args.max_rounds
	if args.skip_parity:
		config.parity.enabled = False
	if args.dry_run:
		return _dry_run(args)

	from re_agent.backend.registry import create_backend
	from re_agent.core.session import Session
	from re_agent.llm.registry import create_provider
	from re_agent.orchestrator.class_runner import reverse_class
	from re_agent.orchestrator.single import reverse_single

	backend = create_backend(config.backend)
	llm = create_provider(config.llm)
	session = Session(config.output.session_file)

	if args.address:
		class_name = args.class_name or ""
		function_name = ""
		if not class_name:
			try:
				decompile = backend.decompile(args.address)
				if "::" in decompile.name:
					class_name, _, function_name = decompile.name.rpartition("::")
				else:
					function_name = decompile.name
			except Exception:
				pass
		target = FunctionTarget(address=args.address, class_name=class_name, function_name=function_name)
		result = reverse_single(target, config, backend, llm, session)
		print(format_result(result))
		return 0 if result.success else 1

	if args.class_name:
		results = reverse_class(args.class_name, config, backend, llm, session, max_functions=args.max_functions)
		for result in results:
			print(format_result(result))
			print()
		passed = sum(1 for result in results if result.success)
		print(f"Results: {passed}/{len(results)} passed")
		return 0 if passed == len(results) else 1

	print("Error: specify --address or --class", file=sys.stderr)
	return 1


def _dry_run(args: argparse.Namespace) -> int:
	print("Dry run mode — no LLM calls will be made.\n")
	if args.address:
		print(f"Would reverse: {args.address}")
		if args.class_name:
			print(f"  Class: {args.class_name}")
		return 0
	if args.class_name:
		print(f"Would reverse functions in class: {args.class_name}")
		print(f"  Max functions: {args.max_functions or 10}")
		print(f"  Max rounds per function: {args.max_rounds or 4}")
		return 0
	print("Error: specify --address or --class", file=sys.stderr)
	return 1
