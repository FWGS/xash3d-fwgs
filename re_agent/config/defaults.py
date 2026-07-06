"""Default config templates."""

from __future__ import annotations

import json

from re_agent.config.schema import ReAgentConfig


def _as_serializable() -> dict[str, object]:
	config = ReAgentConfig.create_default()
	return {
		"project_profile": {
			"hook_patterns": config.project_profile.hook_patterns,
			"stub_markers": config.project_profile.stub_markers,
			"stub_call_prefix": config.project_profile.stub_call_prefix,
			"class_macro": config.project_profile.class_macro,
			"source_root": config.project_profile.source_root,
			"source_extensions": config.project_profile.source_extensions,
			"hooks_csv": config.project_profile.hooks_csv,
		},
		"llm": {
			"provider": config.llm.provider,
			"model": config.llm.model,
			"api_key": config.llm.api_key,
			"base_url": config.llm.base_url,
			"max_tokens": config.llm.max_tokens,
			"temperature": config.llm.temperature,
			"timeout_s": config.llm.timeout_s,
		},
		"backend": {
			"type": config.backend.type,
			"cli_path": config.backend.cli_path,
			"timeout_s": config.backend.timeout_s,
			"fixtures_dir": config.backend.fixtures_dir,
		},
		"parity": {
			"enabled": config.parity.enabled,
			"call_count_warn_diff": config.parity.call_count_warn_diff,
			"inline_wrapper_autoskip": config.parity.inline_wrapper_autoskip,
		},
		"orchestrator": {
			"max_review_rounds": config.orchestrator.max_review_rounds,
			"max_functions_per_class": config.orchestrator.max_functions_per_class,
			"objective_verifier_enabled": config.orchestrator.objective_verifier_enabled,
			"objective_call_count_tolerance": config.orchestrator.objective_call_count_tolerance,
			"objective_control_flow_tolerance": config.orchestrator.objective_control_flow_tolerance,
		},
		"output": {
			"report_dir": config.output.report_dir,
			"log_dir": config.output.log_dir,
			"session_file": config.output.session_file,
			"format": config.output.format,
		},
	}


DEFAULT_CONFIG_YAML = json.dumps(_as_serializable(), indent=2) + "\n"
