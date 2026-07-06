"""Result formatting."""

from __future__ import annotations

from re_agent.core.models import ParityStatus, ReversalResult


def format_result(result: ReversalResult) -> str:
	lines = [
		f"Target: {result.target.class_name}::{result.target.function_name} ({result.target.address})".rstrip(":"),
		f"Status: {'PASS' if result.success else 'FAIL'} after {result.rounds_used} round(s)",
	]
	if result.checker_verdict is not None:
		lines.append(f"Checker: {result.checker_verdict.verdict.value} - {result.checker_verdict.summary}")
	if result.objective_verdict is not None:
		lines.append(f"Objective: {result.objective_verdict.verdict.value} - {result.objective_verdict.summary}")
	if result.parity_status is not None:
		status = result.parity_status.value if isinstance(result.parity_status, ParityStatus) else str(result.parity_status)
		lines.append(f"Parity: {status}")
	if result.parity_findings:
		lines.append("Findings:")
		for finding in result.parity_findings:
			lines.append(f"- [{finding.level}] {finding.reason}")
	return "\n".join(lines)
