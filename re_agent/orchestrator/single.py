"""Single-function orchestration."""

from __future__ import annotations

from pathlib import Path

from re_agent.agents.loop import run_fix_loop
from re_agent.backend.protocol import REBackend
from re_agent.config.schema import ReAgentConfig
from re_agent.core.models import FunctionTarget, HookEntry, ReversalResult
from re_agent.core.session import Session
from re_agent.llm.protocol import LLMProvider
from re_agent.parity.engine import fetch_ghidra_data, score_single
from re_agent.parity.source_indexer import SourceIndexer


def reverse_single(
	target: FunctionTarget,
	config: ReAgentConfig,
	backend: REBackend,
	llm: LLMProvider,
	session: Session | None = None,
	output_dir: Path | None = None,
	indexer: SourceIndexer | None = None,
) -> ReversalResult:
	result = run_fix_loop(
		target=target,
		backend=backend,
		reverser_llm=llm,
		checker_llm=llm,
		max_rounds=config.orchestrator.max_review_rounds,
		log_dir=Path(config.output.log_dir),
		source_root=Path(config.project_profile.source_root),
		project_profile=config.project_profile,
		indexer=indexer,
		session=session,
		report_dir=Path(config.output.report_dir),
		objective_verifier_enabled=config.orchestrator.objective_verifier_enabled,
		objective_call_count_tolerance=config.orchestrator.objective_call_count_tolerance,
		objective_control_flow_tolerance=config.orchestrator.objective_control_flow_tolerance,
	)

	if result.code:
		code_dir = output_dir or (Path(config.output.report_dir) / "code")
		code_dir.mkdir(parents=True, exist_ok=True)
		safe_name = f"{target.address}_{target.class_name}_{target.function_name}.cpp".replace("::", "_").replace("/", "_")
		(code_dir / safe_name).write_text(result.code, encoding="utf-8")

	if config.parity.enabled and result.code:
		local_indexer = indexer or SourceIndexer(Path(config.project_profile.source_root), config.project_profile)
		source = local_indexer.find(target.class_name, target.function_name)
		ghidra = fetch_ghidra_data(target.address, backend) if backend.capabilities.has_decompile else None
		status, findings = score_single(_target_to_hook(target), source, ghidra, config.parity)
		result.parity_status = status
		result.parity_findings = findings

	if session is not None:
		session.record_result(result)
	return result


def _target_to_hook(target: FunctionTarget) -> HookEntry:
	return HookEntry(
		class_path=target.class_name,
		fn_name=target.function_name,
		address=target.address,
		reversed=True,
		locked=False,
		is_virtual=False,
	)
