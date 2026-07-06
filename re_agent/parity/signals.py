"""Heuristic parity signals."""

from __future__ import annotations

from typing import Callable

from re_agent.core.models import Finding, GhidraData, SourceMatch


Signal = Callable[[SourceMatch | None, GhidraData | None, bool, int], Finding | None]


def signal_missing_source(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is None:
		return Finding(level="red", reason="No source body found")
	return None


def signal_stub_markers(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is not None and source.has_stub_marker:
		return Finding(level="red", reason="Source contains stub markers")
	return None


def signal_trivial_stub(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is None:
		return None
	if source.body_lines <= 12 and source.plugin_call_count > 0 and source.non_plugin_call_count == 0 and source.control_flow_count == 0:
		return Finding(level="red", reason="Source looks like a trivial stub")
	return None


def signal_large_asm_tiny_source(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is None or ghidra is None:
		return None
	if ghidra.asm_instruction_count >= 80 and source.body_lines <= 12:
		return Finding(level="red", reason="Large ASM body paired with tiny source body")
	return None


def signal_plugin_call_heavy(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is None or source.call_count == 0:
		return None
	if source.plugin_call_count > source.non_plugin_call_count:
		return Finding(level="yellow", reason="Plugin-call heavy source body")
	return None


def signal_short_body(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is not None and source.body_lines < 6 and not inline_skip:
		return Finding(level="yellow", reason="Very short source body")
	return None


def signal_low_call_count(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is None or ghidra is None or ghidra.callees is None:
		return None
	if ghidra.callees - source.call_count >= call_count_warn_diff:
		return Finding(level="yellow", reason="Source call count is well below decompile call count")
	return None


def signal_fp_sensitivity(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is None or ghidra is None:
		return None
	if ghidra.asm_has_fp_sensitive and not source.has_fp_token:
		return Finding(level="yellow", reason="ASM suggests floating-point logic but source does not")
	return None


def signal_nan_logic(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is None or ghidra is None:
		return None
	if ghidra.decompile_has_nan and "nan" not in source.body_no_comments.lower():
		return Finding(level="yellow", reason="Decompile shows NaN logic missing from source")
	return None


def signal_inline_wrapper(source: SourceMatch | None, ghidra: GhidraData | None, inline_skip: bool, call_count_warn_diff: int) -> Finding | None:
	if source is not None and source.is_inline_internal_forwarder:
		return Finding(level="info", reason="Thin inline wrapper")
	return None


ALL_SIGNALS: tuple[Signal, ...] = (
	signal_missing_source,
	signal_stub_markers,
	signal_trivial_stub,
	signal_large_asm_tiny_source,
	signal_plugin_call_heavy,
	signal_short_body,
	signal_low_call_count,
	signal_fp_sensitivity,
	signal_nan_logic,
	signal_inline_wrapper,
)
