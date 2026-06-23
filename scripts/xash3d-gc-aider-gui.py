#!/usr/bin/env python3
"""GameCube-inspired command console for goal-driven Xash3D porting."""

from __future__ import annotations

import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from urllib.parse import urlparse

from PyQt6.QtCore import QProcess, QProcessEnvironment, Qt, QTimer, QUrl
from PyQt6.QtGui import QCloseEvent, QDesktopServices, QFont, QFontDatabase, QTextCursor
from PyQt6.QtSvgWidgets import QSvgWidget
from PyQt6.QtWidgets import (
	QApplication,
	QCheckBox,
	QFileDialog,
	QFormLayout,
	QGridLayout,
	QHeaderView,
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
	QTableWidget,
	QTableWidgetItem,
	QVBoxLayout,
	QWidget,
)

DEFAULT_REPO = Path(__file__).resolve().parents[1]
GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |x|X|MANUAL)\]\s+(.+)$")
QWABLE_5_MODEL_ID = "DJLougen/Qwable-5-27B-Coder"
QWABLE_5_SERVED_NAME = "qwen-local"

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


def load_font(path: Path, fallback: str) -> str:
	font_id = QFontDatabase.addApplicationFont(str(path))
	if font_id < 0:
		return fallback
	families = QFontDatabase.applicationFontFamilies(font_id)
	return families[0] if families else fallback


def load_dotenv(path: Path) -> None:
	"""Load simple KEY=VALUE entries without overriding the parent shell."""
	if not path.is_file():
		return
	for raw_line in path.read_text(encoding="utf-8").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("#") or "=" not in line:
			continue
		if line.startswith("export "):
			line = line[len("export "):].lstrip()
		key, value = line.split("=", 1)
		key = key.strip()
		value = value.strip()
		if not key or key in os.environ:
			continue
		if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
			value = value[1:-1]
		os.environ[key] = value


def huggingface_hub_cache() -> Path:
	if os.environ.get("HUGGINGFACE_HUB_CACHE"):
		return Path(os.environ["HUGGINGFACE_HUB_CACHE"]).expanduser()
	if os.environ.get("HF_HOME"):
		return Path(os.environ["HF_HOME"]).expanduser() / "hub"
	return Path.home() / ".cache/huggingface/hub"


def local_hf_snapshot(model_id: str) -> Path | None:
	model_dir = huggingface_hub_cache() / f"models--{model_id.replace('/', '--')}"
	snapshots = model_dir / "snapshots"
	if not snapshots.is_dir():
		return None
	ref = model_dir / "refs/main"
	if ref.is_file():
		candidate = snapshots / ref.read_text(encoding="utf-8").strip()
		if (candidate / "config.json").is_file():
			return candidate
	candidates = [path for path in snapshots.iterdir() if (path / "config.json").is_file()]
	if not candidates:
		return None
	return max(candidates, key=lambda path: path.stat().st_mtime)


def default_qwable_model() -> str:
	if os.environ.get("QWABLE_5_MODEL"):
		return os.environ["QWABLE_5_MODEL"]
	snapshot = local_hf_snapshot(QWABLE_5_MODEL_ID)
	return str(snapshot) if snapshot else QWABLE_5_MODEL_ID


def gopex_vllm_launcher() -> list[str] | None:
	root = Path(os.environ.get("GOPEX_ROOT", str(Path.home() / "GopexLLC"))).expanduser()
	python = root / ".venv/bin/python"
	launcher = root / "tools/vllm_serve_launcher.py"
	if python.exists() and launcher.is_file():
		return [str(python), str(launcher)]
	return None


