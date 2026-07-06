"""Text and code analysis helpers."""

from __future__ import annotations

import re

_COMMENT_RE = re.compile(r"//.*?$|/\*.*?\*/", re.DOTALL | re.MULTILINE)
_CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_:<>]*)\s*\(")
_CONTROL_FLOW_RE = re.compile(r"\b(if|else\s+if|for|while|switch|case|\?|catch)\b")
_FP_TOKEN_RE = re.compile(r"\b(float|double|fabs|sqrt|sin|cos|NaN|NAN|isnan)\b", re.IGNORECASE)
_ASM_FP_RE = re.compile(r"\b(fadd|fsub|fmul|fdiv|fld|fst|xmm|sse)\b", re.IGNORECASE)


def strip_comments(text: str) -> str:
	return _COMMENT_RE.sub("", text)


def count_calls(body: str, stub_call_prefix: str = "plugin::Call") -> tuple[int, int, int]:
	total = plugin = 0
	for match in _CALL_RE.finditer(body):
		name = match.group(1)
		if name in {"if", "for", "while", "switch", "return", "sizeof"}:
			continue
		total += 1
		if stub_call_prefix and name.startswith(stub_call_prefix):
			plugin += 1
	return total, plugin, total - plugin


def count_control_flow(body: str) -> int:
	return len(_CONTROL_FLOW_RE.findall(body))


def has_fp_token(body: str) -> bool:
	return bool(_FP_TOKEN_RE.search(body))


def has_fp_asm(text: str) -> bool:
	return bool(_ASM_FP_RE.search(text))
