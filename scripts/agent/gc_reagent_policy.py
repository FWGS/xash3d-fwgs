#!/usr/bin/env python3
"""Decide whether function-level re_agent analysis is justified by evidence."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any


FILESYSTEM_ONLY = (
	"delta.lst", "fs_fileexists", "fs_loadfile", "search path", "searchpath",
	"filesystem contract", "asset staging", "bsp not found",
)
FUNCTION_TARGET = re.compile(
	r"(?:function|call path|returns? (?:an )?unexpected|abi|struct(?:ure)? layout|"
	r"address\s*0x[0-9a-f]+|0x[0-9a-f]+.*(?:trace|guest failure|crash))",
	re.IGNORECASE,
)
ADDRESS = re.compile(r"\b0x[0-9a-f]{4,}\b", re.IGNORECASE)


def target_eligibility(evidence: str) -> dict[str, Any]:
	"""Return a fail-closed decision for a proposed re_agent invocation."""
	text = evidence or ""
	lower = text.lower()
	filesystem_hits = [term for term in FILESYSTEM_ONLY if term in lower]
	addresses = ADDRESS.findall(text)
	if filesystem_hits and not FUNCTION_TARGET.search(text):
		return {
			"eligible": False,
			"reason": "filesystem_contract_without_function_target",
			"evidence": filesystem_hits,
			"next_action": "instrument FS_FileExists/FS_LoadFile, verify ISO asset, run map ladder",
		}
	if not FUNCTION_TARGET.search(text):
		return {
			"eligible": False,
			"reason": "no_function_level_target",
			"evidence": [],
			"next_action": "collect a concrete function, ABI, structure, or address target",
		}
	return {
		"eligible": True,
		"reason": "function_level_target_present",
		"evidence": addresses or ["function-level evidence"],
		"next_action": "run re_agent and then normal build/Dolphin gates",
	}


def accept_report(report: dict[str, Any], *, build_ok: bool,
		dolphin_ok: bool) -> tuple[bool, list[str]]:
	"""Require re_agent output and independent runtime gates before acceptance."""
	failures: list[str] = []
	for field in ("address", "decompile", "source_match", "confidence"):
		if not report.get(field):
			failures.append(f"missing re_agent report field: {field}")
	if not build_ok:
		failures.append("normal build gate failed")
	if not dolphin_ok:
		failures.append("normal Dolphin gate failed")
	return not failures, failures


def evidence_from_paths(paths: list[Path]) -> str:
	parts: list[str] = []
	for path in paths:
		try:
			parts.append(path.read_text(encoding="utf-8", errors="replace"))
		except OSError:
			continue
	return "\n".join(parts)
