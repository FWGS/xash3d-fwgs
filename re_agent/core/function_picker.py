"""Function selection helpers."""

from __future__ import annotations

from re_agent.backend.protocol import REBackend
from re_agent.core.models import FunctionTarget
from re_agent.core.session import Session


def pick_next(class_name: str, backend: REBackend, session: Session) -> FunctionTarget | None:
	candidates = backend.remaining(class_name)
	filtered = [entry for entry in candidates if not session.is_completed(entry.address)]
	if not filtered:
		return None
	best = max(filtered, key=lambda entry: entry.caller_count)
	name = best.name
	if "::" in name:
		_, _, name = name.rpartition("::")
	return FunctionTarget(
		address=best.address,
		class_name=best.class_name or class_name,
		function_name=name,
		caller_count=best.caller_count,
	)
