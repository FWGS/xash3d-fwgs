"""LLM provider factory."""

from __future__ import annotations

from re_agent.config.schema import LLMConfig
from re_agent.llm.stub import StubLLMProvider


def create_provider(config: LLMConfig):
	if config.provider == "stub":
		return StubLLMProvider()
	if config.provider in {"codex", "claude", "openai", "openai-compat"}:
		raise RuntimeError(f"LLM provider '{config.provider}' is declared but not wired in this repo yet; use llm.provider=stub")
	raise RuntimeError(f"Unsupported LLM provider: {config.provider}")
