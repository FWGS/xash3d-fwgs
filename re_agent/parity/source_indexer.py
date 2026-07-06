"""C/C++ source indexing for parity checks."""

from __future__ import annotations

import contextlib
import re
from collections import defaultdict
from pathlib import Path

from re_agent.config.schema import ProjectProfile
from re_agent.core.models import SourceMatch
from re_agent.utils.text import count_calls, count_control_flow, has_fp_token, strip_comments

FUNC_TOKEN_RE = re.compile(r"([A-Za-z_~][A-Za-z0-9_]*)::([A-Za-z_~][A-Za-z0-9_]*)\s*\(")


class SourceIndexer:
	def __init__(self, source_root: Path, profile: ProjectProfile | None = None) -> None:
		self.source_root = source_root
		extensions = profile.source_extensions if profile else [".c", ".cc", ".cpp", ".h", ".hpp"]
		self.stub_markers = tuple(profile.stub_markers) if profile else ("TODO",)
		self.stub_call_prefix = profile.stub_call_prefix if profile else "plugin::Call"
		self._hook_patterns: list[re.Pattern[str]] = []
		if profile:
			for pattern in profile.hook_patterns:
				with contextlib.suppress(re.error):
					self._hook_patterns.append(re.compile(pattern))
		self._class_macro_re: re.Pattern[str] | None = None
		if profile and profile.class_macro:
			with contextlib.suppress(re.error):
				self._class_macro_re = re.compile(rf"{re.escape(profile.class_macro)}\s*\(\s*(\w+)\s*\)")
		self.source_files = sorted(path for ext in extensions for path in source_root.rglob(f"*{ext}"))
		self.file_text_cache: dict[Path, str] = {}
		self.token_index: dict[tuple[str, str], list[tuple[Path, int]]] = defaultdict(list)
		self.hook_address_index: dict[str, tuple[str, str]] = {}
		self.lookup_cache: dict[tuple[str, str], SourceMatch | None] = {}
		self.free_lookup_cache: dict[str, SourceMatch | None] = {}
		self._build_index()

	def _read_text(self, path: Path) -> str:
		cached = self.file_text_cache.get(path)
		if cached is None:
			cached = path.read_text(encoding="utf-8", errors="ignore")
			self.file_text_cache[path] = cached
		return cached

	def _build_index(self) -> None:
		for path in self.source_files:
			text = self._read_text(path)
			for match in FUNC_TOKEN_RE.finditer(text):
				self.token_index[(match.group(1), match.group(2))].append((path, match.start()))
			file_class = ""
			if self._class_macro_re:
				class_match = self._class_macro_re.search(text)
				if class_match:
					file_class = class_match.group(1)
			for hook_pattern in self._hook_patterns:
				for hook_match in hook_pattern.finditer(text):
					if hook_match.lastindex and hook_match.lastindex >= 2:
						self.hook_address_index[hook_match.group(2).strip().lower()] = (file_class, hook_match.group(1).strip())

	@staticmethod
	def _skip_ws(text: str, index: int) -> int:
		while index < len(text) and text[index].isspace():
			index += 1
		return index

	@staticmethod
	def _find_matching_paren(text: str, open_index: int) -> int | None:
		depth = 0
		for index in range(open_index, len(text)):
			char = text[index]
			if char == "(":
				depth += 1
			elif char == ")":
				depth -= 1
				if depth == 0:
					return index
		return None

	@staticmethod
	def _find_matching_brace(text: str, open_index: int) -> int | None:
		depth = 0
		for index in range(open_index, len(text)):
			char = text[index]
			if char == "{":
				depth += 1
			elif char == "}":
				depth -= 1
				if depth == 0:
					return index
		return None

	def _find_function_body_open(self, text: str, fn_index: int, fn_name: str) -> int | None:
		paren_open = self._skip_ws(text, fn_index + len(fn_name))
		if paren_open >= len(text) or text[paren_open] != "(":
			return None
		paren_close = self._find_matching_paren(text, paren_open)
		if paren_close is None:
			return None
		index = self._skip_ws(text, paren_close + 1)
		while True:
			if text.startswith("const", index):
				index = self._skip_ws(text, index + len("const"))
				continue
			if text.startswith("noexcept", index):
				index = self._skip_ws(text, index + len("noexcept"))
				if index < len(text) and text[index] == "(":
					index = self._find_matching_paren(text, index) or index
					index = self._skip_ws(text, index + 1)
				continue
			break
		if index < len(text) and text[index] == "{":
			return index
		return None

	def _make_source_match(self, path: Path, text: str, signature_index: int, open_brace: int, close_brace: int) -> SourceMatch:
		body = text[open_brace:close_brace + 1]
		body_no_comments = strip_comments(body)
		total, plugin, non_plugin = count_calls(body_no_comments, self.stub_call_prefix)
		return SourceMatch(
			path=str(path),
			line=text.count("\n", 0, signature_index) + 1,
			body=body,
			body_no_comments=body_no_comments,
			body_lines=body.count("\n") + 1,
			call_count=total,
			plugin_call_count=plugin,
			non_plugin_call_count=non_plugin,
			control_flow_count=count_control_flow(body_no_comments),
			has_stub_marker=any(marker in body_no_comments for marker in self.stub_markers),
			has_fp_token=has_fp_token(body_no_comments),
			is_inline_internal_forwarder=self._is_inline_internal_forwarder(body_no_comments),
		)

	@staticmethod
	def _is_inline_internal_forwarder(body_no_comments: str) -> bool:
		text = body_no_comments.strip()
		if not (text.startswith("{") and text.endswith("}")):
			return False
		inner = text[1:-1].strip()
		if inner.startswith("return "):
			inner = inner[7:].strip()
		return inner.endswith(");") and "if" not in inner and "for" not in inner and "while" not in inner

	def find_by_address(self, address: str) -> SourceMatch | None:
		entry = self.hook_address_index.get(address.strip().lower())
		if entry is None:
			return None
		return self.find(entry[0], entry[1])

	def _find_free_function(self, fn_name: str) -> SourceMatch | None:
		if fn_name in self.free_lookup_cache:
			return self.free_lookup_cache[fn_name]
		pattern = re.compile(rf"(?<!::)\b{re.escape(fn_name)}\s*\(")
		for path in self.source_files:
			text = self._read_text(path)
			for match in pattern.finditer(text):
				open_brace = self._find_function_body_open(text, match.start(), fn_name)
				if open_brace is None:
					continue
				close_brace = self._find_matching_brace(text, open_brace)
				if close_brace is None:
					continue
				result = self._make_source_match(path, text, match.start(), open_brace, close_brace)
				self.free_lookup_cache[fn_name] = result
				return result
		self.free_lookup_cache[fn_name] = None
		return None

	def find(self, class_name: str, fn_name: str) -> SourceMatch | None:
		key = (class_name, fn_name)
		if key in self.lookup_cache:
			return self.lookup_cache[key]
		candidates = self.token_index.get(key, [])
		for path, index in candidates:
			text = self._read_text(path)
			fn_index = index + len(class_name) + 2
			open_brace = self._find_function_body_open(text, fn_index, fn_name)
			if open_brace is None:
				continue
			close_brace = self._find_matching_brace(text, open_brace)
			if close_brace is None:
				continue
			result = self._make_source_match(path, text, index, open_brace, close_brace)
			self.lookup_cache[key] = result
			return result
		result = self._find_free_function(fn_name)
		self.lookup_cache[key] = result
		return result
