"""Bounded reverser/checker loop."""

from __future__ import annotations

import json
import time
from pathlib import Path

from re_agent.agents.checker import CheckerAgent
from re_agent.agents.reverser import ReverserAgent
from re_agent.backend.protocol import REBackend
from re_agent.config.schema import ProjectProfile
from re_agent.core.models import CheckerVerdict, FunctionTarget, ObjectiveVerdict, ReversalResult, Verdict
from re_agent.core.session import Session
from re_agent.llm.protocol import LLMProvider
from re_agent.parity.source_indexer import SourceIndexer
from re_agent.verification.objective import verify_candidate


def run_fix_loop(
	target: FunctionTarget,
	backend: REBackend,
	reverser_llm: LLMProvider,
	checker_llm: LLMProvider | None = None,
	max_rounds: int = 4,
	log_dir: Path | None = None,
	source_root: Path | None = None,
	project_profile: ProjectProfile | None = None,
	indexer: SourceIndexer | None = None,
	session: Session | None = None,
	report_dir: Path | None = None,
	objective_verifier_enabled: bool = True,
	objective_call_count_tolerance: int = 3,
	objective_control_flow_tolerance: int = 2,
) -> ReversalResult:
	if checker_llm is None:
		checker_llm = reverser_llm
	reverser = ReverserAgent(reverser_llm, backend, source_root, project_profile, indexer, session, report_dir)
	checker = CheckerAgent(checker_llm, backend)

	if log_dir is not None:
		log_dir.mkdir(parents=True, exist_ok=True)

	code = ""
	last_verdict: CheckerVerdict | None = None
	last_objective: ObjectiveVerdict | None = None

	for round_num in range(1, max_rounds + 1):
		if round_num == 1:
			code, phase = reverser.reverse(target)
		else:
			assert last_verdict is not None
			code, phase = reverser.fix(
				last_verdict.summary,
				last_verdict.issues,
				last_verdict.fix_instructions,
				target,
				last_objective.findings if last_objective else None,
			)

		timestamp = time.strftime("%Y%m%d-%H%M%S")
		if log_dir is not None:
			(log_dir / f"round{round_num}-{timestamp}-{phase}.json").write_text(json.dumps({
				"round": round_num,
				"phase": phase,
				"prompt": reverser.last_prompt,
				"response": reverser.last_response,
			}, indent=2), encoding="utf-8")

		last_verdict = checker.check(code, target)
		last_objective = verify_candidate(
			code,
			target,
			backend,
			call_count_tolerance=objective_call_count_tolerance,
			control_flow_tolerance=objective_control_flow_tolerance,
		) if objective_verifier_enabled else None

		if log_dir is not None:
			(log_dir / f"round{round_num}-{timestamp}-checker.json").write_text(json.dumps({
				"round": round_num,
				"prompt": checker.last_prompt,
				"response": checker.last_response,
				"verdict": last_verdict.verdict.value,
				"objective_verdict": last_objective.verdict.value if last_objective else None,
			}, indent=2), encoding="utf-8")

		if last_verdict.verdict == Verdict.PASS and (last_objective is None or last_objective.verdict != Verdict.FAIL):
			return ReversalResult(
				target=target,
				code=code,
				checker_verdict=last_verdict,
				objective_verdict=last_objective,
				rounds_used=round_num,
				success=True,
			)

	return ReversalResult(
		target=target,
		code=code,
		checker_verdict=last_verdict,
		objective_verdict=last_objective,
		rounds_used=max_rounds,
		success=False,
	)
