"""Configuration schema dataclasses for re-agent."""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class ProjectProfile:
	hook_patterns: list[str] = field(default_factory=lambda: [
		r"RH_ScopedInstall\s*\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)",
		r"HOOK_(?:METHOD|FUNC)\s*\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)",
	])
	stub_markers: list[str] = field(default_factory=lambda: [
		"NOT_IMPLEMENTED",
		"NOTSA_UNREACHABLE",
		"TODO",
	])
	stub_call_prefix: str = "plugin::Call"
	class_macro: str = "RH_ScopedClass"
	source_root: str = "."
	source_extensions: list[str] = field(default_factory=lambda: [".c", ".cc", ".cpp", ".h", ".hpp"])
	hooks_csv: str | None = None


@dataclass
class LLMConfig:
	provider: str = "stub"
	model: str = "re-agent-stub"
	api_key: str | None = None
	base_url: str | None = None
	max_tokens: int = 4096
	temperature: float = 0.0
	timeout_s: int = 1800


@dataclass
class BackendConfig:
	type: str = "stub"
	cli_path: str = "ghidra"
	timeout_s: int = 45
	fixtures_dir: str | None = None


@dataclass
class ParityConfig:
	enabled: bool = True
	call_count_warn_diff: int = 3
	inline_wrapper_autoskip: bool = False


@dataclass
class OrchestratorConfig:
	max_review_rounds: int = 4
	max_functions_per_class: int = 10
	objective_verifier_enabled: bool = True
	objective_call_count_tolerance: int = 3
	objective_control_flow_tolerance: int = 2


@dataclass
class OutputConfig:
	report_dir: str = "reports/re-agent"
	log_dir: str = "reports/re-agent/logs"
	session_file: str = "re-agent-progress.json"
	format: str = "json"


@dataclass
class ReAgentConfig:
	project_profile: ProjectProfile = field(default_factory=ProjectProfile)
	llm: LLMConfig = field(default_factory=LLMConfig)
	backend: BackendConfig = field(default_factory=BackendConfig)
	parity: ParityConfig = field(default_factory=ParityConfig)
	orchestrator: OrchestratorConfig = field(default_factory=OrchestratorConfig)
	output: OutputConfig = field(default_factory=OutputConfig)

	@classmethod
	def create_default(cls) -> "ReAgentConfig":
		return cls()
