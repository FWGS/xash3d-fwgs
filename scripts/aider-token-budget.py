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
DEFAULT_MAX_CONTEXT = 200000
METADATA_PATH = Path(".ai/aider-model-metadata.json")
DEFAULT_SYSTEM_OVERHEAD_TOKENS = 6144
LOW_VRAM_SYSTEM_OVERHEAD_TOKENS = 4096
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
	profile = os.environ.get("AI_LOCAL_PROFILE", "").strip().lower()
	low_vram = profile in {"rtx-pro-4000-24gb", "rtx4000-24gb", "24gb"} or \
		os.environ.get("AIDER_LOW_VRAM_PROFILE") in {"1", "true", "yes"}
	system_overhead_tokens = int(os.environ.get(
		"AIDER_SYSTEM_OVERHEAD_TOKENS",
		str(LOW_VRAM_SYSTEM_OVERHEAD_TOKENS if low_vram else DEFAULT_SYSTEM_OVERHEAD_TOKENS),
	))
	if low_vram:
		max_context = min(max_context, 32768)
	attempt = max(1, min(attempt, 4))
	attempt_scale = {1: 1.0, 2: 0.75, 3: 0.5, 4: 0.35}[attempt]
	max_output_cap = max(512, min(1024, max_context // 16))
	output_tiers = [
		max(450, min(1200, max_output_cap // 2)),
		max(300, min(900, max_output_cap // 3)),
		max(220, min(600, max_output_cap // 4)),
		max(150, min(350, max_output_cap // 6)),
	]
	output_tiers = [max(128, int(value * attempt_scale)) for value in output_tiers]
	input_budget = max(4096, max_context - output_tiers[0] - system_overhead_tokens)
	max_bytes = int(input_budget * BYTES_PER_TOKEN)
	if max_context >= 60000:
		context_tiers = [
			max(12500, min(24000, max_bytes // 4)),
			max(8500, min(14000, max_bytes // 6)),
			max(5500, min(9000, max_bytes // 8)),
			max(3500, min(5000, max_bytes // 12)),
		]
	else:
		context_tiers = [
			max(3800, min(12000, max_bytes // 5)),
			max(2800, min(8000, max_bytes // 7)),
			max(1800, min(5000, max_bytes // 10)),
			max(1400, min(3000, max_bytes // 14)),
		]
	context_tiers = [max(1500, int(value * attempt_scale)) for value in context_tiers]
	history = max(128, min(512, int(max_context // 160 * attempt_scale)))
	if low_vram:
		output_tiers = [max(128, min(value, cap)) for value, cap in zip(output_tiers, (512, 384, 256, 192))]
		# Relax context floor slightly to avoid immediate budget failures during recovery
		low_vram_context_floors = (17000, 14000, 11000, 9000)
		context_tiers = [
			max(floor, int(value * 0.65))
			for value, floor in zip(context_tiers, low_vram_context_floors)
		]
		history = max(128, min(history, 220))
	editable_tiers = context_tiers
	if low_vram:
		# Source-first discovery must preserve at least one medium-sized frame
		# source file; the preflight estimator still rejects combinations that
		# exceed the actual model window.
		editable_tiers = (40000, 40000, 40000, 40000)
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
		"AIDER_EDITABLE_BYTES_INITIAL": editable_tiers[0],
		"AIDER_EDITABLE_BYTES_RETRY_1": editable_tiers[1],
		"AIDER_EDITABLE_BYTES_RETRY_2": editable_tiers[2],
		"AIDER_EDITABLE_BYTES_RETRY_3": editable_tiers[3],
		"AIDER_READ_BYTES_INITIAL": max(2000, context_tiers[0] // 2),
		"AIDER_READ_BYTES_RETRY_1": max(1500, context_tiers[1] // 2),
		"AIDER_READ_BYTES_RETRY_2": max(1000, context_tiers[2] // 2),
		"AIDER_READ_BYTES_RETRY_3": max(800, context_tiers[3] // 2),
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
