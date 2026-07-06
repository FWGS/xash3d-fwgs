"""Backend protocol."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol, runtime_checkable

from re_agent.core.models import AsmResult, DecompileResult, FunctionEntry, XRef


@dataclass
class BackendCapabilities:
	has_decompile: bool = True
	has_asm: bool = False
	has_xrefs: bool = True
	has_search: bool = True


@runtime_checkable
class REBackend(Protocol):
	@property
	def capabilities(self) -> BackendCapabilities: ...

	def decompile(self, target: str) -> DecompileResult: ...

	def get_asm(self, target: str) -> AsmResult | None: ...

	def xrefs_to(self, target: str) -> list[XRef]: ...

	def xrefs_from(self, target: str) -> list[XRef]: ...

	def search(self, pattern: str) -> list[FunctionEntry]: ...

	def remaining(self, class_name: str | None = None) -> list[FunctionEntry]: ...
