"""LLM provider protocol."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol, runtime_checkable


@dataclass
class Message:
	role: str
	content: str


@runtime_checkable
class LLMProvider(Protocol):
	def send(self, messages: list[Message], **kwargs: Any) -> str: ...

	@property
	def supports_conversations(self) -> bool: ...

	def new_conversation(self, system: str) -> str: ...

	def resume(self, conversation_id: str, message: str) -> str: ...
