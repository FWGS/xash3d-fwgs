#!/usr/bin/env python3
"""GameCube-inspired command console for goal-driven Xash3D porting."""

from __future__ import annotations

from dataclasses import dataclass
from collections.abc import Mapping
import os
import re
import json
import shlex
import shutil
import socket
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from urllib.parse import urlparse

from PyQt6.QtCore import QByteArray, QObject, QProcess, QProcessEnvironment, Qt, QThread, QTimer, QUrl, pyqtSignal
from PyQt6.QtGui import QAction, QCloseEvent, QDesktopServices, QFont, QFontDatabase, QPixmap, QTextCursor
from PyQt6.QtSvgWidgets import QSvgWidget
from PyQt6.QtWidgets import (
	QApplication,
	QCheckBox,
	QDialog,
	QDialogButtonBox,
	QDockWidget,
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
	QSizePolicy,
	QSpinBox,
	QTabWidget,
	QTableWidget,
	QTableWidgetItem,
	QVBoxLayout,
	QWidget,
)

DEFAULT_REPO = Path(__file__).resolve().parents[1]
APP_VERSION = "0.4.0-dev"
SETTINGS_PATH = DEFAULT_REPO / ".ai/state/xash3d-gc-aider-gui-settings.json"
GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |~|x|X|MANUAL)\]\s+(.+)$")
QWABLE_5_MODEL_ID = "DJLougen/Qwable-5-27B-Coder"
QWABLE_5_SERVED_NAME = "qwen-local"
HEADER_LOGO = DEFAULT_REPO / "assets/ui/nintendo-gamecube-logo.svg"
HEADER_MARK = DEFAULT_REPO / "assets/ui/gamecube-mark.svg"
DOCK_CLOSE_ICON = DEFAULT_REPO / "assets/ui/dock-close-white.svg"
DOCK_FLOAT_ICON = DEFAULT_REPO / "assets/ui/dock-float-white.svg"

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
		"--max-model-len", os.environ.get("QWABLE_5_MAX_MODEL_LEN", "65536"),
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
	if gopex_vllm_launcher():
		return vllm_qwable_command()
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


def command_gpu_memory_utilization(command: list[str]) -> float | None:
	for index, arg in enumerate(command):
		if arg == "--gpu-memory-utilization" and index + 1 < len(command):
			try:
				return float(command[index + 1])
			except ValueError:
				return None
		if arg.startswith("--gpu-memory-utilization="):
			try:
				return float(arg.split("=", 1)[1])
			except ValueError:
				return None
	return None


def gpu_memory_preflight_message(command: list[str]) -> str | None:
	utilization = command_gpu_memory_utilization(command)
	if utilization is None or not shutil.which("nvidia-smi"):
		return None
	try:
		result = subprocess.run([
			"nvidia-smi",
			"--query-gpu=index,memory.total,memory.free",
			"--format=csv,noheader,nounits",
		], text=True, capture_output=True, timeout=3, check=False)
	except (OSError, subprocess.SubprocessError):
		return None
	if result.returncode != 0 or not result.stdout.strip():
		return None

	line = result.stdout.splitlines()[0]
	try:
		gpu_index, total_text, free_text = [part.strip() for part in line.split(",", 2)]
		total_mib = float(total_text)
		free_mib = float(free_text)
	except (ValueError, IndexError):
		return None
	requested_mib = total_mib * utilization
	if requested_mib <= free_mib:
		return None

	process_lines: list[str] = []
	try:
		procs = subprocess.run([
			"nvidia-smi",
			"--query-compute-apps=pid,process_name,used_memory",
			"--format=csv,noheader,nounits",
		], text=True, capture_output=True, timeout=3, check=False)
		if procs.returncode == 0:
			for proc_line in procs.stdout.splitlines()[:6]:
				parts = [part.strip() for part in proc_line.split(",")]
				if len(parts) >= 3:
					process_lines.append(f"  pid {parts[0]}  {parts[1]}  {parts[2]} MiB")
	except (OSError, subprocess.SubprocessError):
		pass

	summary = (
		f"GPU {gpu_index} has {free_mib / 1024:.1f} GiB free of {total_mib / 1024:.1f} GiB, "
		f"but --gpu-memory-utilization {utilization:.2f} asks vLLM for "
		f"{requested_mib / 1024:.1f} GiB.\n\n"
		"Free GPU memory, lower QWABLE_5_GPU_MEMORY_UTILIZATION, or reuse the "
		"already-running model server instead of starting another one. If those "
		"processes are stale, use the GUI Kill button or stop them before retrying."
	)
	if process_lines:
		summary += "\n\nGPU compute users:\n" + "\n".join(process_lines)
	return summary


def stylesheet() -> str:
	return f"""
	QMainWindow, QWidget {{ background: {GC_BG}; color: {GC_TEXT}; }}
	QMenuBar {{ background: {GC_PANEL}; color: {GC_TEXT}; border-bottom: 1px solid {GC_BORDER}; }}
	QMenuBar::item {{ padding: 4px 8px; background: transparent; }}
	QMenuBar::item:selected {{ background: {GC_PANEL_2}; color: {GC_CYAN}; }}
	QMenu {{ background: {GC_PANEL}; color: {GC_TEXT}; border: 1px solid {GC_BORDER}; }}
	QMenu::item {{ padding: 5px 22px; }}
	QMenu::item:selected {{ background: {GC_PANEL_2}; color: {GC_CYAN}; }}
	QDockWidget {{ titlebar-close-icon: url({DOCK_CLOSE_ICON.as_posix()});
		titlebar-normal-icon: url({DOCK_FLOAT_ICON.as_posix()}); }}
	QDockWidget::title {{ background: {GC_PANEL_2}; color: {GC_CYAN};
		border: 1px solid {GC_BORDER}; padding: 5px 7px; font-weight: bold; }}
	QDockWidget::close-button, QDockWidget::float-button {{ background: transparent;
		border: 1px solid transparent; padding: 2px; icon-size: 14px; }}
	QDockWidget::close-button:hover, QDockWidget::float-button:hover {{
		background: {GC_VIOLET}; border: 1px solid {GC_CYAN}; border-radius: 3px; }}
	QDockWidget::close-button:pressed, QDockWidget::float-button:pressed {{ background: #4b3c99; }}
	QGroupBox {{ background: {GC_PANEL}; border: 2px solid {GC_BORDER};
		border-radius: 8px; margin-top: 11px; padding: 9px; font-weight: bold; }}
	QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 6px; color: {GC_CYAN}; }}
	QLineEdit, QSpinBox, QPlainTextEdit {{ background: {GC_INPUT}; color: {GC_TEXT};
		border: 2px inset {GC_PANEL_2}; border-radius: 6px; padding: 5px; }}
	QLineEdit:focus, QSpinBox:focus, QPlainTextEdit:focus {{ border: 2px solid {GC_CYAN}; }}
	QPushButton {{ background: {GC_VIOLET}; color: {GC_TEXT}; border: 2px outset {GC_BORDER};
		border-radius: 9px; padding: 6px 10px; font-weight: bold; }}
	QPushButton:hover {{ background: #806ee0; border-color: {GC_CYAN}; }}
	QPushButton:pressed {{ background: #4b3c99; border-style: inset; }}
	QPushButton:disabled {{ background: {GC_PANEL_2}; color: {GC_MUTED}; border-color: {GC_PANEL}; }}
	QProgressBar {{ background: {GC_INPUT}; border: 1px solid {GC_CYAN}; border-radius: 8px;
		text-align: center; color: {GC_TEXT}; height: 16px; }}
	QProgressBar::chunk {{ background: {GC_CYAN}; border-radius: 7px; }}
	QLabel#Title {{ color: {GC_TEXT}; font-size: 19px; font-weight: bold; }}
	QLabel#Subtitle {{ color: {GC_CYAN}; font-size: 9px; }}
	QLabel#Chip {{ background: {GC_PANEL_2}; color: {GC_MINT}; border: 1px solid {GC_BORDER};
		border-radius: 7px; padding: 4px 7px; font-weight: bold; }}
	QLabel#CenterBay {{ background: #090617; color: {GC_MUTED}; border: 2px dashed {GC_PANEL_2};
		border-radius: 10px; padding: 12px; font-weight: bold; }}
	QLabel#PipelineIdle {{ background: {GC_PANEL_2}; color: {GC_MUTED}; border: 2px outset {GC_BORDER};
		border-radius: 8px; padding: 8px; font-weight: bold; }}
	QLabel#PipelineRunning {{ background: #17405a; color: {GC_CYAN}; border: 2px solid {GC_CYAN};
		border-radius: 8px; padding: 8px; font-weight: bold; }}
	QLabel#PipelineSuccess {{ background: #174638; color: {GC_MINT}; border: 2px solid {GC_MINT};
		border-radius: 8px; padding: 8px; font-weight: bold; }}
	QLabel#PipelineFailed {{ background: #563621; color: {GC_ORANGE}; border: 2px solid {GC_ORANGE};
		border-radius: 8px; padding: 8px; font-weight: bold; }}
	QTableWidget {{ background: {GC_INPUT}; alternate-background-color: {GC_PANEL}; color: {GC_TEXT};
		gridline-color: {GC_PANEL_2}; border: 1px solid {GC_BORDER}; border-radius: 6px; }}
	QHeaderView::section {{ background: {GC_PANEL_2}; color: {GC_CYAN}; border: 0;
		border-right: 1px solid {GC_PANEL}; padding: 4px; font-weight: bold; }}
	"""


