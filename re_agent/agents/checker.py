"""Checker agent."""

from __future__ import annotations

from re_agent.backend.protocol import REBackend
from re_agent.core.models import CheckerVerdict, FunctionTarget, Verdict
from re_agent.llm.protocol import LLMProvider, Message


class CheckerAgent:
	def __init__(self, llm: LLMProvider, backend: REBackend) -> None:
		self.llm = llm
		self.backend = backend
		self.last_prompt = ""
		self.last_response = ""

	def check(self, code: str, target: FunctionTarget) -> CheckerVerdict:
		decompile = self.backend.decompile(target.address)
		self.last_prompt = (
			f"Assess whether this reconstructed function plausibly matches the binary.\n"
			f"Target: {decompile.name or target.function_name} at {target.address}\n"
			f"Reference decompile:\n{decompile.decompiled}\n\n"
			f"Candidate source:\n{code}\n\n"
			"Respond with PASS or FAIL on the first line, then a short summary."
		)
		self.last_response = self.llm.send([
			Message(role="system", content="You conservatively review reverse engineered functions."),
			Message(role="user", content=self.last_prompt),
		])
		return _parse_checker_response(self.last_response)


def _parse_checker_response(text: str) -> CheckerVerdict:
	lines = [line.strip() for line in text.splitlines() if line.strip()]
	first = lines[0].upper() if lines else ""
	if first.startswith("PASS"):
		return CheckerVerdict(verdict=Verdict.PASS, summary="\n".join(lines[1:]) or "PASS", issues=[], fix_instructions=[])
	if first.startswith("FAIL"):
		summary = lines[1] if len(lines) > 1 else "FAIL"
		issues = [line[2:].strip() if line.startswith("- ") else line for line in lines[2:]]
		return CheckerVerdict(verdict=Verdict.FAIL, summary=summary, issues=issues, fix_instructions=issues)
	issues = [line[2:].strip() if line.startswith("- ") else line for line in lines[1:]]
	return CheckerVerdict(verdict=Verdict.UNKNOWN, summary=lines[0] if lines else "No checker response", issues=issues, fix_instructions=issues)
