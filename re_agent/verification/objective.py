"""Objective structural verifier."""

from __future__ import annotations

import re
from re_agent.backend.protocol import REBackend
from re_agent.core.models import FunctionTarget, ObjectiveVerdict, Verdict
from re_agent.utils.text import count_calls, count_control_flow, strip_comments


def verify_candidate(
	code: str,
	target: FunctionTarget,
	backend: REBackend,
	call_count_tolerance: int = 3,
	control_flow_tolerance: int = 2,
	signature_verification: bool = True,
	variable_verification: bool = True,
	type_verification: bool = True,
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
	
	# Check 1: Call count verification
	if decompile.callees is not None:
		checks_run += 1
		if decompile.callees - source_call_count >= call_count_tolerance:
			findings.append(f"Call count mismatch: decompile reports {decompile.callees}, candidate has {source_call_count}")
	
	# Check 2: Control flow verification
	if decompile_flow >= 2:
		checks_run += 1
		if decompile_flow - source_flow_count >= control_flow_tolerance:
			findings.append(f"Control-flow mismatch: decompile has {decompile_flow}, candidate has {source_flow_count}")
	
	# Check 3: ASM call count verification
	if backend.capabilities.has_asm:
		asm = backend.get_asm(target.address)
		if asm is not None:
			checks_run += 1
			if asm.call_count - source_call_count >= call_count_tolerance:
				findings.append(f"ASM call mismatch: disassembly has {asm.call_count}, candidate has {source_call_count}")
	
	# Check 4: Function signature verification
	if signature_verification:
		checks_run += 1
		signature_mismatch = _verify_signature(code, decompile)
		if signature_mismatch:
			findings.append(signature_mismatch)
	
	# Check 5: Variable usage verification
	if variable_verification:
		checks_run += 1
		var_mismatch = _verify_variable_usage(code, decompile)
		if var_mismatch:
			findings.append(var_mismatch)
	
	# Check 6: Data type verification
	if type_verification:
		checks_run += 1
		type_mismatch = _verify_data_types(code, decompile)
		if type_mismatch:
			findings.append(type_mismatch)

	if findings:
		return ObjectiveVerdict(verdict=Verdict.FAIL, summary="Objective verifier found structural mismatches", findings=findings)
	if checks_run == 0:
		return ObjectiveVerdict(verdict=Verdict.UNKNOWN, summary="Objective verifier had insufficient structural data", findings=[])
	return ObjectiveVerdict(verdict=Verdict.PASS, summary="No structural mismatches found", findings=[])


def _verify_signature(code: str, decompile) -> str | None:
	"""Verify function signature matches the original."""
	# Extract function name from the decompiled code
	decompile_name = decompile.name
	# Try to extract function name from the candidate code
	candidate_name_match = re.search(r'(\w+)\s*\([^)]*\)\s*\{', code)
	if candidate_name_match:
		candidate_name = candidate_name_match.group(1)
		if candidate_name != decompile_name and decompile_name:
			return f"Function name mismatch: expected '{decompile_name}', got '{candidate_name}'"
	return None


def _verify_variable_usage(code: str, decompile) -> str | None:
	"""Verify variable usage patterns match the original."""
	# Count local variable declarations in decompiled code
	decompile_vars = len(re.findall(r'\b(\w+)\s*=\s*[^;]+;', decompile.raw_output))
	candidate_vars = len(re.findall(r'\b(\w+)\s*=\s*[^;]+;', code))
	
	# Check if variable count is significantly different
	if decompile_vars > 0 and abs(decompile_vars - candidate_vars) > 2:
		return f"Variable count mismatch: decompile has ~{decompile_vars}, candidate has ~{candidate_vars}"
	return None


def _verify_data_types(code: str, decompile) -> str | None:
	"""Verify data type usage matches the original."""
	# Check for common data types in decompiled code
	type_patterns = [
		(r'\bint\b', 'int'),
		(r'\bfloat\b', 'float'),
		(r'\bvoid\b', 'void'),
		(r'\bchar\b', 'char'),
		(r'\bbool\b', 'bool'),
	]
	
	for pattern, type_name in type_patterns:
		decompile_has_type = bool(re.search(pattern, decompile.raw_output))
		candidate_has_type = bool(re.search(pattern, code))
		
		if decompile_has_type != candidate_has_type:
			return f"Data type mismatch for '{type_name}': decompile uses it, candidate does not (or vice versa)"
	
	return None


def _extract_body(text: str) -> str:
	open_brace = text.find("{")
	close_brace = text.rfind("}")
	if open_brace == -1 or close_brace == -1 or close_brace <= open_brace:
		return text
	return text[open_brace:close_brace + 1]
