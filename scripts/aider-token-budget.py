#!/usr/bin/env python3
"""Probe the local vLLM server and emit shell exports for Aider token budgets.

Usage:
  eval "$(python3 scripts/aider-token-budget.py --attempt 1)"
  python3 scripts/aider-token-budget.py --sync-metadata
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path


DEFAULT_API_BASE = "http://127.0.0.1:8072/v1"
DEFAULT_MODEL = "qwen-local"
DEFAULT_MAX_CONTEXT = 65536
METADATA_PATH = Path(".ai/aider-model-metadata.json")
SYSTEM_OVERHEAD_TOKENS = 4096
BYTES_PER_TOKEN = 3.5


def fetch_max_context(api_base: str, model: str) -> int:
	"""Return max_model_len from the OpenAI-compatible /v1/models endpoint."""
	url = f"{api_base.rstrip('/')}/models"
	req = urllib.request.Request(url, headers={"Accept": "application/json"})
	with urllib.request.urlopen(req, timeout=5) as resp:
		payload = json.load(resp)
	for item in payload.get("data", []):
		if item.get("id") == model:
			for key in ("max_model_len", "context_length", "max_context_length"):
				value = item.get(key)
				if isinstance(value, int) and value > 0:
					return value
	# Some servers expose limits only on the first model entry.
	if payload.get("data"):
		first = payload["data"][0]
		for key in ("max_model_len", "context_length", "max_context_length"):
			value = first.get(key)
			if isinstance(value, int) and value > 0:
				return value
	return DEFAULT_MAX_CONTEXT


def compute_budgets(max_context: int, attempt: int) -> dict[str, int]:
	"""Scale output/context/history budgets from the live model context window."""
	attempt = max(1, min(attempt, 4))
	max_output_cap = max(1024, min(16384, max_context // 4))
	output_tiers = [
		max(768, min(4096, max_output_cap)),
		max(512, min(2048, max_output_cap // 2)),
		max(384, min(1024, max_output_cap // 3)),
		max(256, min(768, max_output_cap // 4)),
	]
	input_budget = max(8192, max_context - output_tiers[0] - SYSTEM_OVERHEAD_TOKENS)
	max_bytes = int(input_budget * BYTES_PER_TOKEN)
	context_tiers = [
		max(8000, min(45000, max_bytes // 2)),
		max(6000, min(20000, max_bytes // 4)),
		max(4000, min(12000, max_bytes // 6)),
		max(3000, min(8000, max_bytes // 8)),
	]
	# Tighten further on later goal-loop attempts.
	attempt_scale = {1: 1.0, 2: 0.85, 3: 0.7, 4: 0.55}[attempt]
	context_tiers = [max(3000, int(value * attempt_scale)) for value in context_tiers]
	history = max(512, min(4096, int(max_context // 32 * attempt_scale)))
	return {
		"AIDER_MODEL_MAX_CONTEXT": max_context,
		"AIDER_MODEL_MAX_OUTPUT": max_output_cap,
		"AIDER_OUTPUT_TOKENS_INITIAL": output_tiers[0],
		"AIDER_OUTPUT_TOKENS_RETRY_1": output_tiers[1],
		"AIDER_OUTPUT_TOKENS_RETRY_2": output_tiers[2],
		"AIDER_OUTPUT_TOKENS_RETRY_3": output_tiers[3],
		"AIDER_CONTEXT_BYTES_INITIAL": context_tiers[0],
		"AIDER_CONTEXT_BYTES_RETRY_1": context_tiers[1],
		"AIDER_CONTEXT_BYTES_RETRY_2": context_tiers[2],
		"AIDER_CONTEXT_BYTES_RETRY_3": context_tiers[3],
		"AIDER_MAX_CHAT_HISTORY_TOKENS": history,
	}


def sync_metadata(max_context: int, max_output: int, model_key: str) -> None:
	if not METADATA_PATH.is_file():
		return
	data = json.loads(METADATA_PATH.read_text(encoding="utf-8"))
	entry = data.setdefault(model_key, {})
	entry["max_tokens"] = max_context
	entry["max_input_tokens"] = max_context
	entry["max_output_tokens"] = max_output
	METADATA_PATH.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def emit_shell(exports: dict[str, int]) -> None:
	for key, value in exports.items():
		print(f"export {key}={value}")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--api-base", default=os.environ.get("OPENAI_API_BASE", DEFAULT_API_BASE))
	parser.add_argument("--model", default=os.environ.get("AIDER_SERVED_MODEL", DEFAULT_MODEL))
	parser.add_argument("--attempt", type=int, default=int(os.environ.get("AIDER_BUDGET_ATTEMPT", "1")))
	parser.add_argument("--sync-metadata", action="store_true",
		help="update .ai/aider-model-metadata.json from the live server")
	parser.add_argument("--quiet", action="store_true")
	args = parser.parse_args()

	model_key = f"openai/{args.model}" if not args.model.startswith("openai/") else args.model
	try:
		max_context = fetch_max_context(args.api_base, args.model)
	except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, KeyError) as exc:
		if not args.quiet:
			print(f"# aider-token-budget: using defaults ({exc})", file=sys.stderr)
		max_context = DEFAULT_MAX_CONTEXT

	budgets = compute_budgets(max_context, args.attempt)
	if args.sync_metadata:
		sync_metadata(max_context, budgets["AIDER_MODEL_MAX_OUTPUT"], model_key)
	emit_shell(budgets)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
