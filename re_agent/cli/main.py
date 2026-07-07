"""CLI entry point."""

from __future__ import annotations

import argparse

from re_agent import __version__


def runtime_probe() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(prog="re-agent", description="Autonomous reverse engineering agent")
	parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
	parser.add_argument("--config", default="re-agent.yaml", help="Config file path")

	sub = parser.add_subparsers(dest="command", help="Available commands")

	init_parser = sub.add_parser("init", help="Initialize re-agent.yaml config file")
	init_parser.add_argument("--profile", default=None, help="Reserved for future built-in profiles")

	reverse_parser = sub.add_parser("reverse", help="Reverse engineer functions")
	reverse_parser.add_argument("--address", help="Single function address to reverse")
	reverse_parser.add_argument("--class", dest="class_name", help="Class name for class-level reversal")
	reverse_parser.add_argument("--max-functions", type=int, default=None, help="Max functions per class")
	reverse_parser.add_argument("--max-rounds", type=int, default=None, help="Max review rounds per function")
	reverse_parser.add_argument("--dry-run", action="store_true", help="Show plan without executing")
	reverse_parser.add_argument("--skip-parity", action="store_true", help="Skip parity check after PASS")

	parity_parser = sub.add_parser("parity", help="Run parity checks on hooked functions")
	parity_parser.add_argument("--address", action="append", help="Specific address (repeatable)")
	parity_parser.add_argument("--filter", help="Regex filter on symbol/class")
	parity_parser.add_argument("--limit", type=int, help="Max functions to check")
	parity_parser.add_argument("--skip-ghidra", action="store_true", help="Source-only checks")
	parity_parser.add_argument("--strict-exit", action="store_true", help="Exit 1 on RED")
	parity_parser.add_argument("--output", help="Output JSON report path")

	status_parser = sub.add_parser("status", help="Show reversal progress")
	status_parser.add_argument("--class", dest="class_name", help="Filter by class")
	status_parser.add_argument("--format", choices=["text", "json", "markdown"], default="text")

	return parser


def main(argv: list[str] | None = None) -> int:
	parser = build_parser()
	args = parser.parse_args(argv)
	if args.command is None:
		parser.print_help()
		return 0
	if args.command == "init":
		from re_agent.cli.cmd_init import cmd_init
		return cmd_init(args)
	if args.command == "reverse":
		from re_agent.cli.cmd_reverse import cmd_reverse
		return cmd_reverse(args)
	if args.command == "parity":
		from re_agent.cli.cmd_parity import cmd_parity
		return cmd_parity(args)
	if args.command == "status":
		from re_agent.cli.cmd_status import cmd_status
		return cmd_status(args)
	parser.print_help()
	return 1
