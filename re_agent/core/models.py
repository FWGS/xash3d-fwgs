"""Core re-agent data models."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum


@dataclass
class FunctionTarget:
	address: str
	class_name: str
	function_name: str
	caller_count: int = 0


class Verdict(Enum):
	PASS = "PASS"
	FAIL = "FAIL"
	UNKNOWN = "UNKNOWN"


class ParityStatus(Enum):
	GREEN = "green"
	YELLOW = "yellow"
	RED = "red"


@dataclass
class Finding:
	level: str
	reason: str


@dataclass
class CheckerVerdict:
	verdict: Verdict
	summary: str
	issues: list[str] = field(default_factory=list)
	fix_instructions: list[str] = field(default_factory=list)


@dataclass
class ObjectiveVerdict:
	verdict: Verdict
	summary: str
	findings: list[str] = field(default_factory=list)


@dataclass
class ReversalResult:
	target: FunctionTarget
	code: str
	checker_verdict: CheckerVerdict | None = None
	objective_verdict: ObjectiveVerdict | None = None
	parity_status: ParityStatus | None = None
	parity_findings: list[Finding] = field(default_factory=list)
	rounds_used: int = 0
	success: bool = False


@dataclass
class DecompileResult:
	address: str
	name: str
	signature: str
	decompiled: str
	raw_output: str
	callers: int | None = None
	callees: int | None = None


@dataclass
class AsmResult:
	address: str
	instructions: str
	instruction_count: int
	call_count: int
	has_fp_sensitive: bool


@dataclass
class FunctionEntry:
	address: str
	name: str
	class_name: str = ""
	caller_count: int = 0


@dataclass
class XRef:
	address: str
	name: str
	ref_type: str


@dataclass
class SourceMatch:
	path: str
	line: int
	body: str
	body_no_comments: str
	body_lines: int
	call_count: int
	plugin_call_count: int
	non_plugin_call_count: int
	control_flow_count: int
	has_stub_marker: bool
	has_fp_token: bool
	is_inline_internal_forwarder: bool


@dataclass
class HookEntry:
	class_path: str
	fn_name: str
	address: str
	reversed: bool
	locked: bool
	is_virtual: bool

	@property
	def class_name(self) -> str:
		return self.class_path.split("/")[-1] if self.class_path else ""

	@property
	def symbol(self) -> str:
		if self.class_name and self.fn_name:
			return f"{self.class_name}::{self.fn_name}"
		return self.fn_name or self.address


@dataclass
class GhidraData:
	decompile_ok: bool = False
	decompile_error: str | None = None
	callers: int | None = None
	callees: int | None = None
	decompile_has_nan: bool = False
	asm_ok: bool = False
	asm_error: str | None = None
	asm_instruction_count: int = 0
	asm_call_count: int = 0
	asm_has_fp_sensitive: bool = False
	resolved_address: str | None = None
