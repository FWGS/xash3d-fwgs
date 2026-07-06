"""Class-level orchestration."""

from __future__ import annotations

from pathlib import Path

from re_agent.backend.protocol import REBackend
from re_agent.config.schema import ReAgentConfig
from re_agent.core.function_picker import pick_next
from re_agent.core.models import ReversalResult
from re_agent.core.session import Session
from re_agent.llm.protocol import LLMProvider
from re_agent.orchestrator.single import reverse_single
from re_agent.parity.source_indexer import SourceIndexer


def reverse_class(
	class_name: str,
	config: ReAgentConfig,
	backend: REBackend,
	llm: LLMProvider,
	session: Session | None = None,
	max_functions: int | None = None,
) -> list[ReversalResult]:
	local_session = session or Session(config.output.session_file)
	limit = max_functions or config.orchestrator.max_functions_per_class
	indexer = None
	source_root = Path(config.project_profile.source_root)
	if config.parity.enabled and source_root.exists():
		indexer = SourceIndexer(source_root, config.project_profile)
	results: list[ReversalResult] = []
	for _ in range(limit):
		target = pick_next(class_name, backend, local_session)
		if target is None:
			break
		results.append(reverse_single(target, config, backend, llm, local_session, indexer=indexer))
	return results
