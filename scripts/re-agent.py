#!/usr/bin/env python3
"""Wrapper script for the local re-agent package."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
	sys.path.insert(0, str(ROOT))

from re_agent.cli.main import main


if __name__ == "__main__":
	raise SystemExit(main())
