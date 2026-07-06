"""Session progress views."""

from __future__ import annotations

from re_agent.core.session import Session


class ProgressTracker:
	def __init__(self, session: Session) -> None:
		self.session = session

	def print_summary(self) -> str:
		summary = self.session.get_summary()
		return (
			f"Functions: {summary['total_functions']}\n"
			f"Passed: {summary['passed']}\n"
			f"Failed: {summary['failed']}\n"
			f"Classes touched: {summary['classes_touched']}"
		)

	def print_class_summary(self, class_name: str) -> str:
		summary = self.session.get_class_summary(class_name)
		return f"{class_name}: {summary['passed']}/{summary['total']} passed, {summary['failed']} failed"

	def get_function_table(self, class_name: str | None = None) -> list[dict[str, str | int]]:
		rows: list[dict[str, str | int]] = []
		for entry in self.session.get_all_functions():
			if class_name and entry.get("class_name") != class_name:
				continue
			rows.append({
				"address": entry.get("address", ""),
				"class": entry.get("class_name", ""),
				"function": entry.get("function_name", ""),
				"status": "PASS" if entry.get("success") else "FAIL",
				"rounds": int(entry.get("rounds_used", 0)),
				"timestamp": entry.get("timestamp", ""),
			})
		rows.sort(key=lambda row: str(row["address"]))
		return rows