def vllm_qwable_command() -> str:
	served_name = os.environ.get("QWABLE_5_SERVED_NAME", QWABLE_5_SERVED_NAME)
	command = (gopex_vllm_launcher() or ["vllm"]) + [
		"serve", default_qwable_model(),
		"--host", "127.0.0.1",
		"--port", "8072",
		"--served-model-name", served_name,
		"--language-model-only",
		"--max-model-len", os.environ.get("QWABLE_5_MAX_MODEL_LEN", "8192"),
		"--max-num-seqs", os.environ.get("QWABLE_5_MAX_NUM_SEQS", "256"),
		"--gpu-memory-utilization", os.environ.get("QWABLE_5_GPU_MEMORY_UTILIZATION", "0.85"),
		"--reasoning-parser", "qwen3",
		"--enable-auto-tool-choice",
		"--tool-call-parser", "qwen3_coder",
	]
	return shlex.join(command)


def default_model_command() -> str:
	if os.environ.get("QWABLE_5_COMMAND"):
		return os.environ["QWABLE_5_COMMAND"]
	if shutil.which("qwable-5"):
		return "qwable-5 --host 127.0.0.1 --port 8072"
	if shutil.which("vllm"):
		return vllm_qwable_command()
	return "qwable-5 --host 127.0.0.1 --port 8072"


