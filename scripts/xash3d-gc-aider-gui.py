#!/usr/bin/env python3
"""GameCube-inspired command console for goal-driven Xash3D porting."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

from PyQt6.QtCore import QProcess, Qt, QTimer
from PyQt6.QtGui import QFont, QTextCursor
from PyQt6.QtWidgets import (
	QApplication,
	QFileDialog,
	QFormLayout,
	QFrame,
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

GC_BG = "#171225"
GC_PANEL = "#241a3f"
GC_PANEL_2 = "#31265d"
GC_INPUT = "#140f2a"
GC_TEXT = "#f3f0ff"
GC_MUTED = "#b8aee8"
GC_VIOLET = "#6d5ac9"
GC_BORDER = "#8f7bea"
GC_CYAN = "#62d9ff"
GC_ORANGE = "#ffb14a"
GC_MINT = "#7fffd4"


def stylesheet() -> str:
	return f"""
	QMainWindow, QWidget {{ background: {GC_BG}; color: {GC_TEXT}; }}
	QGroupBox {{ background: {GC_PANEL}; border: 2px solid {GC_BORDER};
		border-radius: 14px; margin-top: 15px; padding: 13px; font-weight: bold; }}
	QGroupBox::title {{ subcontrol-origin: margin; left: 14px; padding: 0 8px; color: {GC_CYAN}; }}
	QLineEdit, QSpinBox, QPlainTextEdit {{ background: {GC_INPUT}; color: {GC_TEXT};
		border: 2px inset {GC_PANEL_2}; border-radius: 8px; padding: 7px; }}
	QLineEdit:focus, QSpinBox:focus, QPlainTextEdit:focus {{ border: 2px solid {GC_CYAN}; }}
	QPushButton {{ background: {GC_VIOLET}; color: {GC_TEXT}; border: 2px outset {GC_BORDER};
		border-radius: 15px; padding: 9px 15px; font-weight: bold; }}
	QPushButton:hover {{ background: #806ee0; border-color: {GC_CYAN}; }}
	QPushButton:pressed {{ background: #4b3c99; border-style: inset; }}
	QPushButton:disabled {{ background: {GC_PANEL_2}; color: {GC_MUTED}; border-color: {GC_PANEL}; }}
	QProgressBar {{ background: {GC_INPUT}; border: 1px solid {GC_CYAN}; border-radius: 8px;
		text-align: center; color: {GC_TEXT}; height: 20px; }}
	QProgressBar::chunk {{ background: {GC_CYAN}; border-radius: 7px; }}
	QLabel#Title {{ color: {GC_TEXT}; font-size: 25px; font-weight: bold; }}
	QLabel#Subtitle {{ color: {GC_CYAN}; font-size: 11px; letter-spacing: 2px; }}
	QLabel#Chip {{ background: {GC_PANEL_2}; color: {GC_MINT}; border: 1px solid {GC_BORDER};
		border-radius: 10px; padding: 5px 10px; font-weight: bold; }}
	QLabel#PipelineIdle {{ background: {GC_PANEL_2}; color: {GC_MUTED}; border: 2px outset {GC_BORDER};
		border-radius: 12px; padding: 12px; font-weight: bold; }}
	QLabel#PipelineRunning {{ background: #17405a; color: {GC_CYAN}; border: 2px solid {GC_CYAN};
		border-radius: 12px; padding: 12px; font-weight: bold; }}
	QLabel#PipelineSuccess {{ background: #174638; color: {GC_MINT}; border: 2px solid {GC_MINT};
		border-radius: 12px; padding: 12px; font-weight: bold; }}
	QLabel#PipelineFailed {{ background: #563621; color: {GC_ORANGE}; border: 2px solid {GC_ORANGE};
		border-radius: 12px; padding: 12px; font-weight: bold; }}
	"""


class PortWindow(QMainWindow):
	def __init__(self) -> None:
		super().__init__()
		self.setWindowTitle("Xash3D → GameCube — Port Command Console")
		self.resize(1180, 880)
		self.process: QProcess | None = None
		self.operation = ""
		self.expected_passes = 1
		self.pending_boot = False
		self.pipeline: dict[str, QLabel] = {}

		central = QWidget()
		self.setCentralWidget(central)
		layout = QVBoxLayout(central)
		layout.setSpacing(10)

		header = QHBoxLayout()
		cube = QLabel("◆")
		cube.setStyleSheet(f"color: {GC_VIOLET}; font-size: 42px; padding-right: 8px;")
		header.addWidget(cube)
		titles = QVBoxLayout()
		title = QLabel("Xash3D → GameCube")
		title.setObjectName("Title")
		subtitle = QLabel("PORT HARNESS / BUILD CONSOLE")
		subtitle.setObjectName("Subtitle")
		titles.addWidget(title)
		titles.addWidget(subtitle)
		header.addLayout(titles)
		header.addStretch()
		self.dol_chip = QLabel("DOL  —")
		self.iso_chip = QLabel("ISO  —")
		self.dolphin_chip = QLabel("DOLPHIN  CHECKING")
		for chip in (self.dol_chip, self.iso_chip, self.dolphin_chip):
			chip.setObjectName("Chip")
			header.addWidget(chip)
		layout.addLayout(header)

		project_box = QGroupBox("MEMORY CARD SLOT A  /  PORT WORKSPACE")
		form = QFormLayout(project_box)
		self.repo_edit = QLineEdit(str(DEFAULT_REPO))
		browse = QPushButton("Browse…")
		browse.clicked.connect(self.pick_repo)
		repo_row = QHBoxLayout()
		repo_row.addWidget(self.repo_edit)
		repo_row.addWidget(browse)
		form.addRow("Xash3D repository:", repo_row)

		layout.addWidget(project_box)

		controls = QHBoxLayout()
		self.passes_spin = QSpinBox()
		self.passes_spin.setRange(1, 100)
		self.passes_spin.setValue(20)
		self.loop_btn = QPushButton("▶  ACCOMPLISH PORT GOALS")
		self.loop_btn.clicked.connect(self.run_goal_loop)
		self.stop_btn = QPushButton("■  STOP AUTOMATION")
		self.stop_btn.clicked.connect(self.stop_process)
		self.stop_btn.setEnabled(False)
		controls.addWidget(self.loop_btn, 2)
		controls.addWidget(QLabel("Safety pass limit:"))
		controls.addWidget(self.passes_spin)
		controls.addWidget(self.stop_btn)
		layout.addLayout(controls)

		pipeline_box = QGroupBox("PORT PIPELINE")
		pipeline_row = QHBoxLayout(pipeline_box)
		for index, name in enumerate(("AIDER", "REVIEW", "VERIFY", "DOL", "ISO", "DOLPHIN")):
			if index:
				arrow = QLabel("▶")
				arrow.setStyleSheet(f"color: {GC_CYAN};")
				pipeline_row.addWidget(arrow)
			node = QLabel(name)
			node.setAlignment(Qt.AlignmentFlag.AlignCenter)
			node.setObjectName("PipelineIdle")
			self.pipeline[name] = node
			pipeline_row.addWidget(node, 1)
		layout.addWidget(pipeline_box)

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
		self.dolphin_btn = QPushButton("Build & Boot in Dolphin")
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

		console_box = QGroupBox("GC PORT BUS  /  LIVE AUTOMATION LOG")
		console_layout = QVBoxLayout(console_box)
		self.log = QPlainTextEdit()
		self.log.setReadOnly(True)
		self.log.setMaximumBlockCount(10000)
		self.log.setFont(QFont("Monospace", 9))
		console_layout.addWidget(self.log)
		layout.addWidget(console_box, 1)

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
		self.loop_btn.setEnabled(False)
		self.stop_btn.setEnabled(True)
		self.set_pipeline_state(self.pipeline_node(operation), "Running")
		self.append(f"\n\n$ {' '.join(command)}\n")

		proc = QProcess(self)
		proc.setWorkingDirectory(str(self.repo()))
		proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
		proc.readyReadStandardOutput.connect(self.read_output)
		proc.finished.connect(self.finished)
		proc.errorOccurred.connect(lambda error: self.append(f"\nProcess error: {error.name}\n"))
		self.process = proc
		proc.start(command[0], command[1:])

	def run_goal_loop(self) -> None:
		passes = self.passes_spin.value()
		self.start(["scripts/ai-goal-loop.py", "--repo", str(self.repo()),
			"--max-passes", str(passes)], "Goal automation", passes)

	def read_output(self) -> None:
		if self.process is None:
			return
		text = bytes(self.process.readAllStandardOutput()).decode(errors="replace")
		self.append(text)
		if self.operation == "Goal automation":
			for line in text.splitlines():
				if line.startswith("GOAL PASS "):
					try:
						self.progress.setValue(max(0, int(line.split()[2].split("/")[0]) - 1))
					except (IndexError, ValueError):
						pass

	def pipeline_node(self, operation: str) -> str:
		return {
			"Goal automation": "AIDER", "Review HEAD": "REVIEW", "Verify": "VERIFY",
			"Build DOL": "DOL", "Build disc ISO": "ISO",
		}.get(operation, "AIDER")

	def set_pipeline_state(self, name: str, state: str) -> None:
		node = self.pipeline.get(name)
		if node:
			node.setObjectName(f"Pipeline{state}")
			node.style().unpolish(node)
			node.style().polish(node)

	def finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
		succeeded = exit_code == 0
		if succeeded:
			self.progress.setValue(self.expected_passes)
		self.set_pipeline_state(self.pipeline_node(self.operation), "Success" if succeeded else "Failed")
		self.status_label.setText(f"{'Passed' if succeeded else 'Failed'}: {self.operation} (exit {exit_code})")
		self.append(f"\n[{self.operation} exited {exit_code}]\n")
		boot_after_build = self.pending_boot and self.operation == "Build disc ISO" and succeeded
		if self.operation == "Build disc ISO":
			self.pending_boot = False
		self.process = None
		self.loop_btn.setEnabled(True)
		self.stop_btn.setEnabled(False)
		self.refresh_artifacts()
		if boot_after_build:
			self.launch_dolphin()

	def stop_process(self) -> None:
		if self.process is None:
			return
		self.append("\nStopping process…\n")
		self.process.terminate()
		QTimer.singleShot(3000, lambda: self.process.kill() if self.process else None)

	def boot_dolphin(self) -> None:
		iso = self.repo() / "OUT/xash3d-gc.iso"
		if not iso.is_file():
			self.pending_boot = True
			self.append("\nISO missing; building OUT/xash3d-gc.iso before Dolphin launch.\n")
			self.start(["scripts/build-gamecube-disc.py", "--output", "OUT/xash3d-gc.iso"],
				"Build disc ISO")
			return
		self.launch_dolphin()

	def launch_dolphin(self) -> None:
		iso = self.repo() / "OUT/xash3d-gc.iso"
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
		self.set_pipeline_state("DOLPHIN", "Success")
		self.append(f"\nLaunched Dolphin with {iso}\n")

	def refresh_artifacts(self) -> None:
		root = self.repo()
		dol = root / "OUT/bin/boot.dol"
		iso = root / "OUT/xash3d-gc.iso"
		def artifact(label: str, path: Path) -> str:
			return f"{label}  {path.stat().st_size / (1024 * 1024):.1f} MiB" if path.is_file() else f"{label}  MISSING"
		self.dol_chip.setText(artifact("DOL", dol))
		self.iso_chip.setText(artifact("ISO", iso))
		dolphin_ready = bool(shutil.which("dolphin-emu") or shutil.which("dolphin") or shutil.which("flatpak"))
		self.dolphin_chip.setText("DOLPHIN  READY" if dolphin_ready else "DOLPHIN  MISSING")
		if self.process is None:
			self.status_label.setText("Idle — goal console ready")


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
