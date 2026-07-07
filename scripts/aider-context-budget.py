#!/usr/bin/env python3
"""Shrink Aider context lists to fit the local vLLM context window.

Reads context spec tokens from argv (file:, required:, read:, slice:path:ranges)
and prints a budgeted spec list, one entry per line.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
from dataclasses import dataclass
from pathlib import Path

_SLICE_MODULE = Path(__file__).with_name("aider-context-slice.py")
_spec = importlib.util.spec_from_file_location("aider_context_slice", _SLICE_MODULE)
_slice = importlib.util.module_from_spec(_spec)
assert _spec.loader is not None
sys.modules[_spec.name] = _slice
_spec.loader.exec_module(_slice)
default_output_path = _slice.default_output_path
parse_ranges = _slice.parse_ranges
slice_path = _slice.slice_path


BYTES_PER_TOKEN = 3.5
SYSTEM_OVERHEAD_TOKENS = int(os.environ.get("AIDER_SYSTEM_OVERHEAD_TOKENS", "24576"))
DEFAULT_MAX_CONTEXT = int(os.environ.get("AIDER_MODEL_MAX_CONTEXT", "65536"))

EDITABLE_TOTAL_LIMITS = [
	int(os.environ.get("AIDER_EDITABLE_BYTES_INITIAL", "40000")),
	int(os.environ.get("AIDER_EDITABLE_BYTES_RETRY_1", "32000")),
	int(os.environ.get("AIDER_EDITABLE_BYTES_RETRY_2", "10000")),
	int(os.environ.get("AIDER_EDITABLE_BYTES_RETRY_3", "7000")),
]
READ_TOTAL_LIMITS = [
	int(os.environ.get("AIDER_READ_BYTES_INITIAL", "10000")),
	int(os.environ.get("AIDER_READ_BYTES_RETRY_1", "6000")),
	int(os.environ.get("AIDER_READ_BYTES_RETRY_2", "3500")),
	int(os.environ.get("AIDER_READ_BYTES_RETRY_3", "2000")),
]
MAX_EDITABLE_COUNT = [3, 2, 1, 1]
MAX_SINGLE_EDITABLE = int(os.environ.get("AIDER_MAX_EDITABLE_FILE_BYTES", "40000"))

# When a source path is too large to --file safely, use read-only slices instead.
FILE_SLICES: dict[str, str] = {
	"engine/common/mod_bmodel.c": "2418-2435,4325-4365",
	"engine/common/mod_studio.c": "1-120,1170-1240",
	"engine/server/sv_game.c": "4875-5125,5078-5120",
	"engine/client/cl_scrn.c": "870-990,950-980",
	"engine/platform/gamecube/vid_gamecube.c": "1-180",
	"scripts/dolphin-boot-probe.sh": "1-222",
	"docs/GAMECUBE_PORT_PLAN.md": "1-220",
	".ai/goals/GAMECUBE_PORT_GOALS.md": "1-260",
}


@dataclass(frozen=True)
class ContextSpec:
	mode: str
	path: str
	ranges: str | None = None


def parse_spec(raw: str) -> ContextSpec:
	if raw.startswith("required:"):
		return ContextSpec("required", raw[len("required:"):])
	if raw.startswith("read:"):
		return ContextSpec("read", raw[len("read:"):])
	if raw.startswith("slice:"):
		payload = raw[len("slice:"):]
		path, _, ranges = payload.partition(":")
		if not ranges:
			raise ValueError(f"slice spec missing ranges: {raw}")
		return ContextSpec("slice", path, ranges)
	if raw.startswith("file:"):
		return ContextSpec("file", raw[len("file:"):])
	return ContextSpec("file", raw)


def file_size(root: Path, rel_path: str) -> int:
	path = root / rel_path
	if not path.is_file():
		return 0
	return path.stat().st_size


def estimate_tokens(total_bytes: int, output_tokens: int) -> int:
	return int(total_bytes / BYTES_PER_TOKEN) + SYSTEM_OVERHEAD_TOKENS + output_tokens


def emit(spec: ContextSpec) -> str:
	if spec.mode == "slice":
		assert spec.ranges is not None
		return f"slice-read:{spec.path}:{spec.ranges}"
	if spec.mode == "required":
		return f"required:{spec.path}"
	if spec.mode == "read":
		return f"read:{spec.path}"
	return spec.path


def slice_for_path(path: str) -> ContextSpec | None:
	ranges = FILE_SLICES.get(path)
	if not ranges:
		return None
	return ContextSpec("slice", path, ranges)


def budget_context(root: Path, specs: list[ContextSpec], attempt: int,
	output_tokens: int) -> list[ContextSpec]:
	attempt_idx = max(0, min(attempt - 1, len(EDITABLE_TOTAL_LIMITS) - 1))
	editable_limit = EDITABLE_TOTAL_LIMITS[attempt_idx]
	read_limit = READ_TOTAL_LIMITS[attempt_idx]
	max_editables = MAX_EDITABLE_COUNT[attempt_idx]

	editable: list[ContextSpec] = []
	reads: list[ContextSpec] = []
	for spec in specs:
		if spec.mode in {"file", "required"}:
			editable.append(spec)
		elif spec.mode == "read":
			reads.append(spec)
		elif spec.mode == "slice":
			reads.append(spec)

	if os.environ.get("AIDER_PRESERVE_CONTEXT_ORDER", "0").lower() not in {"1", "true", "yes"}:
		# Prefer smaller editable files first; keep required ordering stable.
		editable.sort(key=lambda item: (0 if item.mode == "required" else 1,
			file_size(root, item.path)))

	selected_editables: list[ContextSpec] = []
	selected_reads: list[ContextSpec] = []
	editable_bytes = 0
	read_bytes = 0

	for spec in editable:
		size = file_size(root, spec.path)
		if size > MAX_SINGLE_EDITABLE:
			slice_spec = None if selected_editables else slice_for_path(spec.path)
			if slice_spec and read_bytes + 8000 <= read_limit:
				selected_reads.append(slice_spec)
				read_bytes += 8000
			continue
		if len(selected_editables) >= max_editables:
			slice_spec = None if selected_editables else slice_for_path(spec.path)
			if slice_spec and read_bytes + 8000 <= read_limit:
				selected_reads.append(slice_spec)
				read_bytes += 8000
			continue
		if editable_bytes + size > editable_limit:
			slice_spec = None if selected_editables else slice_for_path(spec.path)
			if slice_spec and read_bytes + 8000 <= read_limit:
				selected_reads.append(slice_spec)
				read_bytes += 8000
			continue
		selected_editables.append(spec)
		editable_bytes += size

	for spec in reads:
		size = file_size(root, spec.path)
		if read_bytes + size > read_limit:
			slice_spec = slice_for_path(spec.path)
			if slice_spec:
				selected_reads.append(slice_spec)
			continue
		selected_reads.append(spec)
		read_bytes += size

	if attempt >= 4:
		selected_reads = selected_reads[:1]

	# If we still estimate over the model window, keep only the first editable.
	projected = estimate_tokens(editable_bytes + read_bytes, output_tokens)
	if projected > DEFAULT_MAX_CONTEXT - 512 and selected_editables:
		selected_editables = selected_editables[:1]
		selected_reads = [] if attempt >= 2 else selected_reads[:1]

	out: list[ContextSpec] = []
	for spec in selected_editables:
		out.append(spec)
	for spec in selected_reads:
		out.append(spec)
	return out


def materialize_slice(root: Path, spec: ContextSpec) -> Path:
	assert spec.ranges is not None
	source = root / spec.path
	ranges = parse_ranges(spec.ranges)
	output = default_output_path(source, ranges)
	if not output.is_file() or output.stat().st_mtime < source.stat().st_mtime:
		slice_path(source, ranges, output)
	return output


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path("."))
	parser.add_argument("--attempt", type=int, default=1)
	parser.add_argument("--output-tokens", type=int, default=2048)
	parser.add_argument("--materialize", action="store_true",
		help="write slice-read entries to .ai/slices and emit read: paths")
	parser.add_argument("specs", nargs="*", help="context specs from ai-aider-pass")
	args = parser.parse_args()

	root = args.repo.resolve()
	try:
		specs = [parse_spec(raw) for raw in args.specs]
	except ValueError as exc:
		print(f"aider-context-budget: {exc}", file=sys.stderr)
		return 1

	budgeted = budget_context(root, specs, args.attempt, args.output_tokens)
	for spec in budgeted:
		if args.materialize and spec.mode == "slice":
			slice_file = materialize_slice(root, spec)
			print(f"read:{slice_file.relative_to(root).as_posix()}")
			continue
		print(emit(spec))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
