"""Parity scoring."""

from __future__ import annotations

from re_agent.core.models import Finding, ParityStatus


def score(findings: list[Finding]) -> ParityStatus:
	if any(finding.level == "red" for finding in findings):
		return ParityStatus.RED
	if any(finding.level == "yellow" for finding in findings):
		return ParityStatus.YELLOW
	return ParityStatus.GREEN
