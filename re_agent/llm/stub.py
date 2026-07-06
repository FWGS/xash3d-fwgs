"""Deterministic stub LLM."""

from __future__ import annotations

from typing import Any

from re_agent.llm.protocol import LLMProvider, Message


class StubLLMProvider(LLMProvider):
	@property
	def supports_conversations(self) -> bool:
		return False

	def send(self, messages: list[Message], **kwargs: Any) -> str:
		last = messages[-1].content if messages else ""
		if "Respond with PASS or FAIL" in last:
			return "PASS\nLooks structurally plausible."
		return (
			"// stub provider output\n"
			"void ReconstructedFunction(void)\n"
			"{\n"
			"\t/* Replace this with project-specific reversed code. */\n"
			"}\n"
		)

	def new_conversation(self, system: str) -> str:
		raise NotImplementedError("Stub provider does not support conversations")

	def resume(self, conversation_id: str, message: str) -> str:
		raise NotImplementedError("Stub provider does not support conversations")