def command_executable_problem(command: list[str], cwd: Path) -> str | None:
	if not command:
		return "empty command"
	program = command[0]
	if "/" in program:
		path = Path(program).expanduser()
		if not path.is_absolute():
			path = cwd / path
		if not path.exists():
			return f"{program} does not exist"
		if not path.is_file() or not os.access(path, os.X_OK):
			return f"{program} is not executable"
		return None
	return None if shutil.which(program) else program


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
	QTableWidget {{ background: {GC_INPUT}; alternate-background-color: {GC_PANEL}; color: {GC_TEXT};
		gridline-color: {GC_PANEL_2}; border: 1px solid {GC_BORDER}; border-radius: 8px; }}
	QHeaderView::section {{ background: {GC_PANEL_2}; color: {GC_CYAN}; border: 0;
		border-right: 1px solid {GC_PANEL}; padding: 6px; font-weight: bold; }}
	"""


class PortWindow(QMainWindow):
	def __init__(self, gamecube_font_family: str) -> None:
		super().__init__()
		self.setWindowTitle("Xash3D → GameCube — Port Command Console")
		self.resize(1180, 880)
		self.process: QProcess | None = None
		self.model_process: QProcess | None = None
		self.operation = ""
		self.model_operation = ""
		self.expected_passes = 1
		self.pending_boot = False
		self.pipeline: dict[str, QLabel] = {}
		self.last_context = ""
		self.start_head = ""

		central = QWidget()
		self.setCentralWidget(central)
		layout = QVBoxLayout(central)
		layout.setSpacing(10)

		header = QHBoxLayout()
		logo = QSvgWidget(str(DEFAULT_REPO / "fonts/GameCube.svg"))
		logo.setFixedSize(58, 58)
		header.addWidget(logo)
		titles = QVBoxLayout()
		title_row = QHBoxLayout()
		title_row.setSpacing(7)
		title_prefix = QLabel("Xash3D →")
		title_prefix.setObjectName("Title")
		gamecube_title = QLabel("GameCube")
		gamecube_title.setObjectName("GameCubeTitle")
		gamecube_title.setFont(QFont(gamecube_font_family, 22))
		title_row.addWidget(title_prefix)
		title_row.addWidget(gamecube_title)
		title_row.addStretch()
		subtitle = QLabel("BUILD CONSOLE")
		subtitle.setObjectName("Subtitle")
		titles.addLayout(title_row)
		titles.addWidget(subtitle)
		header.addLayout(titles)
		header.addStretch()
		self.dol_chip = QLabel("DOL  —")
		self.iso_chip = QLabel("ISO  —")
		self.dolphin_chip = QLabel("DOLPHIN CHECKING")
		self.model_chip = QLabel("MODEL CHECKING")
		self.save_chip = QLabel("GIT SAVED")
		for chip in (self.dol_chip, self.iso_chip, self.dolphin_chip, self.model_chip, self.save_chip):
			chip.setObjectName("Chip")
			header.addWidget(chip)
		layout.addLayout(header)

		project_box = QGroupBox("WORKSPACE")
		form = QFormLayout(project_box)
		self.repo_edit = QLineEdit(str(DEFAULT_REPO))
		browse = QPushButton("Browse…")
		browse.clicked.connect(self.pick_repo)
		repo_row = QHBoxLayout()
		repo_row.addWidget(self.repo_edit)
		repo_row.addWidget(browse)
		form.addRow("Xash3D repository:", repo_row)

		layout.addWidget(project_box)

		model_box = QGroupBox("MODEL SERVER")
		model_form = QGridLayout(model_box)
		self.model_command_edit = QLineEdit(default_model_command())
		self.model_api_edit = QLineEdit(os.environ.get(
			"OPENAI_API_BASE", "http://127.0.0.1:8072/v1"))
		model_command_label = QLabel("Command:")
		model_api_label = QLabel("API base:")
		model_controls = QHBoxLayout()
		self.start_model_btn = QPushButton("▶  START QWABLE-5")
		self.start_model_btn.clicked.connect(self.start_model)
		self.kill_model_btn = QPushButton("■  KILL QWABLE-5")
		self.kill_model_btn.clicked.connect(self.kill_model)
		model_controls.addWidget(self.start_model_btn)
		model_controls.addWidget(self.kill_model_btn)
		model_form.addWidget(model_command_label, 0, 0)
		model_form.addWidget(model_api_label, 0, 1)
		model_form.addWidget(self.model_command_edit, 1, 0)
		model_form.addWidget(self.model_api_edit, 1, 1)
		model_form.addLayout(model_controls, 2, 0, 1, 2)
		model_form.setColumnStretch(0, 4)
		model_form.setColumnStretch(1, 1)
		layout.addWidget(model_box)

		intel_row = QHBoxLayout()
		goals_box = QGroupBox("GOALS")
		goals_layout = QVBoxLayout(goals_box)
		self.goal_summary = QLabel("Loading goal ledger…")
		self.goal_summary.setWordWrap(True)
		self.goal_summary.setStyleSheet(f"color: {GC_CYAN}; font-weight: bold;")
		goals_layout.addWidget(self.goal_summary)
		self.goal_table = QTableWidget(0, 3)
		self.goal_table.setHorizontalHeaderLabels(["ID", "STATE", "OBJECTIVE"])
		self.goal_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
		self.goal_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
		self.goal_table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
		self.goal_table.setAlternatingRowColors(True)
		self.goal_table.setMaximumHeight(195)
		goals_layout.addWidget(self.goal_table)
		intel_row.addWidget(goals_box, 3)

		context_box = QGroupBox("CONTEXT")
		context_layout = QVBoxLayout(context_box)
		self.context_view = QPlainTextEdit()
		self.context_view.setReadOnly(True)
		self.context_view.setMaximumHeight(225)
		context_layout.addWidget(self.context_view)
		intel_row.addWidget(context_box, 2)
		layout.addLayout(intel_row)

		controls = QHBoxLayout()
		self.passes_spin = QSpinBox()
		self.passes_spin.setRange(1, 100)
		self.passes_spin.setValue(3)
		self.loop_btn = QPushButton("▶  ACCOMPLISH GOALS")
		self.loop_btn.clicked.connect(self.run_goal_loop)
		self.stop_btn = QPushButton("■  STOP AUTOMATION")
		self.stop_btn.clicked.connect(self.stop_process)
		self.stop_btn.setEnabled(False)
		controls.addWidget(self.loop_btn, 2)
		controls.addWidget(QLabel("Safety pass limit:"))
		controls.addWidget(self.passes_spin)
		controls.addWidget(self.stop_btn)
		layout.addLayout(controls)

		pipeline_box = QGroupBox("PIPELINE")
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
		self.persistence_note = QLabel(
			"LIVE MODEL OUTPUT IS NOT A FILE CHANGE UNTIL THE STATUS CHIP SAYS GIT SAVED"
		)
		self.persistence_note.setStyleSheet(f"color: {GC_ORANGE}; font-weight: bold;")
		layout.addWidget(self.persistence_note)

		console_box = QGroupBox("LOG")
		console_layout = QVBoxLayout(console_box)
		log_tools = QHBoxLayout()
		self.copy_log_btn = QPushButton("Copy Log")
		self.copy_log_btn.setToolTip("Copy selected text, or the entire log when nothing is selected")
		self.copy_log_btn.setShortcut("Ctrl+Shift+C")
		self.copy_log_btn.clicked.connect(self.copy_log)
		self.save_log_btn = QPushButton("Save Log…")
		self.save_log_btn.setShortcut("Ctrl+Shift+S")
		self.save_log_btn.clicked.connect(self.save_log)
		self.clear_log_btn = QPushButton("Clear")
		self.clear_log_btn.clicked.connect(self.clear_log)
		self.open_logs_btn = QPushButton("Open Logs Folder")
		self.open_logs_btn.clicked.connect(self.open_logs_folder)
		self.follow_log = QCheckBox("Follow output")
		self.follow_log.setChecked(True)
		for button in (self.copy_log_btn, self.save_log_btn, self.clear_log_btn,
			self.open_logs_btn):
			log_tools.addWidget(button)
		log_tools.addStretch()
		log_tools.addWidget(self.follow_log)
		console_layout.addLayout(log_tools)
		self.log = QPlainTextEdit()
		self.log.setReadOnly(True)
		self.log.setMaximumBlockCount(10000)
		console_layout.addWidget(self.log)
		layout.addWidget(console_box, 1)

		self.timer = QTimer(self)
		self.timer.setInterval(3000)
		self.timer.timeout.connect(self.refresh_dashboard)
		self.timer.start()
		self.refresh_dashboard()

	def repo(self) -> Path:
		return Path(self.repo_edit.text().strip()).expanduser().resolve()

	def valid_repo(self) -> bool:
		root = self.repo()
		ok = (root / ".git").exists() and (root / "scripts/ai-aider-pass.sh").is_file()
		if not ok:
			QMessageBox.warning(self, "Invalid repository", "Select the Xash3D GameCube repository root.")
		return ok

	def pick_repo(self) -> None:
		path = QFileDialog.getExistingDirectory(self, "Select repository", str(self.repo()))
		if path:
			self.repo_edit.setText(path)

	def append(self, text: str) -> None:
		visible_cursor = self.log.textCursor()
		had_selection = visible_cursor.hasSelection()
		position = visible_cursor.position()
		anchor = visible_cursor.anchor()
		scrollbar = self.log.verticalScrollBar()
		scroll_position = scrollbar.value()

		writer = QTextCursor(self.log.document())
		writer.movePosition(QTextCursor.MoveOperation.End)
		writer.insertText(text)

		if self.follow_log.isChecked() and not had_selection:
			scrollbar.setValue(scrollbar.maximum())
		else:
			restored = self.log.textCursor()
			restored.setPosition(anchor)
			restored.setPosition(position, QTextCursor.MoveMode.KeepAnchor)
			self.log.setTextCursor(restored)
			scrollbar.setValue(scroll_position)

	def copy_log(self) -> None:
		cursor = self.log.textCursor()
		text = cursor.selectedText().replace("\u2029", "\n") if cursor.hasSelection() \
			else self.log.toPlainText()
		QApplication.clipboard().setText(text)
		kind = "selection" if cursor.hasSelection() else "log"
		self.status_label.setText(f"Copied {kind} ({len(text):,} characters)")

	def save_log(self) -> None:
		logs = self.repo() / ".ai/logs"
		logs.mkdir(parents=True, exist_ok=True)
		stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
		default = logs / f"gui-console-{stamp}.log"
		path, _ = QFileDialog.getSaveFileName(self, "Save console log", str(default),
			"Log files (*.log);;Text files (*.txt);;All files (*)")
		if not path:
			return
		try:
			Path(path).write_text(self.log.toPlainText(), encoding="utf-8")
		except OSError as exc:
			QMessageBox.warning(self, "Save failed", str(exc))
			return
		self.status_label.setText(f"Saved log: {path}")

	def clear_log(self) -> None:
		self.log.clear()
		self.status_label.setText("Console log cleared")

	def open_logs_folder(self) -> None:
		logs = self.repo() / ".ai/logs"
		logs.mkdir(parents=True, exist_ok=True)
		if not QDesktopServices.openUrl(QUrl.fromLocalFile(str(logs.resolve()))):
			QMessageBox.warning(self, "Open folder failed", str(logs))

	def model_host_port(self) -> tuple[str, int]:
		parsed = urlparse(self.model_api_edit.text().strip() or "http://127.0.0.1:8072/v1")
		host = parsed.hostname or "127.0.0.1"
		try:
			port = parsed.port or (443 if parsed.scheme == "https" else 80)
		except ValueError:
			port = 8072
		return host, port

	def model_port_open(self) -> bool:
		host, port = self.model_host_port()
		try:
			with socket.create_connection((host, port), timeout=0.2):
				return True
		except OSError:
			return False

	def model_kill_pattern(self) -> str:
		if os.environ.get("QWABLE_5_KILL_PATTERN"):
			return os.environ["QWABLE_5_KILL_PATTERN"]
		try:
			command = shlex.split(self.model_command_edit.text().strip())
		except ValueError:
			command = []
		if "qwable-5" in command:
			return "qwable-5"
		return Path(command[0]).name if command else "qwable-5"

	def set_model_state(self, text: str, color: str) -> None:
		self.model_chip.setText(text)
		self.model_chip.setStyleSheet(f"color: {color};")

	def start_model(self) -> None:
		if not self.valid_repo():
			return
		if self.model_process is not None:
			self.status_label.setText("qwable-5 is already managed by this GUI")
			return
		command_text = self.model_command_edit.text().strip()
		try:
			command = shlex.split(command_text)
		except ValueError as exc:
			QMessageBox.warning(self, "Invalid model command", str(exc))
			return
		if not command:
			QMessageBox.warning(self, "Invalid model command", "Enter a command to start qwable-5.")
			return
		problem = command_executable_problem(command, self.repo())
		if problem:
			message = (
				f"Cannot start model command: {problem}\n\n"
				"Install it, add it to PATH, or set QWABLE_5_COMMAND. "
				"If you use vLLM, try:\n"
				f"vllm serve {QWABLE_5_MODEL_ID} --host 127.0.0.1 --port 8072 "
				f"--served-model-name {QWABLE_5_SERVED_NAME}"
			)
			self.append(f"\nModel command problem: {problem}\n")
			QMessageBox.warning(self, "Model command problem", message)
			return

		self.model_operation = "qwable-5"
		self.append(f"\n\n$ {' '.join(command)}\n")
		self.status_label.setText("Starting qwable-5")
		self.set_model_state("MODEL STARTING", GC_ORANGE)
		self.start_model_btn.setEnabled(False)
		self.kill_model_btn.setEnabled(True)

		proc = QProcess(self)
		proc.setWorkingDirectory(str(self.repo()))
		proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
		load_dotenv(self.repo() / ".env")
		env = QProcessEnvironment.systemEnvironment()
		for key, value in os.environ.items():
			env.insert(key, value)
		env.insert("OPENAI_API_BASE", self.model_api_edit.text().strip())
		if not env.contains("CUDA_DEVICE_ORDER"):
			env.insert("CUDA_DEVICE_ORDER", "PCI_BUS_ID")
		if not env.contains("VLLM_USE_FLASHINFER_SAMPLER"):
			env.insert("VLLM_USE_FLASHINFER_SAMPLER", "0")
		proc.setProcessEnvironment(env)
		proc.readyReadStandardOutput.connect(self.read_model_output)
		proc.finished.connect(self.model_finished)
		proc.errorOccurred.connect(
			lambda error: self.append(
				f"\nModel process error: {error.name} while starting {command[0]}\n"
			))
		self.model_process = proc
		proc.start(command[0], command[1:])

	def read_model_output(self) -> None:
		if self.model_process is None:
			return
		text = bytes(self.model_process.readAllStandardOutput()).decode(errors="replace")
		self.append(text)

	def model_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
		self.append(f"\n[{self.model_operation or 'qwable-5'} exited {exit_code}]\n")
		self.model_process = None
		self.model_operation = ""
		self.start_model_btn.setEnabled(True)
		self.kill_model_btn.setEnabled(True)
		self.refresh_model_status()

	def kill_model(self) -> None:
		if self.model_process is not None:
			self.append("\nStopping qwable-5 process…\n")
			self.model_process.terminate()
			QTimer.singleShot(3000, lambda: self.model_process.kill() if self.model_process else None)
			return

		pattern = self.model_kill_pattern()
		answer = QMessageBox.question(self, "Kill qwable-5?",
			f"No GUI-managed model process is running. Send TERM to processes matching '{pattern}'?",
			QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
			QMessageBox.StandardButton.No)
		if answer != QMessageBox.StandardButton.Yes:
			return
		if not shutil.which("pkill"):
			QMessageBox.warning(self, "pkill missing", "Cannot kill an external qwable-5 process without pkill.")
			return
		result = subprocess.run(["pkill", "-TERM", "-f", pattern], cwd=self.repo(),
			text=True, capture_output=True, check=False)
		if result.returncode not in (0, 1):
			self.append(result.stderr or result.stdout)
			QMessageBox.warning(self, "Kill failed", f"pkill exited {result.returncode}")
			return
		self.append(f"\nSent TERM to processes matching '{pattern}'.\n")
		QTimer.singleShot(3000, self.refresh_model_status)

	def git_output(self, *args: str) -> str:
		result = subprocess.run(["git", *args], cwd=self.repo(), text=True,
			capture_output=True, timeout=4, check=False)
		return result.stdout.strip()

	def set_save_state(self, text: str, color: str) -> None:
		self.save_chip.setText(text)
		self.save_chip.setStyleSheet(f"color: {color};")

	def read_goals(self) -> list[tuple[str, str, str, str]]:
		path = self.repo() / ".ai/goals/GAMECUBE_PORT_GOALS.md"
		if not path.is_file():
			return []
		goals: list[tuple[str, str, str, str]] = []
		current: tuple[str, str, str] | None = None
		body: list[str] = []
		for line in path.read_text(encoding="utf-8").splitlines():
			match = GOAL_RE.match(line)
			if match:
				if current:
					goals.append((*current, "\n".join(body).strip()))
				current = match.groups()
				body = []
			elif current:
				body.append(line)
		if current:
			goals.append((*current, "\n".join(body).strip()))
		return goals

	def refresh_goals(self) -> None:
		goals = self.read_goals()
		self.goal_table.setRowCount(len(goals))
		def blocked(body: str) -> bool:
			return bool(re.search(r"(?im)^\s*-\s*Status:\s*BLOCKED\b", body))
		complete = sum(state.lower() == "x" for _, state, _, _ in goals)
		blocked_count = sum(blocked(body) for _, state, _, body in goals if state != "MANUAL")
		automatic = sum(state != "MANUAL" for _, state, _, _ in goals)
		active = next((goal for goal in goals
			if goal[1] == " " and not blocked(goal[3])), None)
		for row, (goal_id, state, title, body) in enumerate(goals):
			is_blocked = blocked(body)
			label = "MANUAL" if state == "MANUAL" else "DONE" if state.lower() == "x" \
				else "BLOCKED" if is_blocked else "ACTIVE" if active and goal_id == active[0] else "QUEUED"
			for column, value in enumerate((goal_id, label, title)):
				item = QTableWidgetItem(value)
				if label == "DONE":
					item.setForeground(Qt.GlobalColor.cyan)
				elif label == "ACTIVE":
					item.setForeground(Qt.GlobalColor.yellow)
				elif label == "BLOCKED":
					item.setForeground(Qt.GlobalColor.red)
				self.goal_table.setItem(row, column, item)
		if active:
			criteria = " ".join(line.lstrip("- ") for line in active[3].splitlines() if line.startswith("- "))
			self.goal_summary.setText(f"ACTIVE {active[0]}  /  {active[2]}\n{criteria}")
		else:
			self.goal_summary.setText("All automatic goals complete or blocked; manual hardware validation remains.")
		self.progress.setToolTip(f"{complete}/{automatic} automatic goals complete, {blocked_count} blocked")

	def refresh_context(self) -> None:
		root = self.repo()
		if not (root / ".git").exists():
			return
		load_dotenv(root / ".env")
		branch = self.git_output("branch", "--show-current") or "detached"
		porcelain = self.git_output("status", "--porcelain")
		tracking = self.git_output("status", "--short", "--branch").splitlines()
		recent = self.git_output("log", "-1", "--oneline")
		submodules = self.git_output("submodule", "status", "--recursive").splitlines()
		dirty_submodules = sum(line.startswith(("+", "-", "U")) for line in submodules)
		valve = root / "Half-Life/valve"
		blockers = root / ".ai/state/BLOCKERS.md"
		blocker_tail = "none recorded"
		if blockers.is_file():
			entries = [line[2:] for line in blockers.read_text(encoding="utf-8").splitlines() if line.startswith("- ")]
			if entries:
				blocker_tail = entries[-1][:100]
		toolchain = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro")) / "devkitPPC/bin/powerpc-eabi-gcc"
		lines = [
			f"GIT       {branch}  {'DIRTY' if porcelain else 'CLEAN'}",
			f"TRACKING  {tracking[0][3:] if tracking else 'unknown'}",
			f"HEAD      {recent}",
			f"SUBMODULE {len(submodules)} present / {dirty_submodules} divergent",
			f"TOOLCHAIN {'READY' if toolchain.is_file() else 'MISSING'}  {toolchain}",
			f"CONTENT   {'READY' if valve.is_dir() else 'MISSING'}  Half-Life/valve",
			f"AIDER     {'AUTH INHERITED' if os.environ.get('OPENAI_API_KEY') else 'AUTH NOT IN ENVIRONMENT'}",
			f"BLOCKER   {blocker_tail}",
		]
		context = "\n".join(lines)
		if context != self.last_context:
			self.context_view.setPlainText(context)
			self.last_context = context

	def refresh_dashboard(self) -> None:
		try:
			self.refresh_artifacts()
			self.refresh_model_status()
			self.refresh_goals()
			self.refresh_context()
		except (OSError, subprocess.SubprocessError) as exc:
			self.context_view.setPlainText(f"Telemetry unavailable: {exc}")

	def start(self, command: list[str], operation: str, passes: int = 1) -> None:
		if self.process is not None or not self.valid_repo():
			return
		self.operation = operation
		self.start_head = self.git_output("rev-parse", "HEAD")
		self.expected_passes = passes
		self.progress.setRange(0, passes)
		self.progress.setValue(0)
		self.progress.setFormat(f"{operation}: %v / %m")
		self.status_label.setText(f"Running: {operation}")
		self.loop_btn.setEnabled(False)
		self.stop_btn.setEnabled(True)
		if operation == "Goal automation":
			self.set_save_state("UNSAVED", GC_ORANGE)
		else:
			self.set_save_state("RUNNING", GC_CYAN)
		self.set_pipeline_state(self.pipeline_node(operation), "Running")
		self.append(f"\n\n$ {' '.join(command)}\n")

		proc = QProcess(self)
		proc.setWorkingDirectory(str(self.repo()))
		proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
		load_dotenv(self.repo() / ".env")
		env = QProcessEnvironment.systemEnvironment()
		for key, value in os.environ.items():
			env.insert(key, value)
		proc.setProcessEnvironment(env)
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
		if "== verifier ==" in text:
			self.set_pipeline_state("VERIFY", "Running")
		if "== pre-commit verifier ==" in text or "== repaired pre-commit verifier ==" in text:
			self.set_save_state("VERIFYING", GC_ORANGE)
		if "== post-commit safety ==" in text:
			self.set_save_state("COMMITTED", GC_CYAN)
		if "== accepted patch ==" in text:
			self.set_save_state("GIT SAVED", GC_MINT)
		if "verify: OK" in text:
			self.set_pipeline_state("VERIFY", "Success")
		if "== AI review ==" in text:
			self.set_pipeline_state("REVIEW", "Running")
		if "review: OK" in text:
			self.set_pipeline_state("REVIEW", "Success")
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
		self.refresh_dashboard()
		current_head = self.git_output("rev-parse", "HEAD")
		dirty = bool(self.git_output("status", "--porcelain"))
		if dirty:
			self.set_save_state("REVIEW", GC_ORANGE)
		elif current_head != self.start_head:
			self.set_save_state("GIT SAVED", GC_MINT)
		elif self.operation == "Goal automation":
			self.set_save_state("NO CHANGE", GC_ORANGE)
		else:
			self.set_save_state("UNCHANGED", GC_MUTED)
		if boot_after_build:
			self.launch_dolphin()

	def stop_process(self) -> None:
		if self.process is None:
			return
		answer = QMessageBox.question(self, "Stop automation?",
			"The current model response may not have reached an applied, verified Git commit. "
			"Stopping now discards incomplete model output. Stop anyway?",
			QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
			QMessageBox.StandardButton.No)
		if answer != QMessageBox.StandardButton.Yes:
			return
		self.append("\nStopping process…\n")
		self.process.terminate()
		QTimer.singleShot(3000, lambda: self.process.kill() if self.process else None)

	def closeEvent(self, event: QCloseEvent) -> None:
		if self.process is None and self.model_process is None:
			event.accept()
			return
		if self.process is not None:
			title = "Automation is still running"
			message = (
				"Closing now can discard incomplete model output before it becomes a Git commit. "
				"Keep the GUI open until GIT SAVED appears. Close anyway?"
			)
		else:
			title = "qwable-5 is still running"
			message = "Close the GUI and terminate the managed qwable-5 process?"
		answer = QMessageBox.question(self, title, message,
			QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
			QMessageBox.StandardButton.No)
		if answer == QMessageBox.StandardButton.Yes:
			if self.model_process is not None:
				self.model_process.terminate()
			event.accept()
		else:
			event.ignore()

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
		active_node = self.pipeline_node(self.operation) if self.process else ""
		if active_node != "DOL":
			self.set_pipeline_state("DOL", "Success" if dol.is_file() else "Idle")
		if active_node != "ISO":
			self.set_pipeline_state("ISO", "Success" if iso.is_file() else "Idle")
		dolphin_ready = bool(shutil.which("dolphin-emu") or shutil.which("dolphin") or shutil.which("flatpak"))
		self.dolphin_chip.setText("DOLPHIN  READY" if dolphin_ready else "DOLPHIN  MISSING")
		if self.process is None:
			self.status_label.setText("Idle — goal console ready")

	def refresh_model_status(self) -> None:
		if self.model_process is not None:
			self.set_model_state("MODEL  RUNNING", GC_CYAN)
			self.start_model_btn.setEnabled(False)
			self.kill_model_btn.setEnabled(True)
			return
		if self.model_port_open():
			self.set_model_state("MODEL  READY", GC_MINT)
			self.start_model_btn.setEnabled(False)
			self.kill_model_btn.setEnabled(True)
		else:
			self.set_model_state("MODEL  DOWN", GC_ORANGE)
			self.start_model_btn.setEnabled(True)
			self.kill_model_btn.setEnabled(True)


def main() -> int:
	app = QApplication(sys.argv)
	rodin_family = load_font(DEFAULT_REPO / "fonts/FOT-Rodin Pro DB.otf", "Sans Serif")
	gamecube_family = load_font(DEFAULT_REPO / "fonts/GameCube.ttf", rodin_family)
	app.setStyleSheet(stylesheet())
	font = QFont(rodin_family, 10)
	font.setStyleHint(QFont.StyleHint.SansSerif)
	app.setFont(font)
	window = PortWindow(gamecube_family)
	window.show()
	return app.exec()


if __name__ == "__main__":
	raise SystemExit(main())
