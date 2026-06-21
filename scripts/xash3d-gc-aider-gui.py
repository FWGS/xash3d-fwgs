#!/usr/bin/env python3
"""PyQt6 control panel for supervised Aider work on the Xash3D GameCube port."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from PyQt6.QtCore import QProcess, QProcessEnvironment, QTimer
from PyQt6.QtGui import QFont, QFontDatabase, QTextCursor
from PyQt6.QtWidgets import (
	QApplication,
	QFileDialog,
	QFormLayout,
	QGroupBox,
	QHBoxLayout,
	QLabel,
	QLineEdit,
	QMainWindow,
	QMessageBox,
	QPlainTextEdit,
	QProgressBar,
	QPushButton,
	QSpinBox,
	QVBoxLayout,
	QWidget,
)

DEFAULT_REPO = Path(__file__).resolve().parents[1]
STATE_DIR = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "xash3d-gc-aider"
TASK_FILE = STATE_DIR / "current-task.md"

GC_PRIMARY = "#6b5cb1"
GC_BG = "#2a2448"
GC_PANEL = "#3d3568"
GC_INPUT = "#1a1630"
GC_TEXT = "#f0ecff"
GC_ACCENT = "#8a7bc9"
GC_PROGRESS = "#5ec8e8"

DEFAULT_TASK = """Continue the Xash3D GameCube port.

Read docs/GAMECUBE_PORT_PLAN.md, the latest Git log and diff, and the
read-only project rules. Do exactly one useful, low-risk patch toward the next
documented blocker.

