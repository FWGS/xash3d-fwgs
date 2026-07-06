"""Configuration loading with YAML/JSON, env, and CLI overrides."""

from __future__ import annotations

import dataclasses
import json
import os
from pathlib import Path
from typing import Any, TypeVar

from re_agent.config.schema import (
	BackendConfig,
	LLMConfig,
	OrchestratorConfig,
	OutputConfig,
	ParityConfig,
	ProjectProfile,
	ReAgentConfig,
)


def _load_structured_file(path: Path) -> dict[str, Any]:
	text = path.read_text(encoding="utf-8")
	try:
		import yaml  # type: ignore[import-untyped]
	except ImportError:
		yaml = None  # type: ignore[assignment]

	if yaml is not None:
		data = yaml.safe_load(text)
	else:
		data = json.loads(text)

	if data is None:
		return {}
	if not isinstance(data, dict):
		raise ValueError(f"Expected a mapping in {path}")
	return data


def _apply_env_overrides(raw: dict[str, Any]) -> dict[str, Any]:
	mappings: list[tuple[str, str, type]] = [
		("RE_AGENT_LLM_PROVIDER", "llm.provider", str),
		("RE_AGENT_LLM_API_KEY", "llm.api_key", str),
		("RE_AGENT_LLM_MODEL", "llm.model", str),
		("RE_AGENT_LLM_BASE_URL", "llm.base_url", str),
		("RE_AGENT_BACKEND_TYPE", "backend.type", str),
		("RE_AGENT_BACKEND_CLI_PATH", "backend.cli_path", str),
		("RE_AGENT_BACKEND_TIMEOUT", "backend.timeout_s", int),
		("RE_AGENT_BACKEND_FIXTURES_DIR", "backend.fixtures_dir", str),
	]
	for env_var, dotted_key, cast in mappings:
		value = os.environ.get(env_var)
		if value is None:
			continue
		_apply_dotted(raw, dotted_key, cast(value))
	return raw


def _apply_dotted(raw: dict[str, Any], dotted_key: str, value: Any) -> None:
	current = raw
	parts = dotted_key.split(".")
	for part in parts[:-1]:
		next_value = current.get(part)
		if not isinstance(next_value, dict):
			next_value = {}
			current[part] = next_value
		current = next_value
	current[parts[-1]] = value


def _coerce(field_type: object, value: Any) -> Any:
	type_name = field_type if isinstance(field_type, str) else getattr(field_type, "__name__", str(field_type))
	if value is None:
		return None
	if "bool" in type_name and not isinstance(value, bool):
		if isinstance(value, str):
			return value.lower() in {"1", "true", "yes", "on"}
		return bool(value)
	if "int" in type_name and not isinstance(value, int):
		try:
			return int(value)
		except (TypeError, ValueError):
			return value
	if "float" in type_name and not isinstance(value, (int, float)):
		try:
			return float(value)
		except (TypeError, ValueError):
			return value
	return value


_T = TypeVar("_T")


def _build_dataclass(cls: type[_T], raw: dict[str, Any]) -> _T:
	fields = {field.name: field for field in dataclasses.fields(cls)}  # type: ignore[arg-type]
	kwargs: dict[str, Any] = {}
	for key, value in raw.items():
		if key in fields:
			kwargs[key] = _coerce(fields[key].type, value)
	return cls(**kwargs)


def load_config(yaml_path: Path | None = None, cli_overrides: dict[str, Any] | None = None) -> ReAgentConfig:
	raw: dict[str, Any] = {}
	if yaml_path is not None:
		if not yaml_path.exists():
			raise FileNotFoundError(f"Config file not found: {yaml_path}")
		raw = _load_structured_file(yaml_path)
	else:
		default_path = Path("re-agent.yaml")
		if default_path.exists():
			raw = _load_structured_file(default_path)

	raw = _apply_env_overrides(raw)
	if cli_overrides:
		for key, value in cli_overrides.items():
			_apply_dotted(raw, key, value)

	return ReAgentConfig(
		project_profile=_build_dataclass(ProjectProfile, raw.get("project_profile", {})),
		llm=_build_dataclass(LLMConfig, raw.get("llm", {})),
		backend=_build_dataclass(BackendConfig, raw.get("backend", {})),
		parity=_build_dataclass(ParityConfig, raw.get("parity", {})),
		orchestrator=_build_dataclass(OrchestratorConfig, raw.get("orchestrator", {})),
		output=_build_dataclass(OutputConfig, raw.get("output", {})),
	)
