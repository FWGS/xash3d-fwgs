"""Persistent JSON session state."""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any

from re_agent.core.models import ReversalResult
from re_agent.utils.address import normalize_address


class Session:
	def __init__(self, path: str | Path = "re-agent-progress.json") -> None:
		self.path = Path(path)
		self._data: dict[str, Any] = {"functions": {}, "runs": []}
		if self.path.exists():
			self.load()

	def load(self) -> None:
		try:
			self._data = json.loads(self.path.read_text(encoding="utf-8"))
		except (OSError, json.JSONDecodeError):
			self._data = {"functions": {}, "runs": []}

	def save(self) -> None:
		self.path.parent.mkdir(parents=True, exist_ok=True)
		tmp = self.path.with_suffix(".tmp")
		tmp.write_text(json.dumps(self._data, indent=2), encoding="utf-8")
		tmp.replace(self.path)

	def record_result(self, result: ReversalResult) -> None:
		entry = {
			"address": result.target.address,
			"class_name": result.target.class_name,
			"function_name": result.target.function_name,
			"success": result.success,
			"rounds_used": result.rounds_used,
			"verdict": result.checker_verdict.verdict.value if result.checker_verdict else None,
			"parity_status": result.parity_status.value if result.parity_status else None,
			"timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
		}
		self._data["functions"][normalize_address(result.target.address)] = entry
		self._data["runs"].append(entry)
		self.save()

	def is_completed(self, address: str) -> bool:
		entry = self._data["functions"].get(normalize_address(address))
		return bool(entry and entry.get("success"))

	def is_attempted(self, address: str) -> bool:
		return normalize_address(address) in self._data["functions"]

	def get_summary(self) -> dict[str, Any]:
		functions = self._data["functions"]
		total = len(functions)
		passed = sum(1 for entry in functions.values() if entry.get("success"))
		return {
			"total_functions": total,
			"passed": passed,
			"failed": total - passed,
			"classes_touched": len({entry.get("class_name", "") for entry in functions.values() if entry.get("class_name")}),
		}

	def get_class_summary(self, class_name: str) -> dict[str, int]:
		total = passed = failed = 0
		for entry in self._data["functions"].values():
			if entry.get("class_name") != class_name:
				continue
			total += 1
			if entry.get("success"):
				passed += 1
			else:
				failed += 1
		return {"total": total, "passed": passed, "failed": failed}

	def get_all_functions(self) -> list[dict[str, Any]]:
		return list(self._data["functions"].values())
