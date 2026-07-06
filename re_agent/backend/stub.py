"""Simple fixture-backed backend for development and tests."""

from __future__ import annotations

import json
from pathlib import Path

from re_agent.backend.protocol import BackendCapabilities
from re_agent.core.models import AsmResult, DecompileResult, FunctionEntry, XRef
from re_agent.utils.address import normalize_address


class StubBackend:
	def __init__(self, fixtures_dir: str | None = None) -> None:
		self._fixtures_dir = Path(fixtures_dir) if fixtures_dir else None
		self._capabilities = BackendCapabilities(has_decompile=True, has_asm=True, has_xrefs=True, has_search=True)
		self._payload = self._load_payload()

	@property
	def capabilities(self) -> BackendCapabilities:
		return self._capabilities

	def _load_payload(self) -> dict[str, object]:
		if self._fixtures_dir is None:
			return {"functions": {}, "remaining": []}
		payload = self._fixtures_dir / "backend.json"
		if not payload.exists():
			return {"functions": {}, "remaining": []}
		return json.loads(payload.read_text(encoding="utf-8"))

	def _function(self, target: str) -> dict[str, object]:
		functions = self._payload.get("functions", {})
		if not isinstance(functions, dict):
			raise RuntimeError("Invalid backend fixtures")
		key = normalize_address(target) if target.startswith("0x") or target[:1].isdigit() else target
		data = functions.get(key)
		if not isinstance(data, dict):
			raise RuntimeError(f"No fixture for target {target}")
		return data

	def decompile(self, target: str) -> DecompileResult:
		data = self._function(target)
		return DecompileResult(
			address=str(data.get("address", target)),
			name=str(data.get("name", target)),
			signature=str(data.get("signature", "")),
			decompiled=str(data.get("decompiled", "")),
			raw_output=str(data.get("raw_output", data.get("decompiled", ""))),
			callers=data.get("callers") if isinstance(data.get("callers"), int) else None,
			callees=data.get("callees") if isinstance(data.get("callees"), int) else None,
		)

	def get_asm(self, target: str) -> AsmResult | None:
		data = self._function(target)
		asm = data.get("asm")
		if not isinstance(asm, dict):
			return None
		return AsmResult(
			address=str(asm.get("address", target)),
			instructions=str(asm.get("instructions", "")),
			instruction_count=int(asm.get("instruction_count", 0)),
			call_count=int(asm.get("call_count", 0)),
			has_fp_sensitive=bool(asm.get("has_fp_sensitive", False)),
		)

	def xrefs_to(self, target: str) -> list[XRef]:
		return []

	def xrefs_from(self, target: str) -> list[XRef]:
		return []

	def search(self, pattern: str) -> list[FunctionEntry]:
		out: list[FunctionEntry] = []
		functions = self._payload.get("functions", {})
		if not isinstance(functions, dict):
			return out
		for data in functions.values():
			if not isinstance(data, dict):
				continue
			name = str(data.get("name", ""))
			if pattern.lower() in name.lower():
				out.append(FunctionEntry(
					address=str(data.get("address", "")),
					name=name,
					class_name=str(data.get("class_name", "")),
					caller_count=int(data.get("callers", 0) or 0),
				))
		return out

	def remaining(self, class_name: str | None = None) -> list[FunctionEntry]:
		entries = self._payload.get("remaining", [])
		if not isinstance(entries, list):
			return []
		out: list[FunctionEntry] = []
		for entry in entries:
			if not isinstance(entry, dict):
				continue
			entry_class = str(entry.get("class_name", ""))
			if class_name and entry_class != class_name:
				continue
			out.append(FunctionEntry(
				address=str(entry.get("address", "")),
				name=str(entry.get("name", "")),
				class_name=entry_class,
				caller_count=int(entry.get("caller_count", 0)),
			))
		return out