- Keep the patch small and limited to one demonstrated blocker.
- Do not break existing targets.
- Prefer platform isolation and existing abstractions.
- Update docs/GAMECUBE_PORT_PLAN.md with evidence and the next blocker.
- Stop after one patch.
"""


def load_dotenv(path: Path) -> dict[str, str]:
	values: dict[str, str] = {}
	if not path.is_file():
		return values
	for raw in path.read_text(encoding="utf-8").splitlines():
		line = raw.strip()
		if not line or line.startswith("#") or "=" not in line:
			continue
		key, value = line.split("=", 1)
		values[key.strip()] = value.strip().strip("\"'")
	return values


def stylesheet() -> str:
	return f"""
	QMainWindow, QWidget {{ background: {GC_BG}; color: {GC_TEXT}; }}
	QGroupBox {{ background: {GC_PANEL}; border: 2px solid {GC_ACCENT};
		border-radius: 10px; margin-top: 13px; padding: 11px; font-weight: bold; }}
	QGroupBox::title {{ subcontrol-origin: margin; left: 12px; padding: 0 7px; color: {GC_ACCENT}; }}
	QLineEdit, QSpinBox, QPlainTextEdit {{ background: {GC_INPUT}; color: {GC_TEXT};
		border: 1px solid {GC_ACCENT}; border-radius: 6px; padding: 6px; }}
	QPushButton {{ background: {GC_PRIMARY}; color: {GC_TEXT}; border: 1px solid {GC_ACCENT};
		border-radius: 12px; padding: 8px 14px; font-weight: bold; }}
	QPushButton:hover {{ background: #7d6ec4; }}
	QPushButton:disabled {{ background: #4f4580; color: #b8aed8; }}
	QProgressBar {{ background: {GC_INPUT}; border: 1px solid {GC_ACCENT}; border-radius: 7px;
		text-align: center; color: {GC_TEXT}; height: 20px; }}
	QProgressBar::chunk {{ background: {GC_PROGRESS}; border-radius: 6px; }}
	"""


class PortWindow(QMainWindow):
	def __init__(self) -> None:
		super().__init__()
		self.setWindowTitle("Xash3D → GameCube — Aider Port Harness")
		self.resize(1050, 850)
		self.process: QProcess | None = None
		self.operation = ""
		self.expected_passes = 1

		central = QWidget()
		self.setCentralWidget(central)
		layout = QVBoxLayout(central)

		project_box = QGroupBox("Port workspace")
		form = QFormLayout(project_box)
		self.repo_edit = QLineEdit(str(DEFAULT_REPO))
		browse = QPushButton("Browse…")
		browse.clicked.connect(self.pick_repo)
		repo_row = QHBoxLayout()
		repo_row.addWidget(self.repo_edit)
		repo_row.addWidget(browse)
		form.addRow("Xash3D repository:", repo_row)

		env = load_dotenv(DEFAULT_REPO / ".env")
		self.base_url_edit = QLineEdit(env.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1"))
		self.key_edit = QLineEdit(env.get("OPENAI_API_KEY", ""))
		self.key_edit.setEchoMode(QLineEdit.EchoMode.Password)
		self.model_edit = QLineEdit("openai/qwen-local")
		form.addRow("OpenAI-compatible URL:", self.base_url_edit)
		form.addRow("API key:", self.key_edit)
		form.addRow("Aider model:", self.model_edit)
		layout.addWidget(project_box)

		task_box = QGroupBox("Next bounded GameCube port task (one-pass mode)")
		task_layout = QVBoxLayout(task_box)
		self.task_edit = QPlainTextEdit()
		self.task_edit.setPlainText(TASK_FILE.read_text(encoding="utf-8") if TASK_FILE.is_file() else DEFAULT_TASK)
		self.task_edit.setMaximumHeight(190)
		task_layout.addWidget(self.task_edit)
		layout.addWidget(task_box)

		controls = QHBoxLayout()
		self.one_btn = QPushButton("Run one Aider pass")
		self.one_btn.clicked.connect(self.run_one_pass)
		self.passes_spin = QSpinBox()
		self.passes_spin.setRange(1, 20)
		self.passes_spin.setValue(3)
		self.loop_btn = QPushButton("Run guarded loop")
		self.loop_btn.clicked.connect(self.run_loop)
		self.stop_btn = QPushButton("Stop")
		self.stop_btn.clicked.connect(self.stop_process)
		self.stop_btn.setEnabled(False)
		controls.addWidget(self.one_btn)
		controls.addWidget(QLabel("Passes:"))
		controls.addWidget(self.passes_spin)
		controls.addWidget(self.loop_btn)
		controls.addWidget(self.stop_btn)
		layout.addLayout(controls)

		tools = QHBoxLayout()
		for label, command in (
			("Verify", ["scripts/ai-verify.sh"]),
			("Review HEAD", ["scripts/ai-review.sh"]),
			("Build DOL", ["scripts/build-gamecube.sh"]),
			("Build disc ISO", ["scripts/build-gamecube-disc.py", "--output", "OUT/xash3d-gc.iso"]),
		):
			button = QPushButton(label)
			button.clicked.connect(lambda _checked=False, c=command, n=label: self.start(c, n))
			tools.addWidget(button)
		self.dolphin_btn = QPushButton("Boot ISO in Dolphin")
		self.dolphin_btn.clicked.connect(self.boot_dolphin)
		tools.addWidget(self.dolphin_btn)
		layout.addLayout(tools)

		self.status_label = QLabel("Idle")
		layout.addWidget(self.status_label)
		self.progress = QProgressBar()
		self.progress.setRange(0, 1)
		self.progress.setValue(0)
		self.progress.setFormat("No automation running")
		layout.addWidget(self.progress)

		self.log = QPlainTextEdit()
		self.log.setReadOnly(True)
		self.log.setMaximumBlockCount(10000)
		layout.addWidget(self.log, 1)

		self.timer = QTimer(self)
		self.timer.setInterval(1500)
		self.timer.timeout.connect(self.refresh_artifacts)
		self.timer.start()
		self.refresh_artifacts()

	def repo(self) -> Path:
		return Path(self.repo_edit.text().strip()).expanduser().resolve()

	def valid_repo(self) -> bool:
		root = self.repo()
		ok = (root / ".git").exists() and (root / "scripts/ai-aider-pass.sh").is_file()
		if not ok:
			QMessageBox.warning(self, "Invalid repository", "Select the Xash3D GameCube repository root.")
		return ok

	def pick_repo(self) -> None:
		path = QFileDialog.getExistingDirectory(self, "Select Xash3D repository", str(self.repo()))
		if path:
			self.repo_edit.setText(path)

	def process_environment(self) -> QProcessEnvironment:
		env = QProcessEnvironment.systemEnvironment()
		env.insert("OPENAI_API_BASE", self.base_url_edit.text().strip())
		env.insert("OPENAI_API_KEY", self.key_edit.text())
		env.insert("AIDER_MODEL", self.model_edit.text().strip())
		return env

	def append(self, text: str) -> None:
		self.log.moveCursor(QTextCursor.MoveOperation.End)
		self.log.insertPlainText(text)
		self.log.moveCursor(QTextCursor.MoveOperation.End)

	def start(self, command: list[str], operation: str, passes: int = 1) -> None:
		if self.process is not None or not self.valid_repo():
			return
		self.operation = operation
		self.expected_passes = passes
		self.progress.setRange(0, passes)
		self.progress.setValue(0)
		self.progress.setFormat(f"{operation}: %v / %m")
		self.status_label.setText(f"Running: {operation}")
		self.one_btn.setEnabled(False)
		self.loop_btn.setEnabled(False)
		self.stop_btn.setEnabled(True)
		self.append(f"\n\n$ {' '.join(command)}\n")

		proc = QProcess(self)
		proc.setWorkingDirectory(str(self.repo()))
		proc.setProcessEnvironment(self.process_environment())
		proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
		proc.readyReadStandardOutput.connect(self.read_output)
		proc.finished.connect(self.finished)
		proc.errorOccurred.connect(lambda error: self.append(f"\nProcess error: {error.name}\n"))
		self.process = proc
		proc.start(command[0], command[1:])

	def run_one_pass(self) -> None:
		if not self.key_edit.text().strip():
			QMessageBox.warning(self, "API key missing", "Enter the vLLM API key before running Aider.")
			return
		STATE_DIR.mkdir(parents=True, exist_ok=True)
		TASK_FILE.write_text(self.task_edit.toPlainText().rstrip() + "\n", encoding="utf-8")
		self.start(["scripts/ai-aider-pass.sh", str(self.repo()), str(TASK_FILE)], "Aider pass")

	def run_loop(self) -> None:
		if not self.key_edit.text().strip():
			QMessageBox.warning(self, "API key missing", "Enter the vLLM API key before running Aider.")
			return
		passes = self.passes_spin.value()
		self.start(["scripts/ai-loop.sh", str(passes), str(self.repo())], "Guarded Aider loop", passes)

	def read_output(self) -> None:
		if self.process is None:
			return
		text = bytes(self.process.readAllStandardOutput()).decode(errors="replace")
		self.append(text)
		if self.operation == "Guarded Aider loop":
			for line in text.splitlines():
				if line.startswith("AI PASS "):
					try:
						self.progress.setValue(max(0, int(line.split()[2]) - 1))
					except (IndexError, ValueError):
						pass

	def finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
		succeeded = exit_code == 0
		if succeeded:
			self.progress.setValue(self.expected_passes)
		self.status_label.setText(f"{'Passed' if succeeded else 'Failed'}: {self.operation} (exit {exit_code})")
		self.append(f"\n[{self.operation} exited {exit_code}]\n")
		self.process = None
		self.one_btn.setEnabled(True)
		self.loop_btn.setEnabled(True)
		self.stop_btn.setEnabled(False)
		self.refresh_artifacts()

	def stop_process(self) -> None:
		if self.process is None:
			return
		self.append("\nStopping process…\n")
		self.process.terminate()
		QTimer.singleShot(3000, lambda: self.process.kill() if self.process else None)

	def boot_dolphin(self) -> None:
		iso = self.repo() / "OUT/xash3d-gc.iso"
		if not iso.is_file():
			QMessageBox.warning(self, "ISO missing", "Build OUT/xash3d-gc.iso first.")
			return
		if shutil.which("dolphin-emu"):
			command = ["dolphin-emu", "-e", str(iso)]
		elif shutil.which("dolphin"):
			command = ["dolphin", "-e", str(iso)]
		elif shutil.which("flatpak"):
			command = ["flatpak", "run", "org.DolphinEmu.dolphin-emu", "-e", str(iso)]
		else:
			QMessageBox.warning(self, "Dolphin missing", "No Dolphin executable or Flatpak was found.")
			return
		subprocess.Popen(command, cwd=self.repo())
		self.append(f"\nLaunched Dolphin with {iso}\n")

	def refresh_artifacts(self) -> None:
		root = self.repo()
		dol = root / "OUT/bin/boot.dol"
		iso = root / "OUT/xash3d-gc.iso"
		parts = []
		for label, path in (("DOL", dol), ("ISO", iso)):
			parts.append(f"{label}: {path.stat().st_size / (1024 * 1024):.1f} MiB" if path.is_file() else f"{label}: missing")
		if self.process is None:
			self.status_label.setText("Idle — " + " | ".join(parts))


def main() -> int:
	app = QApplication(sys.argv)
	app.setStyleSheet(stylesheet())
	font = QFont("Sans Serif", 10)
	font.setStyleHint(QFont.StyleHint.SansSerif)
	app.setFont(font)
	window = PortWindow()
	window.show()
	return app.exec()


if __name__ == "__main__":
	raise SystemExit(main())
