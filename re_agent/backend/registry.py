"""Backend factory."""

from __future__ import annotations

from re_agent.backend.stub import StubBackend
from re_agent.config.schema import BackendConfig


def create_backend(config: BackendConfig):
	if config.type == "stub":
		return StubBackend(config.fixtures_dir)
	if config.type == "ghidra-bridge":
		raise RuntimeError("ghidra-bridge backend is not wired in this repo yet; use backend.type=stub with fixtures_dir")
	raise RuntimeError(f"Unsupported backend type: {config.type}")
