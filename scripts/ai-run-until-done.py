#!/usr/bin/env python3
"""Keep the GameCube goal runner moving until automatic goals are exhausted."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from itertools import count
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


def run(command: list[str], root: Path, env: dict[str, str] | None = None) -> int:
	print("$ " + " ".join(command), flush=True)
	return subprocess.run(command, cwd=root, env=env or os.environ.copy(),
		check=False).returncode


def load_dotenv(path: Path) -> None:
	if not path.is_file():
		return
	for raw_line in path.read_text(encoding="utf-8").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("#") or "=" not in line:
			continue
		if line.startswith("export "):
			line = line[len("export "):].lstrip()
		key, value = line.split("=", 1)
		key = key.strip()
		value = value.strip().strip("'\"")
		if key and key not in os.environ:
			os.environ[key] = value


def api_models_url(api_base: str) -> str:
	parsed = urlparse(api_base)
	if parsed.path.rstrip("/").endswith("/v1"):
		return api_base.rstrip("/") + "/models"
	return api_base.rstrip("/") + "/v1/models"


def model_ready(api_base: str) -> bool:
	request = Request(api_models_url(api_base))
	if os.environ.get("OPENAI_API_KEY"):
		request.add_header("Authorization", f"Bearer {os.environ['OPENAI_API_KEY']}")
	try:
		with urlopen(request, timeout=3) as response:
			return 200 <= response.status < 500
	except (OSError, URLError):
		return False


def read_goals(root: Path) -> list[dict[str, object]]:
	result = subprocess.run(["scripts/ai-goal-loop.py", "--repo", str(root), "--status-json"],
		cwd=root, text=True, capture_output=True, check=False)
	if result.returncode != 0:
		raise RuntimeError(result.stderr.strip() or "goal status failed")
	return json.loads(result.stdout)


def next_automatic_goal(root: Path) -> str | None:
	for goal in read_goals(root):
		if not goal["complete"] and not goal["manual"] and not goal["blocked"]:
			return f"{goal['goal_id']} {goal['title']}"
	return None


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--chunk-passes", type=int, default=0,
		help="passes per goal-loop invocation; 0 means unlimited")
	parser.add_argument("--max-cycles", type=int, default=0,
		help="maximum supervisor cycles; 0 means unlimited")
	parser.add_argument("--recoverable-retries", type=int, default=8)
	parser.add_argument("--sleep", type=int, default=15)
	args = parser.parse_args()
	root = args.repo.resolve()
	load_dotenv(root / ".env")
	api_base = os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")

	if not os.environ.get("OPENAI_API_KEY"):
		print("run-until-done: OPENAI_API_KEY is not set", file=sys.stderr)
		return 2
	if not model_ready(api_base):
		print(f"run-until-done: model API is not reachable at {api_base}", file=sys.stderr)
		print("Start the Qwable/vLLM server from the GUI or QWABLE_5_COMMAND first.", file=sys.stderr)
		return 2

	if args.chunk_passes < 0:
		parser.error("--chunk-passes must be zero or positive")
	if args.max_cycles < 0:
		parser.error("--max-cycles must be zero or positive")

	cycles = count(1) if args.max_cycles == 0 else range(1, args.max_cycles + 1)
	for cycle in cycles:
		goal = next_automatic_goal(root)
		if goal is None:
			print("run-until-done: all automatic goals are complete or blocked")
			return 0
		cycle_limit = "unlimited" if args.max_cycles == 0 else str(args.max_cycles)
		print(f"\n== supervisor cycle {cycle}/{cycle_limit}: {goal} ==", flush=True)
		status = run(["scripts/ai-goal-loop.py", "--repo", str(root),
			"--max-passes", str(args.chunk_passes),
			"--recoverable-retries", str(args.recoverable_retries)], root)
		if status == 0:
			continue
		if status in {3, 10, 17, 18}:
			print(f"run-until-done: recoverable exit {status}; continuing after {args.sleep}s",
				file=sys.stderr)
			time.sleep(args.sleep)
			continue
		print(f"run-until-done: stopped on non-recoverable exit {status}", file=sys.stderr)
		return status

	if args.max_cycles > 0:
		print("run-until-done: cycle limit reached with automatic goals remaining", file=sys.stderr)
		return 3
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
