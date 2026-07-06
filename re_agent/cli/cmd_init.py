"""Init command."""

from __future__ import annotations

import argparse
from pathlib import Path

from re_agent.config.defaults import DEFAULT_CONFIG_YAML


def cmd_init(args: argparse.Namespace) -> int:
	config_path = Path(args.config)
	if config_path.exists():
		print(f"Config already exists: {config_path}")
		print("Delete it first if you want to regenerate.")
		return 1
	config_path.write_text(DEFAULT_CONFIG_YAML, encoding="utf-8")
	print(f"Created {config_path}")
	print("Edit it to configure your LLM provider, backend, and project profile.")
	return 0
