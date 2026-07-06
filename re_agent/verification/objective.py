"""Objective structural verifier."""

from __future__ import annotations

from re_agent.backend.protocol import REBackend
from re_agent.core.models import FunctionTarget, ObjectiveVerdict, Verdict
from re_agent.utils.text import count_calls, count_control_flow, strip_comments


def verify_candidate(
	code: str,
	target: FunctionTarget,
	backend: REBackend,
	call_count_tolerance: int = 3,
	control_flow_tolerance: int = 2,
) -> ObjectiveVerdict:
	if not code.strip():
		return ObjectiveVerdict(verdict=Verdict.FAIL, summary="No candidate code produced", findings=["Candidate code is empty"])

	source_body = strip_comments(_extract_body(code))
	source_call_count, _, _ = count_calls(source_body)
	source_flow_count = count_control_flow(source_body)
	findings: list[str] = []
	checks_run = 0

	try:
		decompile = backend.decompile(target.address)
	except Exception as exc:
		return ObjectiveVerdict(verdict=Verdict.UNKNOWN, summary="Objective verifier could not read decompile output", findings=[str(exc)])

	decompile_body = strip_comments(_extract_body(decompile.raw_output))
	decompile_flow = count_control_flow(decompile_body)
	if decompile.callees is not None:
		checks_run += 1
		if decompile.callees - source_call_count >= call_count_tolerance:
			findings.append(f"Call count mismatch: decompile reports {decompile.callees}, candidate has {source_call_count}")
	if decompile_flow >= 2:
		checks_run += 1
		if decompile_flow - source_flow_count >= control_flow_tolerance:
			findings.append(f"Control-flow mismatch: decompile has {decompile_flow}, candidate has {source_flow_count}")
	if backend.capabilities.has_asm:
		asm = backend.get_asm(target.address)
		if asm is not None:
			checks_run += 1
			if asm.call_count - source_call_count >= call_count_tolerance:
				findings.append(f"ASM call mismatch: disassembly has {asm.call_count}, candidate has {source_call_count}")

	if findings:
		return ObjectiveVerdict(verdict=Verdict.FAIL, summary="Objective verifier found structural mismatches", findings=findings)
	if checks_run == 0:
		return ObjectiveVerdict(verdict=Verdict.UNKNOWN, summary="Objective verifier had insufficient structural data", findings=[])
	return ObjectiveVerdict(verdict=Verdict.PASS, summary="No structural mismatches found", findings=[])


def _extract_body(text: str) -> str:
	open_brace = text.find("{")
	close_brace = text.rfind("}")
	if open_brace == -1 or close_brace == -1 or close_brace <= open_brace:
		return text
	return text[open_brace:close_brace + 1]