@dataclass
class DashboardSnapshot:
	dol_text: str = "DOL  MISSING"
	iso_text: str = "ISO  MISSING"
	dol_exists: bool = False
	iso_exists: bool = False
	dolphin_ready: bool = False
	model_ready: bool = False
	goals: list[tuple[str, str, str, str]] | None = None
	complete_goals: int = 0
	automatic_goals: int = 0
	blocked_goals: int = 0
	active_goal: tuple[str, str, str, str] | None = None
	context: str = ""
	screenshot_path: str = ""
	screenshot_status: str = "No Dolphin screenshot captured yet"
	error: str = ""


def git_output_for_repo(repo: Path, *args: str) -> str:
	result = subprocess.run(["git", *args], cwd=repo, text=True,
		capture_output=True, timeout=4, check=False)
	return result.stdout.strip()


def read_goals_for_repo(repo: Path) -> list[tuple[str, str, str, str]]:
	path = repo / ".ai/goals/GAMECUBE_PORT_GOALS.md"
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


def goal_is_blocked(body: str) -> bool:
	return bool(re.search(r"(?im)^\s*-\s*Status:\s*BLOCKED\b", body))


def latest_dolphin_screenshot_for_repo(repo: Path) -> tuple[str, str]:
	memory = repo / ".ai/state/dolphin-harness-memory.json"
	if memory.is_file():
		try:
			data = json.loads(memory.read_text(encoding="utf-8"))
			run = data.get("runs", [{}])[0] if isinstance(data.get("runs"), list) else {}
			shot = run.get("latest_screenshot") if isinstance(run, dict) else ""
			status = ""
			if isinstance(run, dict):
				classification = run.get("classification", {})
				if isinstance(classification, dict):
					status = (
						f"{classification.get('status', 'unknown')} / "
						f"{classification.get('visual', 'visual unknown')} / "
						f"{classification.get('audio', 'audio unknown')}"
					)
			if shot:
				path = repo / str(shot)
				if path.is_file():
					return str(path), status
		except (OSError, json.JSONDecodeError, TypeError):
			pass

	candidates = sorted((repo / ".ai/logs").glob("dolphin-vision-*/screenshots/*.png"),
		key=lambda path: path.stat().st_mtime if path.exists() else 0)
	if candidates:
		return str(candidates[-1]), "latest screenshot from harness logs"
	return "", "No Dolphin screenshot captured yet"


def model_port_open(host: str, port: int) -> bool:
	try:
		with socket.create_connection((host, port), timeout=0.2):
			return True
	except OSError:
		return False


def build_dashboard_snapshot(repo: Path, model_host: str, model_port: int) -> DashboardSnapshot:
	snapshot = DashboardSnapshot()
	try:
		dol = repo / "OUT/bin/boot.dol"
		iso = repo / "OUT/xash3d-gc.iso"

		def artifact(label: str, path: Path) -> str:
			return f"{label}  {path.stat().st_size / (1024 * 1024):.1f} MiB" if path.is_file() else f"{label}  MISSING"

		snapshot.dol_exists = dol.is_file()
		snapshot.iso_exists = iso.is_file()
		snapshot.dol_text = artifact("DOL", dol)
		snapshot.iso_text = artifact("ISO", iso)
		snapshot.dolphin_ready = bool(os.environ.get("DOLPHIN_EXECUTABLE") or
			shutil.which("dolphin-emu") or shutil.which("dolphin") or shutil.which("flatpak"))
		snapshot.model_ready = model_port_open(model_host, model_port)

		goals = read_goals_for_repo(repo)
		snapshot.goals = goals
		snapshot.complete_goals = sum(state.lower() == "x" for _, state, _, _ in goals)
		snapshot.blocked_goals = sum(goal_is_blocked(body) for _, state, _, body in goals if state != "MANUAL")
		snapshot.automatic_goals = sum(state != "MANUAL" for _, state, _, _ in goals)
		snapshot.active_goal = next((goal for goal in goals
			if goal[1] in {" ", "~"} and not goal_is_blocked(goal[3])), None)

		if (repo / ".git").exists():
			load_dotenv(repo / ".env")
			branch = git_output_for_repo(repo, "branch", "--show-current") or "detached"
			porcelain = git_output_for_repo(repo, "status", "--porcelain")
			tracking = git_output_for_repo(repo, "status", "--short", "--branch").splitlines()
			recent = git_output_for_repo(repo, "log", "-1", "--oneline")
			submodules = git_output_for_repo(repo, "submodule", "status", "--recursive").splitlines()
			dirty_submodules = sum(line.startswith(("+", "-", "U")) for line in submodules)
			valve = repo / "Half-Life/valve"
			blockers = repo / ".ai/state/BLOCKERS.md"
			blocker_tail = "none recorded"
			if blockers.is_file():
				entries = [line[2:] for line in blockers.read_text(encoding="utf-8").splitlines() if line.startswith("- ")]
				if entries:
					blocker_tail = entries[-1][:100]
			harness_latest = repo / ".ai/state/dolphin-harness-latest.md"
			harness_status = "none recorded"
			if harness_latest.is_file():
				interesting = []
				for line in harness_latest.read_text(encoding="utf-8").splitlines():
					if line.startswith("- Status:") or line.startswith("- Visual:") or line.startswith("- Next action:"):
						interesting.append(line.removeprefix("- ").strip())
				if interesting:
					harness_status = " / ".join(interesting)[:160]
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
				f"DOLPHIN   {harness_status}",
			]
			snapshot.context = "\n".join(lines)

		snapshot.screenshot_path, snapshot.screenshot_status = latest_dolphin_screenshot_for_repo(repo)
	except (OSError, subprocess.SubprocessError, ValueError) as exc:
		snapshot.error = f"Telemetry unavailable: {exc}"
	return snapshot


