"""Reverser agent."""

from __future__ import annotations

from pathlib import Path

from re_agent.agents.source_context import gather_source_context
from re_agent.backend.protocol import REBackend
from re_agent.config.schema import ProjectProfile
from re_agent.core.models import FunctionTarget
from re_agent.core.session import Session
from re_agent.llm.protocol import LLMProvider, Message
from re_agent.parity.source_indexer import SourceIndexer


class ReverserAgent:
	def __init__(
		self,
		llm: LLMProvider,
		backend: REBackend,
		source_root: Path | None = None,
		project_profile: ProjectProfile | None = None,
		indexer: SourceIndexer | None = None,
		session: Session | None = None,
		report_dir: Path | None = None,
	) -> None:
		self.llm = llm
		self.backend = backend
		self.source_root = source_root
		self.project_profile = project_profile
		self.indexer = indexer
		self.session = session
		self.report_dir = report_dir
		self.last_prompt = ""
		self.last_response = ""

	def reverse(self, target: FunctionTarget) -> tuple[str, str]:
		decompile = self.backend.decompile(target.address)
		source_context = gather_source_context(target, self.source_root, self.project_profile, self.indexer)
		self.last_prompt = (
			f"Reverse engineer {decompile.name or target.function_name} at {target.address}.\n"
			f"Signature: {decompile.signature}\n"
			f"Decompile:\n{decompile.decompiled}\n\n"
			f"{source_context}"
			"Produce a C or C++ implementation only."
		)
		self.last_response = self.llm.send([
			Message(role="system", content="You reverse engineer binary functions into readable source."),
			Message(role="user", content=self.last_prompt),
		])
		return self.last_response, "reverse"

	def fix(
		self,
		checker_report: str,
		issues: list[str],
		fix_instructions: list[str],
		target: FunctionTarget,
		objective_findings: list[str] | None = None,
	) -> tuple[str, str]:
		decompile = self.backend.decompile(target.address)
		self.last_prompt = (
			f"Revise the function for {decompile.name or target.function_name} at {target.address}.\n"
			f"Decompile:\n{decompile.decompiled}\n\n"
			f"Checker summary: {checker_report}\n"
			f"Issues:\n- " + "\n- ".join(issues or ["none"]) + "\n"
			f"Fix instructions:\n- " + "\n- ".join(fix_instructions or ["none"]) + "\n"
			f"Objective findings:\n- " + "\n- ".join(objective_findings or ["none"]) + "\n"
			"Return improved code only."
		)
		self.last_response = self.llm.send([
			Message(role="system", content="You repair reverse engineered code to better match the binary structure."),
			Message(role="user", content=self.last_prompt),
		])
		return self.last_response, "fix"
