"""Source context retrieval."""

from __future__ import annotations

from pathlib import Path

from re_agent.config.schema import ProjectProfile
from re_agent.core.models import FunctionTarget
from re_agent.parity.source_indexer import SourceIndexer


def gather_source_context(
	target: FunctionTarget,
	source_root: Path | None,
	profile: ProjectProfile | None,
	indexer: SourceIndexer | None = None,
) -> str:
	if source_root is None or profile is None or not source_root.exists():
		return ""
	local_indexer = indexer or SourceIndexer(source_root, profile)
	match = local_indexer.find(target.class_name, target.function_name)
	if match is None:
		return ""
	return f"Nearby source from {match.path}:{match.line}\n{match.body}\n"