class DashboardWorker(QObject):
	finished = pyqtSignal(object)

	def __init__(self, repo: Path, model_host: str, model_port: int) -> None:
		super().__init__()
		self.repo = repo
		self.model_host = model_host
		self.model_port = model_port

	def run(self) -> None:
		self.finished.emit(build_dashboard_snapshot(self.repo, self.model_host, self.model_port))


class PortWindow(QMainWindow):
	def __init__(self, gamecube_font_family: str) -> None:
		super().__init__()
		self.setWindowTitle("Xash3D on GameCube Porting")
		self.resize(1320, 820)
		self.process: QProcess | None = None
		self.model_process: QProcess | None = None
		self.operation = ""
		self.model_operation = ""
		self.expected_passes = 1
		self.pending_boot = False
		self.user_stopping = False
		self.last_command: list[str] = []
		self.last_passes = 1
		self.pipeline: dict[str, QLabel] = {}
		self.docks: dict[str, QDockWidget] = {}
		self.center_widgets: dict[str, QWidget] = {}
		self.dashboard_threads: list[QThread] = []
		self.dashboard_refresh_running = False
		self.dashboard_refresh_pending = False
		self.last_context = ""
		self.start_head = ""
		self.closing = False
		self.setDockOptions(
			QMainWindow.DockOption.AllowNestedDocks |
			QMainWindow.DockOption.AllowTabbedDocks |
			QMainWindow.DockOption.AnimatedDocks |
			QMainWindow.DockOption.GroupedDragging
		)
		self.setDockNestingEnabled(True)
		for area in (
			Qt.DockWidgetArea.LeftDockWidgetArea,
			Qt.DockWidgetArea.RightDockWidgetArea,
			Qt.DockWidgetArea.TopDockWidgetArea,
			Qt.DockWidgetArea.BottomDockWidgetArea,
		):
			self.setTabPosition(area, QTabWidget.TabPosition.North)
		self.setCorner(Qt.Corner.BottomLeftCorner, Qt.DockWidgetArea.BottomDockWidgetArea)
		self.setCorner(Qt.Corner.BottomRightCorner, Qt.DockWidgetArea.BottomDockWidgetArea)
		self.setCorner(Qt.Corner.TopLeftCorner, Qt.DockWidgetArea.TopDockWidgetArea)
		self.setCorner(Qt.Corner.TopRightCorner, Qt.DockWidgetArea.TopDockWidgetArea)

		central = QWidget()
		self.setCentralWidget(central)
		layout = QVBoxLayout(central)
		layout.setSpacing(8)
		layout.setContentsMargins(12, 10, 12, 10)

		header = QHBoxLayout()
		mark = QSvgWidget(str(HEADER_MARK))
		mark.setFixedSize(64, 64)
		header.addWidget(mark)
		titles = QVBoxLayout()
		title_row = QHBoxLayout()
		title_row.setSpacing(6)
		title_prefix = QLabel("Xash3D")
		title_prefix.setObjectName("Title")
		title_row.addWidget(title_prefix)
		logo = QSvgWidget(str(HEADER_LOGO))
		logo.setFixedSize(132, 100)
		title_row.addWidget(logo, 0, Qt.AlignmentFlag.AlignVCenter)
		title_row.addStretch()
		titles.addLayout(title_row)
		header.addLayout(titles)
		layout.addLayout(header)

		chip_row = QHBoxLayout()
		self.dol_chip = QLabel("DOL  —")
		self.iso_chip = QLabel("ISO  —")
		self.dolphin_chip = QLabel("DOLPHIN CHECKING")
		self.model_chip = QLabel("MODEL CHECKING")
		self.save_chip = QLabel("GIT SAVED")
		for chip in (self.dol_chip, self.iso_chip, self.dolphin_chip, self.model_chip, self.save_chip):
			chip.setObjectName("Chip")
			chip_row.addWidget(chip)
		chip_row.addStretch()
		layout.addLayout(chip_row)

		self.center_tabs = QTabWidget()
		self.center_tabs.setTabsClosable(True)
		self.center_tabs.tabCloseRequested.connect(self.restore_center_tab)
		self.center_bay = QLabel("CENTER DOCK BAY\nUse View > Dock Panel In Middle to park a panel here.")
		self.center_bay.setObjectName("CenterBay")
		self.center_bay.setAlignment(Qt.AlignmentFlag.AlignCenter)
		self.center_bay.setMinimumHeight(96)
		self.center_bay.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
		self.center_tabs.addTab(self.center_bay, "Center Bay")
		layout.addWidget(self.center_tabs, 1)

		self.configure_menus()

		progress_panel = QWidget()
		progress_layout = QVBoxLayout(progress_panel)
		progress_layout.setContentsMargins(10, 8, 10, 8)
		progress_layout.setSpacing(6)
		self.status_label = QLabel("Idle")
		self.progress = QProgressBar()
		self.progress.setRange(0, 1)
		self.progress.setValue(0)
		self.progress.setFormat("No automation running")
		progress_layout.addWidget(self.status_label)
		progress_layout.addWidget(self.progress)
		self.add_panel("Progress", progress_panel, Qt.DockWidgetArea.TopDockWidgetArea)

		workspace_panel = QWidget()
		form = QFormLayout(workspace_panel)
		self.repo_edit = QLineEdit(str(DEFAULT_REPO))
		browse = QPushButton("Browse…")
		browse.clicked.connect(self.pick_repo)
		repo_row = QHBoxLayout()
		repo_row.addWidget(self.repo_edit)
		repo_row.addWidget(browse)
		form.addRow("Xash3D repository:", repo_row)
		self.add_panel("Workspace", workspace_panel, Qt.DockWidgetArea.LeftDockWidgetArea)

		model_panel = QWidget()
		model_form = QGridLayout(model_panel)
		self.model_command_edit = QLineEdit(default_model_command())
		self.model_api_edit = QLineEdit(os.environ.get(
			"OPENAI_API_BASE", "http://127.0.0.1:8072/v1"))
		model_command_label = QLabel("Command:")
		model_api_label = QLabel("API base:")
		model_controls = QHBoxLayout()
		self.start_model_btn = QPushButton("▶  START")
		self.start_model_btn.clicked.connect(self.start_model)
		self.kill_model_btn = QPushButton("■  KILL")
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
		self.add_panel("Model Server", model_panel, Qt.DockWidgetArea.TopDockWidgetArea)

		goals_panel = QWidget()
		goals_layout = QVBoxLayout(goals_panel)
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
		goals_layout.addWidget(self.goal_table)
		self.add_panel("Goals", goals_panel, Qt.DockWidgetArea.LeftDockWidgetArea)

		context_panel = QWidget()
		context_layout = QVBoxLayout(context_panel)
		self.context_view = QPlainTextEdit()
		self.context_view.setReadOnly(True)
		context_layout.addWidget(self.context_view)
		self.add_panel("Telemetry", context_panel, Qt.DockWidgetArea.RightDockWidgetArea)

		viewport_panel = QWidget()
		viewport_layout = QVBoxLayout(viewport_panel)
		viewport_layout.setContentsMargins(10, 8, 10, 8)
		viewport_layout.setSpacing(6)
		self.viewport_status = QLabel("No Dolphin screenshot captured yet")
		self.viewport_status.setStyleSheet(f"color: {GC_CYAN}; font-weight: bold;")
		self.viewport_image = QLabel("Run Dolphin Screenshot Vision Test")
		self.viewport_image.setAlignment(Qt.AlignmentFlag.AlignCenter)
		self.viewport_image.setMinimumSize(320, 240)
		self.viewport_image.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
		self.viewport_image.setStyleSheet(
			f"background: #05040b; color: {GC_MUTED}; border: 2px inset {GC_PANEL_2};"
			"border-radius: 8px; padding: 10px;"
		)
		viewport_layout.addWidget(self.viewport_status)
		viewport_layout.addWidget(self.viewport_image, 1)
		self.viewport_pixmap: QPixmap | None = None
		self.viewport_path = ""
		self.add_panel("Dolphin Viewport", viewport_panel, Qt.DockWidgetArea.BottomDockWidgetArea)

		automation_panel = QWidget()
		controls = QHBoxLayout()
		automation_panel.setLayout(controls)
		self.passes_spin = QSpinBox()
		self.passes_spin.setRange(0, 100)
		self.passes_spin.setSpecialValueText("Unlimited")
		self.passes_spin.setValue(0)
		self.recovery_spin = QSpinBox()
		self.recovery_spin.setRange(1, 50)
		self.recovery_spin.setValue(8)
		self.loop_btn = QPushButton("▶  ACCOMPLISH GOALS")
		self.loop_btn.clicked.connect(self.run_goal_loop)
		self.stop_btn = QPushButton("■  STOP AUTOMATION")
		self.stop_btn.clicked.connect(self.stop_process)
		self.stop_btn.setEnabled(False)
		controls.addWidget(self.loop_btn, 2)
		controls.addWidget(QLabel("Pass limit:"))
		controls.addWidget(self.passes_spin)
		controls.addWidget(QLabel("Recovery retries:"))
		controls.addWidget(self.recovery_spin)
		controls.addWidget(self.stop_btn)
		self.add_panel("Automation", automation_panel, Qt.DockWidgetArea.TopDockWidgetArea)

		pipeline_panel = QWidget()
		pipeline_row = QHBoxLayout(pipeline_panel)
		for index, name in enumerate(("AIDER", "REVIEW", "VERIFY", "DOL", "ISO", "DOLPHIN", "VISION")):
			if index:
				arrow = QLabel("▶")
				arrow.setStyleSheet(f"color: {GC_CYAN};")
				pipeline_row.addWidget(arrow)
			node = QLabel(name)
			node.setAlignment(Qt.AlignmentFlag.AlignCenter)
			node.setObjectName("PipelineIdle")
			self.pipeline[name] = node
			pipeline_row.addWidget(node, 1)
		self.add_panel("Pipeline", pipeline_panel, Qt.DockWidgetArea.TopDockWidgetArea)

		tools_panel = QWidget()
		tools = QHBoxLayout()
		tools_panel.setLayout(tools)
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
		self.vision_btn = QPushButton("Dolphin Screenshot Vision Test")
		self.vision_btn.clicked.connect(self.run_dolphin_vision_test)
		tools.addWidget(self.vision_btn)
		self.add_panel("Tools", tools_panel, Qt.DockWidgetArea.TopDockWidgetArea)

		console_panel = QWidget()
		console_layout = QVBoxLayout(console_panel)
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
		self.follow_log = QCheckBox("Auto-scroll log")
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
		self.add_panel("Log", console_panel, Qt.DockWidgetArea.BottomDockWidgetArea)

		self.splitDockWidget(self.docks["Workspace"], self.docks["Goals"], Qt.Orientation.Vertical)
		self.splitDockWidget(self.docks["Telemetry"], self.docks["Log"], Qt.Orientation.Vertical)
		self.splitDockWidget(self.docks["Log"], self.docks["Dolphin Viewport"], Qt.Orientation.Horizontal)
		self.tabifyDockWidget(self.docks["Automation"], self.docks["Tools"])
		self.docks["Automation"].raise_()
		self.resizeDocks([self.docks["Goals"], self.docks["Telemetry"]], [520, 420], Qt.Orientation.Horizontal)
		self.resizeDocks([self.docks["Log"], self.docks["Telemetry"]], [320, 220], Qt.Orientation.Vertical)
		self.resizeDocks([self.docks["Log"], self.docks["Dolphin Viewport"]], [520, 420], Qt.Orientation.Horizontal)
		self.load_saved_settings()
		self.ensure_core_panels_visible()
		self.prime_goal_ledger()

		self.timer = QTimer(self)
		self.timer.setInterval(3000)
		self.timer.timeout.connect(self.refresh_dashboard)
		self.timer.start()
		self.refresh_dashboard()

	def configure_menus(self) -> None:
		self.file_menu = self.menuBar().addMenu("&File")
		save_settings_action = QAction("Save Settings", self)
		save_settings_action.setShortcut("Ctrl+S")
		save_settings_action.triggered.connect(self.save_settings)
		self.file_menu.addAction(save_settings_action)
		save_layout_action = QAction("Save Layout", self)
		save_layout_action.triggered.connect(self.save_layout)
		self.file_menu.addAction(save_layout_action)
		restore_layout_action = QAction("Restore Saved Layout", self)
		restore_layout_action.triggered.connect(self.restore_saved_layout)
		self.file_menu.addAction(restore_layout_action)
		self.file_menu.addSeparator()
		save_log_action = QAction("Save Console Log...", self)
		save_log_action.triggered.connect(self.save_log)
		self.file_menu.addAction(save_log_action)
		open_logs_action = QAction("Open Logs Folder", self)
		open_logs_action.triggered.connect(self.open_logs_folder)
		self.file_menu.addAction(open_logs_action)
		self.file_menu.addSeparator()
		quit_action = QAction("Quit", self)
		quit_action.setShortcut("Ctrl+Q")
		quit_action.triggered.connect(self.close)
		self.file_menu.addAction(quit_action)

		self.view_menu = self.menuBar().addMenu("&View")
		self.layout_menu = self.view_menu.addMenu("Layout")
		layout_save_action = QAction("Save Layout", self)
		layout_save_action.triggered.connect(self.save_layout)
		self.layout_menu.addAction(layout_save_action)
		layout_restore_action = QAction("Restore Saved Layout", self)
		layout_restore_action.triggered.connect(self.restore_saved_layout)
		self.layout_menu.addAction(layout_restore_action)
		layout_clear_action = QAction("Clear Saved Layout", self)
		layout_clear_action.triggered.connect(self.clear_saved_layout)
		self.layout_menu.addAction(layout_clear_action)
		reset_action = QAction("Reset Dock Layout", self)
		reset_action.triggered.connect(self.reset_dock_layout)
		self.layout_menu.addAction(reset_action)
		self.middle_menu = self.view_menu.addMenu("Dock Panel In Middle")
		self.view_menu.addSeparator()

		self.about_menu = self.menuBar().addMenu("&About")
		about_action = QAction("About Xash3D GameCube Porting", self)
		about_action.triggered.connect(self.show_about_panel)
		self.about_menu.addAction(about_action)

	def read_settings_file(self) -> dict[str, object]:
		if not SETTINGS_PATH.is_file():
			return {}
		try:
			data = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
		except (OSError, json.JSONDecodeError) as exc:
			self.status_label.setText(f"Settings unavailable: {exc}")
			return {}
		return dict(data) if isinstance(data, Mapping) else {}

	def current_settings(self, *, include_layout: bool) -> dict[str, object]:
		data: dict[str, object] = {
			"version": APP_VERSION,
			"repo": self.repo_edit.text().strip(),
			"model_command": self.model_command_edit.text().strip(),
			"model_api_base": self.model_api_edit.text().strip(),
			"pass_limit": self.passes_spin.value(),
			"recovery_retries": self.recovery_spin.value(),
			"follow_log": self.follow_log.isChecked(),
		}
		if include_layout:
			data["geometry"] = bytes(self.saveGeometry().toBase64()).decode("ascii")
			data["dock_layout"] = bytes(self.saveState().toBase64()).decode("ascii")
		return data

	def write_settings_file(self, *, include_layout: bool) -> bool:
		data = self.read_settings_file()
		data.update(self.current_settings(include_layout=include_layout))
		try:
			SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
			SETTINGS_PATH.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
				encoding="utf-8")
		except OSError as exc:
			QMessageBox.warning(self, "Save settings failed", str(exc))
			return False
		return True

	def save_settings(self) -> None:
		if self.write_settings_file(include_layout=False):
			self.status_label.setText(f"Saved settings: {SETTINGS_PATH.relative_to(DEFAULT_REPO)}")

	def save_layout(self) -> None:
		if self.write_settings_file(include_layout=True):
			self.status_label.setText(f"Saved layout: {SETTINGS_PATH.relative_to(DEFAULT_REPO)}")

	def restore_saved_layout(self) -> None:
		data = self.read_settings_file()
		if not self.apply_layout_settings(data):
			QMessageBox.information(self, "No saved layout",
				"Save a layout first with File > Save Layout or View > Layout > Save Layout.")
			return
		self.status_label.setText("Restored saved dock layout")

	def apply_layout_settings(self, data: Mapping[str, object]) -> bool:
		restored = False
		geometry = data.get("geometry")
		if isinstance(geometry, str) and geometry:
			restored = self.restoreGeometry(QByteArray.fromBase64(geometry.encode("ascii"))) or restored
		layout = data.get("dock_layout")
		if isinstance(layout, str) and layout:
			restored = self.restoreState(QByteArray.fromBase64(layout.encode("ascii"))) or restored
		return restored

	def load_saved_settings(self) -> None:
		data = self.read_settings_file()
		if not data:
			return
		repo = data.get("repo")
		if isinstance(repo, str) and repo:
			self.repo_edit.setText(repo)
		model_command = data.get("model_command")
		if isinstance(model_command, str) and model_command:
			self.model_command_edit.setText(model_command)
		model_api_base = data.get("model_api_base")
		if isinstance(model_api_base, str) and model_api_base:
			self.model_api_edit.setText(model_api_base)
		pass_limit = data.get("pass_limit")
		if isinstance(pass_limit, int):
			self.passes_spin.setValue(max(self.passes_spin.minimum(), min(self.passes_spin.maximum(), pass_limit)))
		recovery_retries = data.get("recovery_retries")
		if isinstance(recovery_retries, int):
			self.recovery_spin.setValue(max(self.recovery_spin.minimum(), min(self.recovery_spin.maximum(), recovery_retries)))
		follow_log = data.get("follow_log")
		if isinstance(follow_log, bool):
			self.follow_log.setChecked(follow_log)

	def ensure_core_panels_visible(self) -> None:
		for title in ("Goals", "Progress", "Workspace"):
			dock = self.docks.get(title)
			if dock:
				dock.show()
		if "Goals" in self.docks:
			self.docks["Goals"].raise_()

	def show_about_panel(self) -> None:
		dialog = QDialog(self)
		dialog.setWindowTitle("About Xash3D GameCube Porting")
		dialog.setMinimumWidth(520)
		layout = QVBoxLayout(dialog)

		logos = QHBoxLayout()
		mark = QSvgWidget(str(HEADER_MARK))
		mark.setFixedSize(88, 88)
		logos.addWidget(mark, 0, Qt.AlignmentFlag.AlignCenter)
		wordmark = QSvgWidget(str(HEADER_LOGO))
		wordmark.setFixedSize(176, 116)
		logos.addWidget(wordmark, 0, Qt.AlignmentFlag.AlignCenter)
		logos.addStretch()
		layout.addLayout(logos)

		title = QLabel("Xash3D GameCube Porting Console")
		title.setObjectName("Title")
		layout.addWidget(title)
		version = QLabel(f"Version {APP_VERSION}")
		version.setStyleSheet(f"color: {GC_CYAN}; font-weight: bold;")
		layout.addWidget(version)
		body = QLabel(
			"Goal-driven PyQt6 cockpit for building, testing, and steering the "
			"native Half-Life / Xash3D GameCube port.\n\n"
			f"Settings file: {SETTINGS_PATH.relative_to(DEFAULT_REPO)}"
		)
		body.setWordWrap(True)
		layout.addWidget(body)

		buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok)
		buttons.accepted.connect(dialog.accept)
		layout.addWidget(buttons)
		dialog.exec()

	def add_panel(self, title: str, widget: QWidget, area: Qt.DockWidgetArea) -> QDockWidget:
		dock = QDockWidget(title, self)
		dock.setObjectName(f"Dock{re.sub(r'[^A-Za-z0-9]+', '', title)}")
		dock.setWidget(widget)
		dock.setAllowedAreas(Qt.DockWidgetArea.AllDockWidgetAreas)
		dock.setFloating(False)
		dock.setFeatures(
			QDockWidget.DockWidgetFeature.DockWidgetClosable |
			QDockWidget.DockWidgetFeature.DockWidgetMovable |
			QDockWidget.DockWidgetFeature.DockWidgetFloatable
		)
		dock.topLevelChanged.connect(lambda floating, item=dock: self.dock_floating_changed(item, floating))
		self.addDockWidget(area, dock)
		self.docks[title] = dock
		self.view_menu.addAction(dock.toggleViewAction())
		action = QAction(title, self)
		action.triggered.connect(lambda _checked=False, name=title: self.dock_panel_middle(name))
		self.middle_menu.addAction(action)
		return dock

	def apply_floating_window_flags(self, dock: QDockWidget) -> None:
		dock.setWindowFlags(
			Qt.WindowType.Window |
			Qt.WindowType.WindowTitleHint |
			Qt.WindowType.WindowSystemMenuHint |
			Qt.WindowType.WindowMinMaxButtonsHint |
			Qt.WindowType.WindowCloseButtonHint
		)

	def dock_floating_changed(self, dock: QDockWidget, floating: bool) -> None:
		if not floating:
			return
		# Keep Qt's native floating-dock window handling. Replacing flags here can
		# destabilize some window managers during drag/drop.

	def dock_panel_middle(self, title: str) -> None:
		dock = self.docks.get(title)
		if not dock:
			return
		widget = dock.widget()
		if widget is None:
			return
		if self.center_tabs.indexOf(widget) < 0:
			dock.setFloating(False)
			dock.hide()
			self.center_widgets[title] = widget
			self.center_tabs.addTab(widget, title)
		self.center_tabs.setCurrentWidget(widget)
		self.status_label.setText(f"Docked {title} in middle workspace")

	def restore_center_tab(self, index: int) -> None:
		if index <= 0:
			return
		title = self.center_tabs.tabText(index)
		widget = self.center_tabs.widget(index)
		dock = self.docks.get(title)
		if not dock or widget is None:
			return
		self.center_tabs.removeTab(index)
		self.center_widgets.pop(title, None)
		dock.setWidget(widget)
		dock.show()
		dock.setFloating(False)
		self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, dock)
		dock.raise_()
		self.status_label.setText(f"Restored {title} to dock layout")

	def restore_all_center_tabs(self) -> None:
		for index in range(self.center_tabs.count() - 1, 0, -1):
			self.restore_center_tab(index)

	def reset_dock_layout(self) -> None:
		required = {"Workspace", "Model Server", "Goals", "Telemetry",
			"Automation", "Pipeline", "Tools", "Dolphin Viewport", "Log"}
		if not required.issubset(self.docks):
			return
		self.restore_all_center_tabs()
		for dock in self.docks.values():
			dock.show()
			dock.setFloating(False)
		self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.docks["Workspace"])
		self.addDockWidget(Qt.DockWidgetArea.TopDockWidgetArea, self.docks["Model Server"])
		self.addDockWidget(Qt.DockWidgetArea.TopDockWidgetArea, self.docks["Automation"])
		self.addDockWidget(Qt.DockWidgetArea.TopDockWidgetArea, self.docks["Pipeline"])
		self.addDockWidget(Qt.DockWidgetArea.TopDockWidgetArea, self.docks["Tools"])
		self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.docks["Goals"])
		self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.docks["Telemetry"])
		self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, self.docks["Dolphin Viewport"])
		self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, self.docks["Log"])
		self.splitDockWidget(self.docks["Workspace"], self.docks["Goals"], Qt.Orientation.Vertical)
		self.splitDockWidget(self.docks["Telemetry"], self.docks["Log"], Qt.Orientation.Vertical)
		self.splitDockWidget(self.docks["Log"], self.docks["Dolphin Viewport"], Qt.Orientation.Horizontal)
		self.tabifyDockWidget(self.docks["Automation"], self.docks["Tools"])
		self.docks["Automation"].raise_()
		self.resizeDocks([self.docks["Goals"], self.docks["Telemetry"]], [520, 420], Qt.Orientation.Horizontal)
		self.resizeDocks([self.docks["Log"], self.docks["Telemetry"]], [320, 220], Qt.Orientation.Vertical)
		self.resizeDocks([self.docks["Log"], self.docks["Dolphin Viewport"]], [520, 420], Qt.Orientation.Horizontal)
		self.clear_saved_layout(silent=True)
		self.status_label.setText("Dock layout reset")

	def clear_saved_layout(self, *, silent: bool = False) -> None:
		data = self.read_settings_file()
		if not data:
			return
		data.pop("geometry", None)
		data.pop("dock_layout", None)
		try:
			SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
			SETTINGS_PATH.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
				encoding="utf-8")
		except OSError as exc:
			if not silent:
				QMessageBox.warning(self, "Clear layout failed", str(exc))
			return
		if not silent:
			self.status_label.setText("Cleared saved dock layout")

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
		if self.closing or not hasattr(self, "log"):
			return
		visible_cursor = self.log.textCursor()
		had_selection = visible_cursor.hasSelection()
		position = visible_cursor.position()
		anchor = visible_cursor.anchor()
		scrollbar = self.log.verticalScrollBar()
		scroll_position = scrollbar.value()

		writer = QTextCursor(self.log.document())
		writer.movePosition(QTextCursor.MoveOperation.End)
		writer.insertText(text)

		if self.follow_log.isChecked():
			follow = QTextCursor(self.log.document())
			follow.movePosition(QTextCursor.MoveOperation.End)
			self.log.setTextCursor(follow)
			self.log.ensureCursorVisible()
			scrollbar.setValue(scrollbar.maximum())
			return
		if had_selection:
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
		if self.closing:
			return
		self.model_chip.setText(text)
		self.model_chip.setStyleSheet(f"color: {color};")

	def process_error(self, error: QProcess.ProcessError) -> None:
		if not self.closing:
			self.append(f"\nProcess error: {error.name}\n")

	def model_process_error(self, error: QProcess.ProcessError, program: str) -> None:
		if not self.closing:
			self.append(f"\nModel process error: {error.name} while starting {program}\n")

	def start_model(self) -> None:
		if not self.valid_repo():
			return
		if self.model_process is not None:
			self.status_label.setText("qwable-5 is already managed by this GUI")
			return
		if self.model_port_open():
			self.set_model_state("MODEL  READY", GC_MINT)
			self.start_model_btn.setEnabled(False)
			self.kill_model_btn.setEnabled(True)
			self.status_label.setText("Reusing existing qwable-5 API server")
			self.append("\nqwable-5 API is already reachable; not launching a duplicate model server.\n")
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
		gpu_problem = gpu_memory_preflight_message(command)
		if gpu_problem:
			self.set_model_state("MODEL  GPU BUSY", GC_ORANGE)
			self.status_label.setText("Model launch blocked by GPU memory preflight")
			self.append(f"\nModel GPU memory preflight blocked launch:\n{gpu_problem}\n")
			QMessageBox.warning(self, "GPU memory too low", gpu_problem)
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
			lambda error, program=command[0]: self.model_process_error(error, program))
		self.model_process = proc
		proc.start(command[0], command[1:])

	def read_model_output(self) -> None:
		if self.closing or self.model_process is None:
			return
		text = bytes(self.model_process.readAllStandardOutput()).decode(errors="replace")
		self.append(text)

	def model_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
		if self.closing:
			self.model_process = None
			return
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
		return git_output_for_repo(self.repo(), *args)

	def set_save_state(self, text: str, color: str) -> None:
		self.save_chip.setText(text)
		self.save_chip.setStyleSheet(f"color: {color};")

	def read_goals(self) -> list[tuple[str, str, str, str]]:
		return read_goals_for_repo(self.repo())

	def prime_goal_ledger(self) -> None:
		try:
			goals = self.read_goals()
		except OSError as exc:
			self.goal_table.setRowCount(0)
			self.goal_summary.setText(f"Goal ledger unavailable: {exc}")
			return
		snapshot = DashboardSnapshot(goals=goals)
		snapshot.complete_goals = sum(state.lower() == "x" for _, state, _, _ in goals)
		snapshot.blocked_goals = sum(goal_is_blocked(body) for _, state, _, body in goals if state != "MANUAL")
		snapshot.automatic_goals = sum(state != "MANUAL" for _, state, _, _ in goals)
		snapshot.active_goal = next((goal for goal in goals
			if goal[1] in {" ", "~"} and not goal_is_blocked(goal[3])), None)
		self.apply_goals_snapshot(snapshot)

	def apply_goals_snapshot(self, snapshot: DashboardSnapshot) -> None:
		goals = snapshot.goals or []
		self.goal_table.setRowCount(len(goals))
		if not goals:
			ledger = self.repo() / ".ai/goals/GAMECUBE_PORT_GOALS.md"
			self.goal_summary.setText(f"No goal ledger loaded from {ledger}")
			return
		active = snapshot.active_goal
		for row, (goal_id, state, title, body) in enumerate(goals):
			is_blocked = goal_is_blocked(body)
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
		self.progress.setToolTip(
			f"{snapshot.complete_goals}/{snapshot.automatic_goals} automatic goals complete, "
			f"{snapshot.blocked_goals} blocked"
		)

	def refresh_context(self) -> None:
		snapshot = build_dashboard_snapshot(self.repo(), *self.model_host_port())
		self.apply_context_snapshot(snapshot)

	def apply_context_snapshot(self, snapshot: DashboardSnapshot) -> None:
		if snapshot.error:
			self.context_view.setPlainText(snapshot.error)
			return
		if snapshot.context and snapshot.context != self.last_context:
			self.context_view.setPlainText(snapshot.context)
			self.last_context = snapshot.context

	def latest_dolphin_screenshot(self) -> tuple[Path | None, str]:
		path, status = latest_dolphin_screenshot_for_repo(self.repo())
		return (Path(path), status) if path else (None, status)

	def refresh_dolphin_viewport(self) -> None:
		path, status = self.latest_dolphin_screenshot()
		self.apply_dolphin_viewport_snapshot(path, status)

	def apply_dolphin_viewport_snapshot(self, path: Path | None, status: str) -> None:
		if path is None:
			self.viewport_path = ""
			self.viewport_pixmap = None
			self.viewport_status.setText(status)
			self.viewport_image.setPixmap(QPixmap())
			self.viewport_image.setText(
				"Run Dolphin Screenshot Vision Test\n\n"
				"Waiting for .ai/logs/dolphin-vision-*/screenshots/*.png"
			)
			return

		try:
			path_text = str(path.relative_to(self.repo()))
		except ValueError:
			path_text = str(path)
		if path_text != self.viewport_path:
			pixmap = QPixmap(str(path))
			if not pixmap.isNull():
				self.viewport_pixmap = pixmap
				self.viewport_path = path_text
		self.viewport_status.setText(f"{status}  -  {path_text}")
		self.update_viewport_pixmap()

	def update_viewport_pixmap(self) -> None:
		if not self.viewport_pixmap or self.viewport_pixmap.isNull():
			return
		size = self.viewport_image.size()
		scaled = self.viewport_pixmap.scaled(
			size,
			Qt.AspectRatioMode.KeepAspectRatio,
			Qt.TransformationMode.SmoothTransformation,
		)
		self.viewport_image.setText("")
		self.viewport_image.setPixmap(scaled)

	def refresh_dashboard(self) -> None:
		if self.closing:
			return
		if self.dashboard_refresh_running:
			self.dashboard_refresh_pending = True
			return
		try:
			repo = self.repo()
			model_host, model_port = self.model_host_port()
		except OSError as exc:
			self.context_view.setPlainText(f"Telemetry unavailable: {exc}")
			return
		self.dashboard_refresh_running = True
		thread = QThread(self)
		worker = DashboardWorker(repo, model_host, model_port)
		worker.moveToThread(thread)
		thread.started.connect(worker.run)
		worker.finished.connect(self.apply_dashboard_snapshot)
		worker.finished.connect(thread.quit)
		worker.finished.connect(worker.deleteLater)
		thread.finished.connect(thread.deleteLater)
		thread.finished.connect(lambda item=thread: self.dashboard_thread_finished(item))
		self.dashboard_threads.append(thread)
		thread.start()

	def dashboard_thread_finished(self, thread: QThread) -> None:
		if thread in self.dashboard_threads:
			self.dashboard_threads.remove(thread)

	def apply_dashboard_snapshot(self, snapshot: DashboardSnapshot) -> None:
		if self.closing:
			return
		self.dashboard_refresh_running = False
		self.dol_chip.setText(snapshot.dol_text)
		self.iso_chip.setText(snapshot.iso_text)
		active_node = self.pipeline_node(self.operation) if self.process else ""
		if active_node != "DOL":
			self.set_pipeline_state("DOL", "Success" if snapshot.dol_exists else "Idle")
		if active_node != "ISO":
			self.set_pipeline_state("ISO", "Success" if snapshot.iso_exists else "Idle")
		self.dolphin_chip.setText("DOLPHIN  READY" if snapshot.dolphin_ready else "DOLPHIN  MISSING")
		if self.process is None:
			self.status_label.setText("Idle - goal console ready")
		if self.model_process is not None:
			self.set_model_state("MODEL  RUNNING", GC_CYAN)
			self.start_model_btn.setEnabled(False)
			self.kill_model_btn.setEnabled(True)
		elif snapshot.model_ready:
			self.set_model_state("MODEL  READY", GC_MINT)
			self.start_model_btn.setEnabled(False)
			self.kill_model_btn.setEnabled(True)
		else:
			self.set_model_state("MODEL  DOWN", GC_ORANGE)
			self.start_model_btn.setEnabled(True)
			self.kill_model_btn.setEnabled(True)
		self.apply_goals_snapshot(snapshot)
		self.apply_context_snapshot(snapshot)
		path = Path(snapshot.screenshot_path) if snapshot.screenshot_path else None
		self.apply_dolphin_viewport_snapshot(path, snapshot.screenshot_status)
		if self.dashboard_refresh_pending:
			self.dashboard_refresh_pending = False
			QTimer.singleShot(0, self.refresh_dashboard)

	def start(self, command: list[str], operation: str, passes: int = 1) -> None:
		if self.process is not None or not self.valid_repo():
			return
		self.user_stopping = False
		self.last_command = list(command)
		self.last_passes = passes
		self.operation = operation
		self.start_head = self.git_output("rev-parse", "HEAD")
		self.expected_passes = passes
		if passes == 0 and operation == "Goal automation":
			self.progress.setRange(0, 0)
		else:
			self.progress.setRange(0, passes)
		self.progress.setValue(0)
		if passes == 0 and operation == "Goal automation":
			self.progress.setFormat(f"{operation}: running until complete")
		else:
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
		env.insert("OPENAI_API_BASE", self.model_api_edit.text().strip())
		proc.setProcessEnvironment(env)
		proc.readyReadStandardOutput.connect(self.read_output)
		proc.finished.connect(self.finished)
		proc.errorOccurred.connect(self.process_error)
		self.process = proc
		proc.start(command[0], command[1:])

	def run_goal_loop(self) -> None:
		passes = self.passes_spin.value()
		recoveries = self.recovery_spin.value()
		self.start(["scripts/ai-run-until-done.py", "--repo", str(self.repo()),
			"--chunk-passes", str(passes), "--max-cycles", "0",
			"--recoverable-retries", str(recoveries), "--sleep", "10"],
			"Goal automation", passes)

	def read_output(self) -> None:
		if self.closing or self.process is None:
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
		if "SCREENSHOT:" in text or "== vision analysis ==" in text:
			self.set_pipeline_state("VISION", "Running")
		if "Logs: .ai/logs/dolphin-vision-" in text:
			self.set_pipeline_state("VISION", "Success")
		if self.operation == "Goal automation":
			for line in text.splitlines():
				if line.startswith("GOAL PASS "):
					try:
						pass_value = int(line.split()[2].split("/")[0])
						if self.expected_passes == 0:
							self.progress.setFormat(f"Goal automation: pass {pass_value}")
						else:
							self.progress.setValue(max(0, pass_value - 1))
					except (IndexError, ValueError):
						pass

	def pipeline_node(self, operation: str) -> str:
		return {
			"Goal automation": "AIDER", "Review HEAD": "REVIEW", "Verify": "VERIFY",
			"Build DOL": "DOL", "Build disc ISO": "ISO",
			"Dolphin vision test": "VISION",
		}.get(operation, "AIDER")

	def set_pipeline_state(self, name: str, state: str) -> None:
		node = self.pipeline.get(name)
		if node:
			node.setObjectName(f"Pipeline{state}")
			node.style().unpolish(node)
			node.style().polish(node)

	def finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
		if self.closing:
			self.process = None
			return
		operation = self.operation
		succeeded = exit_code == 0
		if succeeded:
			if self.expected_passes == 0 and operation == "Goal automation":
				self.progress.setRange(0, 1)
				self.progress.setValue(1)
				self.progress.setFormat("Goal automation: complete")
			else:
				self.progress.setValue(self.expected_passes)
		self.set_pipeline_state(self.pipeline_node(operation), "Success" if succeeded else "Failed")
		self.status_label.setText(f"{'Passed' if succeeded else 'Failed'}: {operation} (exit {exit_code})")
		self.append(f"\n[{operation} exited {exit_code}]\n")
		boot_after_build = self.pending_boot and operation == "Build disc ISO" and succeeded
		if operation == "Build disc ISO":
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
		elif operation == "Goal automation":
			self.set_save_state("NO CHANGE", GC_ORANGE)
		else:
			self.set_save_state("UNCHANGED", GC_MUTED)
		if boot_after_build:
			self.launch_dolphin()
		if operation == "Goal automation" and not succeeded and not self.user_stopping:
			self.append("\n[Goal automation restarting in 10s]\n")
			self.status_label.setText("Restarting goal automation after worker exit")
			QTimer.singleShot(10000, self.restart_goal_automation)

	def restart_goal_automation(self) -> None:
		if self.closing or self.process is not None or not self.last_command:
			return
		self.start(self.last_command, "Goal automation", self.last_passes)

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
		self.user_stopping = True
		self.append("\nStopping process…\n")
		self.process.terminate()
		QTimer.singleShot(3000, lambda: self.process.kill() if self.process else None)

	def closeEvent(self, event: QCloseEvent) -> None:
		if self.process is None and self.model_process is None:
			self.closing = True
			self.timer.stop()
			self.shutdown_dashboard_threads()
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
			self.closing = True
			self.timer.stop()
			self.shutdown_dashboard_threads()
			self.shutdown_processes()
			event.accept()
		else:
			event.ignore()

	def stop_qprocess(self, proc: QProcess | None, *, timeout_ms: int = 3000) -> None:
		if proc is None:
			return
		try:
			proc.readyReadStandardOutput.disconnect()
		except TypeError:
			pass
		try:
			proc.finished.disconnect()
		except TypeError:
			pass
		try:
			proc.errorOccurred.disconnect()
		except TypeError:
			pass
		if proc.state() != QProcess.ProcessState.NotRunning:
			proc.terminate()
			if not proc.waitForFinished(timeout_ms):
				proc.kill()
				proc.waitForFinished(1000)

	def resizeEvent(self, event) -> None:
		super().resizeEvent(event)
		if hasattr(self, "viewport_pixmap"):
			self.update_viewport_pixmap()

	def shutdown_processes(self) -> None:
		self.stop_qprocess(self.process)
		self.stop_qprocess(self.model_process)
		self.process = None
		self.model_process = None

	def shutdown_dashboard_threads(self) -> None:
		self.dashboard_refresh_pending = False
		for thread in list(self.dashboard_threads):
			thread.quit()
			thread.wait(1500)
		self.dashboard_threads.clear()
		self.dashboard_refresh_running = False

	def boot_dolphin(self) -> None:
		iso = self.repo() / "OUT/xash3d-gc.iso"
		if not iso.is_file():
			self.pending_boot = True
			self.append("\nISO missing; building OUT/xash3d-gc.iso before Dolphin launch.\n")
			self.start(["scripts/build-gamecube-disc.py", "--output", "OUT/xash3d-gc.iso"],
				"Build disc ISO")
			return
		self.launch_dolphin()

	def run_dolphin_vision_test(self) -> None:
		self.start([
			"scripts/dolphin-vision-test.py",
			"--repo", str(self.repo()),
			"--api-base", self.model_api_edit.text().strip(),
		], "Dolphin vision test")

	def launch_dolphin(self) -> None:
		iso = self.repo() / "OUT/xash3d-gc.iso"
		dolphin_executable = os.environ.get("DOLPHIN_EXECUTABLE", "")
		dolphin_flatpak_id = os.environ.get("DOLPHIN_FLATPAK_ID", "org.DolphinEmu.dolphin-emu")
		if dolphin_executable.startswith("flatpak:"):
			command = ["flatpak", "run", dolphin_executable.removeprefix("flatpak:"), "-e", str(iso)]
		elif dolphin_executable:
			command = [dolphin_executable, "-e", str(iso)]
		elif shutil.which("dolphin-emu"):
			command = ["dolphin-emu", "-e", str(iso)]
		elif shutil.which("dolphin"):
			command = ["dolphin", "-e", str(iso)]
		elif shutil.which("flatpak"):
			command = ["flatpak", "run", dolphin_flatpak_id, "-e", str(iso)]
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
		dolphin_ready = bool(os.environ.get("DOLPHIN_EXECUTABLE") or
			shutil.which("dolphin-emu") or shutil.which("dolphin") or shutil.which("flatpak"))
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
	font = QFont(rodin_family, 9)
	font.setStyleHint(QFont.StyleHint.SansSerif)
	app.setFont(font)
	window = PortWindow(gamecube_family)
	window.show()
	return app.exec()


if __name__ == "__main__":
	raise SystemExit(main())
