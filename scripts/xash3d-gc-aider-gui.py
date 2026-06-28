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
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path
from urllib.parse import urlparse

from PyQt6.QtCore import QByteArray, QObject, QProcess, QProcessEnvironment, Qt, QThread, QTimer, QUrl, pyqtSignal
from PyQt6.QtGui import (
	QAction, QCloseEvent, QDesktopServices, QFont, QFontDatabase, QPixmap, QTextCharFormat,
	QTextCursor, QColor,
)
from PyQt6.QtSvgWidgets import QSvgWidget
from PyQt6.QtWidgets import (
	QApplication,
	QCheckBox,
	QComboBox,
	QDialog,
	QDialogButtonBox,
	QDoubleSpinBox,
	QDockWidget,
	QFileDialog,
	QFormLayout,
	QFrame,
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
	QScrollArea,
	QSizePolicy,
	QSpinBox,
	QStatusBar,
	QTabWidget,
	QTableWidget,
	QTableWidgetItem,
	QVBoxLayout,
	QWidget,
)

DEFAULT_REPO = Path(__file__).resolve().parents[1]
APP_VERSION = "0.6.4-dev"
DEFAULT_LAYOUT_VERSION = 3

DEFAULT_DOCK_AREAS: dict[str, Qt.DockWidgetArea] = {
	"Progress": Qt.DockWidgetArea.TopDockWidgetArea,
	"Model Server": Qt.DockWidgetArea.TopDockWidgetArea,
	"Automation": Qt.DockWidgetArea.TopDockWidgetArea,
	"Pipeline": Qt.DockWidgetArea.TopDockWidgetArea,
	"Tools": Qt.DockWidgetArea.TopDockWidgetArea,
	"Workspace": Qt.DockWidgetArea.LeftDockWidgetArea,
	"Goals": Qt.DockWidgetArea.LeftDockWidgetArea,
	"Telemetry": Qt.DockWidgetArea.RightDockWidgetArea,
	"Dolphin Viewport": Qt.DockWidgetArea.BottomDockWidgetArea,
	"Log": Qt.DockWidgetArea.BottomDockWidgetArea,
}
TOP_DOCK_TITLES = ("Progress", "Model Server", "Automation", "Pipeline", "Tools")
SETTINGS_PATH = DEFAULT_REPO / ".ai/state/xash3d-gc-aider-gui-settings.json"
OVERNIGHT_SESSION_PATH = DEFAULT_REPO / ".ai/state/overnight-session-latest.md"
GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |~|x|X|MANUAL)\]\s+(.+)$")
QWABLE_5_MODEL_ID = "DJLougen/Qwable-5-27B-Coder"
QWABLE_5_SERVED_NAME = "qwen-local"
HEADER_LOGO = DEFAULT_REPO / "assets/ui/nintendo-gamecube-logo.svg"
HEADER_MARK = DEFAULT_REPO / "assets/ui/gamecube-mark.svg"
DOCK_CLOSE_ICON = DEFAULT_REPO / "assets/ui/dock-close-white.svg"
DOCK_FLOAT_ICON = DEFAULT_REPO / "assets/ui/dock-float-white.svg"

GC_BG = "#090814"
GC_PANEL = "#171229"
GC_PANEL_2 = "#271f4b"
GC_PANEL_3 = "#382d68"
GC_INPUT = "#0d0b18"
GC_TEXT = "#f8f6ff"
GC_MUTED = "#b9b0df"
GC_VIOLET = "#6656bf"
GC_PURPLE = "#4f46a8"
GC_BORDER = "#786dd8"
GC_CYAN = "#5fe3ff"
GC_ORANGE = "#ffb14a"
GC_MINT = "#7dffc7"
GC_SILVER = "#ded9f5"
GC_RED = "#ff6f86"
LOG_HIGHLIGHT_RULES: tuple[tuple[re.Pattern[str], str], ...] = (
	(re.compile(r"(?i)(MAP_READY|G36_STATUS|FRAME_BUDGET_STATS|RC check passed)"), GC_MINT),
	(re.compile(r"(?i)(GUEST_FAILURE|BOOT_FAILURE|FAIL:|RC check failed|out of memory|Xash Error)"), GC_ORANGE),
	(re.compile(r"(?i)(WARNING|WARN:|blocked|OVER_BUDGET)"), GC_ORANGE),
	(re.compile(r"(?i)(verify: OK|GIT SAVED|accepted patch|ENGINE_READY)"), GC_CYAN),
)


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
		"--max-num-seqs", os.environ.get("QWABLE_5_MAX_NUM_SEQS", "1"),
		"--gpu-memory-utilization", os.environ.get("QWABLE_5_GPU_MEMORY_UTILIZATION", "0.85"),
	]
	reasoning_parser = os.environ.get("QWABLE_5_REASONING_PARSER", "").strip()
	if reasoning_parser:
		command.extend(["--reasoning-parser", reasoning_parser])
	if os.environ.get("QWABLE_5_ENABLE_TOOL_CHOICE", "").strip() in {"1", "true", "yes"}:
		command.extend([
			"--enable-auto-tool-choice",
			"--tool-call-parser", "qwen3_coder",
		])
	return shlex.join(command)


def load_gamecube_env(repo: Path) -> None:
	"""Apply scripts/gamecube-env.sh exports without overriding the parent shell."""
	script = repo / "scripts/gamecube-env.sh"
	if not script.is_file():
		return
	try:
		result = subprocess.run(
			["bash", "-c", f"set -a; source '{script}'; env -0"],
			cwd=repo, capture_output=True, timeout=5, check=False,
		)
	except (OSError, subprocess.SubprocessError):
		return
	if result.returncode != 0:
		return
	for entry in result.stdout.split(b"\0"):
		if not entry or b"=" not in entry:
			continue
		key, value = entry.split(b"=", 1)
		name = key.decode(errors="replace")
		if name and name not in os.environ:
			os.environ[name] = value.decode(errors="replace")


def command_flag_value(command: list[str], flag: str) -> str | None:
	for index, arg in enumerate(command):
		if arg == flag and index + 1 < len(command):
			return command[index + 1]
		if arg.startswith(f"{flag}="):
			return arg.split("=", 1)[1]
	return None


def command_has_flag(command: list[str], flag: str) -> bool:
	return any(arg == flag or arg.startswith(f"{flag}=") for arg in command)


def migrate_model_command(command_text: str) -> str:
	"""Upgrade saved launchers that still use heavy vLLM defaults."""
	try:
		command = shlex.split(command_text)
	except ValueError:
		return command_text
	if not command:
		return command_text
	needs_rebuild = False
	seqs = command_flag_value(command, "--max-num-seqs")
	if seqs and seqs.isdigit() and int(seqs) > 2:
		needs_rebuild = True
	if command_has_flag(command, "--enable-auto-tool-choice") and \
		os.environ.get("QWABLE_5_ENABLE_TOOL_CHOICE", "").strip().lower() not in {"1", "true", "yes"}:
		needs_rebuild = True
	if command_has_flag(command, "--reasoning-parser") and \
		not os.environ.get("QWABLE_5_REASONING_PARSER", "").strip():
		needs_rebuild = True
	if needs_rebuild:
		return vllm_qwable_command()
	return command_text


def apply_model_tuning_to_environ(
	max_num_seqs: int,
	gpu_util: float,
	max_model_len: int,
	tool_choice: bool,
	aider_history: int,
	aider_overhead: int,
	reasoning_parser: bool = False,
) -> None:
	os.environ["QWABLE_5_MAX_NUM_SEQS"] = str(max_num_seqs)
	os.environ["QWABLE_5_GPU_MEMORY_UTILIZATION"] = f"{gpu_util:.2f}"
	os.environ["QWABLE_5_MAX_MODEL_LEN"] = str(max_model_len)
	os.environ["AIDER_MAX_CHAT_HISTORY_TOKENS"] = str(aider_history)
	os.environ["AIDER_SYSTEM_OVERHEAD_TOKENS"] = str(aider_overhead)
	if tool_choice:
		os.environ["QWABLE_5_ENABLE_TOOL_CHOICE"] = "1"
	else:
		os.environ.pop("QWABLE_5_ENABLE_TOOL_CHOICE", None)
	if reasoning_parser:
		os.environ["QWABLE_5_REASONING_PARSER"] = "qwen3"
	else:
		os.environ.pop("QWABLE_5_REASONING_PARSER", None)


def fetch_model_api_summary(api_base: str) -> str:
	url = f"{api_base.rstrip('/')}/models"
	headers = {"Accept": "application/json"}
	api_key = os.environ.get("OPENAI_API_KEY", "local")
	if api_key:
		headers["Authorization"] = f"Bearer {api_key}"
	request = urllib.request.Request(url, headers=headers)
	try:
		with urllib.request.urlopen(request, timeout=2) as response:
			payload = json.load(response)
	except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
		return f"Model API offline: {exc}"
	models = payload.get("data", []) if isinstance(payload, dict) else []
	if not models:
		return "Model API reachable but returned no models"
	preferred = next((item for item in models if item.get("id") == QWABLE_5_SERVED_NAME), models[0])
	model_id = preferred.get("id", "?")
	for key in ("max_model_len", "context_length", "max_context_length"):
		value = preferred.get(key)
		if isinstance(value, int) and value > 0:
			return f"Model API: {model_id}  context={value:,}  models={len(models)}"
	return f"Model API: {model_id}  models={len(models)}"


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


def stylesheet(gamecube_font: str = "Sans Serif") -> str:
	gc_font = gamecube_font.replace('"', "")
	return f"""
	QMainWindow {{
		background: qradialgradient(cx:0.2, cy:0.05, radius:1.1,
			stop:0 #1f1a3d, stop:0.45 {GC_BG}, stop:1 #05050c);
		color: {GC_TEXT};
	}}
	QWidget {{ color: {GC_TEXT}; selection-background-color: {GC_PURPLE}; }}
	QWidget#AppCentral {{ background: transparent; }}
	QWidget[panelSurface="true"] {{ background: {GC_PANEL}; border: 1px solid {GC_PANEL_3};
		border-radius: 10px; }}
	QDialog, QMessageBox {{ background: {GC_BG}; color: {GC_TEXT}; }}
	QToolTip {{ background: #151225; color: {GC_TEXT}; border: 1px solid {GC_CYAN};
		border-radius: 6px; padding: 6px; }}
	QStatusBar {{ background: #0e0b1b; color: {GC_MUTED}; border-top: 1px solid {GC_PANEL_3}; }}
	QStatusBar::item {{ border: 0; }}

	QMenuBar {{ background: #0d0a18; color: {GC_TEXT}; border-bottom: 1px solid {GC_PANEL_3}; }}
	QMenuBar::item {{ padding: 6px 12px; background: transparent; border-radius: 8px; }}
	QMenuBar::item:selected {{ background: {GC_PANEL_2}; color: {GC_CYAN}; }}
	QMenu {{ background: #141022; color: {GC_TEXT}; border: 1px solid {GC_BORDER};
		border-radius: 10px; padding: 6px; }}
	QMenu::item {{ padding: 7px 28px; border-radius: 7px; }}
	QMenu::item:selected {{ background: {GC_PURPLE}; color: {GC_TEXT}; }}
	QMenu::separator {{ height: 1px; background: {GC_PANEL_3}; margin: 5px 10px; }}

	QMainWindow::separator {{ background: {GC_PANEL_2}; width: 5px; height: 5px; }}
	QMainWindow::separator:hover {{ background: {GC_CYAN}; }}
	QDockWidget {{ titlebar-close-icon: url({DOCK_CLOSE_ICON.as_posix()});
		titlebar-normal-icon: url({DOCK_FLOAT_ICON.as_posix()});
		background: {GC_PANEL}; border: 0; }}
	QDockWidget::title {{ background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
		stop:0 {GC_PANEL_3}, stop:0.35 {GC_PANEL_2}, stop:1 {GC_PANEL});
		color: {GC_CYAN}; border: 1px solid {GC_BORDER}; border-bottom: 1px solid #141026;
		border-top-left-radius: 12px; border-top-right-radius: 12px; padding: 7px 10px;
		font-weight: bold; font-family: "{gc_font}"; letter-spacing: 0.7px; }}
	QDockWidget::close-button, QDockWidget::float-button {{ background: transparent;
		border: 1px solid transparent; padding: 3px; icon-size: 14px; margin-right: 3px; }}
	QDockWidget::close-button:hover, QDockWidget::float-button:hover {{
		background: {GC_VIOLET}; border: 1px solid {GC_CYAN}; border-radius: 5px; }}
	QDockWidget::close-button:pressed, QDockWidget::float-button:pressed {{ background: #342c78; }}

	QTabWidget::pane {{ border: 1px solid {GC_BORDER}; border-radius: 12px; background: {GC_INPUT}; }}
	QTabBar::tab {{ background: {GC_PANEL}; color: {GC_MUTED}; border: 1px solid {GC_PANEL_2};
		border-bottom: 0; padding: 7px 14px; margin-right: 3px; border-top-left-radius: 9px;
		border-top-right-radius: 9px; font-weight: bold; }}
	QTabBar::tab:selected {{ background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
		stop:0 {GC_PANEL_3}, stop:1 {GC_PURPLE}); color: {GC_TEXT}; border-color: {GC_CYAN}; }}
	QTabBar::tab:hover {{ color: {GC_CYAN}; }}

	QGroupBox {{ background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
		stop:0 {GC_PANEL}, stop:1 #120e22); border: 1px solid {GC_PANEL_3};
		border-radius: 12px; margin-top: 14px; padding: 12px; font-weight: bold; }}
	QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 6px; color: {GC_CYAN};
		font-family: "{gc_font}"; }}

	QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {{ background: {GC_INPUT};
		color: {GC_TEXT}; border: 1px solid {GC_PANEL_3}; border-radius: 9px; padding: 7px;
		selection-background-color: {GC_PURPLE}; }}
	QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus {{
		border: 1px solid {GC_CYAN}; background: #100d20; }}
	QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled, QPlainTextEdit:disabled {{
		background: #131020; color: #746d98; border-color: #282344; }}
	QComboBox::drop-down {{ border: 0; width: 22px; }}
	QComboBox QAbstractItemView {{ background: {GC_PANEL}; color: {GC_TEXT};
		border: 1px solid {GC_BORDER}; selection-background-color: {GC_PURPLE}; }}

	QCheckBox {{ spacing: 8px; color: {GC_TEXT}; }}
	QCheckBox::indicator {{ width: 16px; height: 16px; border-radius: 4px;
		border: 1px solid {GC_BORDER}; background: {GC_INPUT}; }}
	QCheckBox::indicator:hover {{ border-color: {GC_CYAN}; }}
	QCheckBox::indicator:checked {{ background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
		stop:0 {GC_CYAN}, stop:1 {GC_MINT}); border-color: {GC_CYAN}; }}

	QScrollBar:vertical {{ background: {GC_INPUT}; width: 12px; margin: 0; }}
	QScrollBar::handle:vertical {{ background: {GC_PANEL_3}; min-height: 28px; border-radius: 6px; }}
	QScrollBar::handle:vertical:hover {{ background: {GC_CYAN}; }}
	QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}
	QScrollBar:horizontal {{ background: {GC_INPUT}; height: 12px; margin: 0; }}
	QScrollBar::handle:horizontal {{ background: {GC_PANEL_3}; min-width: 28px; border-radius: 6px; }}
	QScrollBar::handle:horizontal:hover {{ background: {GC_CYAN}; }}
	QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {{ width: 0; }}
	QScrollArea {{ background: {GC_PANEL}; border: 0; }}
	QScrollArea > QWidget > QWidget {{ background: {GC_PANEL}; }}

	QPushButton {{ background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
		stop:0 {GC_PANEL_3}, stop:1 {GC_PURPLE}); color: {GC_TEXT};
		border: 1px solid {GC_BORDER}; border-radius: 11px; padding: 8px 13px;
		font-weight: bold; }}
	QPushButton:hover {{ background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
		stop:0 #4f4290, stop:1 #362f80); border-color: {GC_CYAN}; color: {GC_TEXT}; }}
	QPushButton:pressed {{ background: #27215f; border-color: {GC_MINT}; padding-top: 9px; padding-bottom: 7px; }}
	QPushButton:disabled {{ background: #181429; color: #746d98; border-color: #2b2645; }}
	QPushButton#PrimaryButton {{ background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
		stop:0 #3b86ff, stop:0.55 {GC_PURPLE}, stop:1 #2f246d); border-color: {GC_CYAN};
		font-family: "{gc_font}"; letter-spacing: 0.4px; min-height: 28px; }}
	QPushButton#PrimaryButton:hover {{ background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
		stop:0 #55baff, stop:0.55 #6f63dc, stop:1 #45349d); }}
	QPushButton#DangerButton {{ background: #4b1d2c; border-color: {GC_ORANGE}; color: #ffd39c; }}
	QPushButton#DangerButton:hover {{ background: #6d2639; border-color: {GC_RED}; color: {GC_TEXT}; }}
	QPushButton#ToolButton {{ background: {GC_PANEL}; border-color: {GC_PANEL_3}; padding: 7px 11px; }}
	QPushButton#ToolButton:hover {{ background: {GC_PANEL_2}; border-color: {GC_CYAN}; }}

	QProgressBar {{ background: {GC_INPUT}; border: 1px solid {GC_BORDER}; border-radius: 9px;
		text-align: center; color: {GC_TEXT}; min-height: 17px; }}
	QProgressBar::chunk {{ background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
		stop:0 {GC_CYAN}, stop:1 #7d8cff); border-radius: 8px; }}
	QProgressBar#GoalProgress::chunk {{ background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
		stop:0 {GC_MINT}, stop:1 {GC_CYAN}); }}

	QLabel#Title {{ color: {GC_TEXT}; font-size: 24px; font-weight: bold;
		font-family: "{gc_font}"; letter-spacing: 0.8px; }}
	QLabel#Subtitle {{ color: {GC_CYAN}; font-size: 10px; font-weight: bold; letter-spacing: 1.1px; }}
	QLabel#VersionBadge {{ background: {GC_PANEL_2}; color: {GC_SILVER}; border: 1px solid {GC_BORDER};
		border-radius: 10px; padding: 5px 9px; font-size: 10px; font-weight: bold; }}
	QWidget#HeaderBanner {{ background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
		stop:0 #121021, stop:0.36 {GC_PANEL}, stop:0.68 {GC_PANEL_2}, stop:1 #0b0920);
		border: 1px solid {GC_BORDER}; border-radius: 18px; }}
	QFrame#HeaderRule {{ background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
		stop:0 transparent, stop:0.28 {GC_CYAN}, stop:0.72 {GC_BORDER}, stop:1 transparent);
		max-height: 2px; border: 0; }}

	QLabel#Chip {{ background: {GC_PANEL_2}; color: {GC_MINT}; border: 1px solid {GC_BORDER};
		border-radius: 9px; padding: 5px 8px; font-weight: bold; font-family: "{gc_font}";
		font-size: 10px; letter-spacing: 0.3px; }}
	QLabel#ChipOk {{ background: #174638; color: {GC_MINT}; border: 1px solid {GC_MINT};
		border-radius: 9px; padding: 5px 8px; font-weight: bold; font-family: "{gc_font}";
		font-size: 10px; letter-spacing: 0.3px; }}
	QLabel#ChipWarn {{ background: #563621; color: {GC_ORANGE}; border: 1px solid {GC_ORANGE};
		border-radius: 9px; padding: 5px 8px; font-weight: bold; font-family: "{gc_font}";
		font-size: 10px; letter-spacing: 0.3px; }}
	QLabel#ChipBad {{ background: #4a2030; color: {GC_RED}; border: 1px solid {GC_RED};
		border-radius: 9px; padding: 5px 8px; font-weight: bold; font-family: "{gc_font}";
		font-size: 10px; letter-spacing: 0.3px; }}
	QLabel#ChipInfo {{ background: #17405a; color: {GC_CYAN}; border: 1px solid {GC_CYAN};
		border-radius: 9px; padding: 5px 8px; font-weight: bold; font-family: "{gc_font}";
		font-size: 10px; letter-spacing: 0.3px; }}
	QLabel#ChipClickable {{ background: {GC_PANEL_2}; color: {GC_MINT}; border: 1px solid {GC_BORDER};
		border-radius: 9px; padding: 5px 8px; font-weight: bold; font-family: "{gc_font}";
		font-size: 10px; }}
	QLabel#ChipClickable:hover {{ background: {GC_PANEL_3}; border-color: {GC_CYAN}; color: {GC_CYAN}; }}
	QLabel#CenterBay {{ background: #090617; color: {GC_MUTED}; border: 2px dashed {GC_PANEL_2};
		border-radius: 12px; padding: 14px; font-weight: bold; }}

	QLabel#PipelineIdle {{ background: {GC_PANEL}; color: {GC_MUTED}; border: 1px solid {GC_PANEL_3};
		border-radius: 10px; padding: 8px; font-weight: bold; font-family: "{gc_font}"; font-size: 10px; }}
	QLabel#PipelineRunning {{ background: #15384d; color: {GC_CYAN}; border: 1px solid {GC_CYAN};
		border-radius: 10px; padding: 8px; font-weight: bold; font-family: "{gc_font}"; font-size: 10px; }}
	QLabel#PipelineSuccess {{ background: #153c30; color: {GC_MINT}; border: 1px solid {GC_MINT};
		border-radius: 10px; padding: 8px; font-weight: bold; font-family: "{gc_font}"; font-size: 10px; }}
	QLabel#PipelineFailed {{ background: #4b2b1b; color: {GC_ORANGE}; border: 1px solid {GC_ORANGE};
		border-radius: 10px; padding: 8px; font-weight: bold; font-family: "{gc_font}"; font-size: 10px; }}
	QTableWidget {{ background: {GC_INPUT}; alternate-background-color: {GC_PANEL}; color: {GC_TEXT};
		gridline-color: {GC_PANEL_2}; border: 1px solid {GC_PANEL_3}; border-radius: 10px;
		selection-background-color: {GC_PURPLE}; selection-color: {GC_TEXT}; }}
	QTableWidget::item {{ padding: 4px; }}
	QTableWidget::item:selected {{ background: {GC_PURPLE}; color: {GC_TEXT}; }}
	QHeaderView::section {{ background: {GC_PANEL_2}; color: {GC_CYAN}; border: 0;
		border-right: 1px solid {GC_PANEL}; border-bottom: 1px solid {GC_PANEL_3};
		padding: 6px; font-weight: bold; }}
	QTableCornerButton::section {{ background: {GC_PANEL_2}; border: 0; }}
	QLabel#SectionLabel {{ color: {GC_CYAN}; font-weight: bold; letter-spacing: 0.6px; }}
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
	agent_memory: str = ""
	screenshot_path: str = ""
	screenshot_status: str = "No Dolphin screenshot captured yet"
	harness_status: str = "unknown"
	harness_g36: str = "unknown"
	harness_text: str = "HARNESS  —"
	error: str = ""


def git_output_for_repo(repo: Path, *args: str, timeout: float = 4) -> str:
	try:
		result = subprocess.run(["git", *args], cwd=repo, text=True,
			capture_output=True, timeout=timeout, check=False)
	except (OSError, subprocess.SubprocessError):
		return ""
	return result.stdout.strip()


def git_line_for_repo(repo: Path, *args: str, fallback: str = "unavailable") -> str:
	value = git_output_for_repo(repo, *args)
	return value if value else fallback


def is_xash_repo_root(repo: Path) -> bool:
	return (repo / ".git").exists() and (repo / "scripts/ai-aider-pass.sh").is_file()


def repo_validation_detail(repo: Path) -> str:
	if not repo.exists():
		return f"Path does not exist: {repo}"
	if not (repo / ".git").exists():
		return f"No .git directory or gitfile under {repo}"
	if not (repo / "scripts/ai-aider-pass.sh").is_file():
		return f"Missing scripts/ai-aider-pass.sh under {repo}"
	return str(repo)


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


def parse_harness_latest(repo: Path) -> tuple[str, str, str]:
	path = repo / ".ai/state/dolphin-harness-latest.md"
	if not path.is_file():
		return "unknown", "unknown", "HARNESS  NO RUN"
	fields: dict[str, str] = {}
	for line in path.read_text(encoding="utf-8").splitlines():
		if line.startswith("- ") and ": " in line:
			key, value = line[2:].split(": ", 1)
			fields[key.strip()] = value.strip()
	status = fields.get("Status", "unknown")
	g36 = fields.get("Analysis", "unknown")
	goal = fields.get("Goal", "?")
	text = f"HARNESS  {goal} / {status}"
	if g36 and g36 != "unknown":
		text = f"{text} / G36={g36}"
	return status, g36, text[:72]


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


def agent_memory_for_repo(repo: Path) -> str:
	path = repo / ".ai/state/goal-loop-memory.json"
	if not path.is_file():
		return "No agent memory recorded yet."
	try:
		data = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		return "Agent memory unavailable; state file is not readable JSON yet."
	conact = data.get("conact", {}) if isinstance(data, dict) else {}
	if not isinstance(conact, dict):
		return "Agent memory has not been upgraded to ConAct fields yet."

	def items(name: str) -> list[dict[str, object]]:
		value = conact.get(name, [])
		return [item for item in value if isinstance(item, dict)] if isinstance(value, list) else []

	lines = ["CONACT MEMORY"]
	lines.append("Folded action history:")
	history = items("folded_action_history")
	if history:
		for item in history[-4:]:
			lines.append(f"- {item.get('span', 'step')}: {item.get('summary', '')}")
	else:
		lines.append("- none yet")

	lines.append("")
	lines.append("Folded port state:")
	facts = items("folded_port_state")
	if facts:
		for item in facts[-8:]:
			lines.append(f"- {item.get('id', 'fact')}: {item.get('content', '')}")
	else:
		lines.append("- none yet")

	lines.append("")
	lines.append("Recent step record:")
	recent = items("recent_step_record")
	if recent:
		for item in recent[-3:]:
			lines.append(
				f"- {item.get('goal', '?')} {item.get('phase', '?')} "
				f"exit {item.get('exit_code', '?')}: {item.get('result', '')}"
			)
	else:
		lines.append("- none yet")
	return "\n".join(lines)


def count_repo_commits(repo: Path) -> int:
	value = git_output_for_repo(repo, "rev-list", "--count", "HEAD")
	return int(value) if value.isdigit() else 0


def format_duration(seconds: float) -> str:
	seconds = max(0, int(seconds))
	hours, remainder = divmod(seconds, 3600)
	minutes, secs = divmod(remainder, 60)
	if hours:
		return f"{hours}h {minutes}m {secs}s"
	if minutes:
		return f"{minutes}m {secs}s"
	return f"{secs}s"


def write_overnight_session_report(repo: Path, report: Mapping[str, object]) -> Path:
	path = repo / ".ai/state/overnight-session-latest.md"
	lines = [
		"# Overnight Session Report",
		"",
		f"- Started: {report.get('started', '(unknown)')}",
		f"- Ended: {report.get('ended', '(unknown)')}",
		f"- Duration: {report.get('duration', '(unknown)')}",
		f"- Stop reason: {report.get('reason', '(unknown)')}",
		f"- Commits: {report.get('commits_made', 0)} (HEAD count {report.get('commits_start', '?')} → {report.get('commits_end', '?')})",
		f"- Goal passes observed: {report.get('pass_count', 0)}",
		f"- Model restarts: {report.get('model_restarts', 0)}",
		f"- Automation restarts: {report.get('automation_restarts', 0)}",
		f"- Stall recoveries: {report.get('stall_restarts', 0)}",
		f"- Active goal: {report.get('active_goal', '(unknown)')}",
		f"- Harness: {report.get('harness', '(unknown)')}",
		f"- Console log: {report.get('log_path', '(none)')}",
		f"- HEAD: {report.get('head', '(unknown)')}",
	]
	if report.get("notes"):
		lines.extend(("", "## Notes", "", str(report["notes"])))
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text("\n".join(lines) + "\n", encoding="utf-8")
	return path


@dataclass(frozen=True)
class PreflightCheck:
	name: str
	status: str
	detail: str


def supervisor_lock_status(repo: Path) -> tuple[bool, str]:
	lock_path = repo / ".ai/goal-supervisor.lock"
	if not lock_path.is_file():
		return False, "no supervisor lock"
	try:
		pid = int(lock_path.read_text(encoding="utf-8").strip())
	except ValueError:
		return False, "stale supervisor lock (invalid pid)"
	try:
		os.kill(pid, 0)
	except OSError:
		return False, "stale supervisor lock"
	return True, f"supervisor already running (pid {pid})"


def automatic_goals_remaining(repo: Path) -> int:
	remaining = 0
	for _goal_id, state, _title, body in read_goals_for_repo(repo):
		if state == "MANUAL" or state.lower() == "x" or goal_is_blocked(body):
			continue
		remaining += 1
	return remaining


def recommended_model_command_for_preflight() -> str:
	saved = dict(os.environ)
	try:
		apply_model_tuning_to_environ(1, 0.85, 65536, False, 1024, 8192, False)
		return vllm_qwable_command()
	finally:
		os.environ.clear()
		os.environ.update(saved)


def estimate_context_budget(repo: Path, max_context: str) -> tuple[bool, str]:
	script = repo / "scripts/aider-context-estimate.py"
	if not script.is_file():
		return True, "context estimate script unavailable; skipping"
	command = [
		"python3", str(script),
		"--repo", str(repo),
		"--attempt", "1",
		"--output-tokens", "2048",
		"--max-context", max_context,
		"--quiet",
		"read:.ai/goals/GAMECUBE_PORT_GOALS.md",
		"required:engine/platform/gamecube/vid_gamecube.c",
		"read:engine/client/cl_scrn.c",
		"read:.ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		"read:.ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
	]
	try:
		result = subprocess.run(command, cwd=repo, text=True, capture_output=True,
			timeout=20, check=False)
	except (OSError, subprocess.SubprocessError) as exc:
		return True, f"context estimate unavailable: {exc}"
	output = (result.stdout or "") + (result.stderr or "")
	if "OVER_BUDGET" in output:
		return False, output.strip() or "active goal context exceeds model window"
	if result.returncode != 0:
		return True, "context estimate inconclusive; continuing"
	return True, "representative G36 context fits the configured model window"


def collect_overnight_preflight_checks(
	repo: Path,
	*,
	command_text: str,
	api_base: str,
	auto_start_model: bool,
	max_runtime_hours: float,
) -> list[PreflightCheck]:
	checks: list[PreflightCheck] = []
	recommended_command = recommended_model_command_for_preflight()

	def add(name: str, status: str, detail: str) -> None:
		checks.append(PreflightCheck(name=name, status=status, detail=detail))

	if is_xash_repo_root(repo):
		add("Repository", "pass", repo_validation_detail(repo))
	else:
		add("Repository", "fail", repo_validation_detail(repo))

	if os.environ.get("OPENAI_API_KEY"):
		add("OPENAI_API_KEY", "pass", "API key is present in the environment.")
	else:
		add("OPENAI_API_KEY", "fail", "Set OPENAI_API_KEY in the shell or .env before overnight automation.")

	if shutil.which("aider"):
		add("Aider CLI", "pass", shutil.which("aider") or "aider")
	else:
		add("Aider CLI", "fail", "Install aider and ensure it is on PATH.")

	devkit = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
	ppc_gcc = devkit / "devkitPPC/bin/powerpc-eabi-gcc"
	if ppc_gcc.is_file() and (devkit / "libogc").is_dir():
		add("GameCube toolchain", "pass", f"{ppc_gcc}")
	else:
		add("GameCube toolchain", "fail",
			f"devkitPPC/libogc not found under {devkit}; verified commits will fail to build.")

	valve = repo / "Half-Life/valve"
	if valve.is_dir():
		add("Half-Life content", "pass", str(valve.relative_to(repo)))
	else:
		add("Half-Life content", "warn",
			"Half-Life/valve is missing; some probe/build steps may fail until content is staged.")

	load_gamecube_env(repo)
	dolphin = os.environ.get("DOLPHIN_EXECUTABLE", "")
	if dolphin:
		add("Dolphin harness", "pass", dolphin)
	else:
		add("Dolphin harness", "warn",
			"DOLPHIN_EXECUTABLE is unset; runtime probe goals cannot collect fresh evidence.")

	try:
		free_gib = shutil.disk_usage(repo).free / (1024 ** 3)
	except OSError as exc:
		add("Disk space", "warn", f"Could not measure free space: {exc}")
	else:
		if free_gib < 2:
			add("Disk space", "fail", f"{free_gib:.1f} GiB free; need at least 2 GiB for logs and builds.")
		elif free_gib < 10:
			add("Disk space", "warn", f"{free_gib:.1f} GiB free; overnight logs/builds prefer >= 10 GiB.")
		else:
			add("Disk space", "pass", f"{free_gib:.1f} GiB free on the repository filesystem.")

	try:
		command = shlex.split(recommended_command)
	except ValueError as exc:
		add("Recommended vLLM command", "fail", f"Invalid command: {exc}")
		command = []

	if command:
		problem = command_executable_problem(command, repo)
		if problem:
			add("Recommended vLLM command", "fail", problem)
		else:
			add("Recommended vLLM command", "pass", recommended_command[:180])

	if command:
		gpu_problem = gpu_memory_preflight_message(command)
		if gpu_problem:
			host, port = (urlparse(api_base).hostname or "127.0.0.1",
				urlparse(api_base).port or 8072)
			if model_port_open(host, port):
				add("GPU memory", "warn",
					"GPU looks tight for a second vLLM launch, but an existing model API is already reachable.")
			elif auto_start_model:
				add("GPU memory", "fail", gpu_problem)
			else:
				add("GPU memory", "warn", gpu_problem)

	try:
		current = shlex.split(command_text)
	except ValueError:
		current = []
	if command_has_flag(current, "--reasoning-parser"):
		add("Reasoning parser", "warn",
			"Current saved command still uses --reasoning-parser; Overnight Run will apply recommended settings without it.")
	else:
		add("Reasoning parser", "pass", "Recommended command leaves reasoning parser disabled for Aider diff output.")

	host = urlparse(api_base).hostname or "127.0.0.1"
	port = urlparse(api_base).port or 8072
	if model_port_open(host, port):
		add("Model API", "pass", f"{api_base} is reachable.")
	elif auto_start_model:
		add("Model API", "pass", "Model API is offline now; Overnight Run will auto-start vLLM.")
	else:
		add("Model API", "fail",
			"Model API is offline and auto-start vLLM is disabled.")

	lock_active, lock_detail = supervisor_lock_status(repo)
	if lock_active:
		add("Goal supervisor lock", "fail", lock_detail)
	else:
		add("Goal supervisor lock", "pass", lock_detail)

	remaining = automatic_goals_remaining(repo)
	if remaining <= 0:
		add("Automatic goals", "warn", "No automatic goals remain; overnight run may exit immediately.")
	else:
		add("Automatic goals", "pass", f"{remaining} automatic goal(s) still open.")

	ok, detail = estimate_context_budget(repo, os.environ.get("AIDER_MODEL_MAX_CONTEXT", "65536"))
	add("Context budget", "pass" if ok else "warn", detail)

	add("Runtime limit", "pass",
		f"Overnight session will stop after {max_runtime_hours:.1f} hour(s) and write a session report.")
	return checks


class OvernightPreflightDialog(QDialog):
	def __init__(
		self,
		parent: QWidget | None,
		checks: list[PreflightCheck],
		*,
		allow_start: bool = True,
	) -> None:
		super().__init__(parent)
		self.checks = checks
		self.allow_start = allow_start
		self.setWindowTitle("Overnight Preflight Checklist")
		self.setMinimumWidth(760)
		layout = QVBoxLayout(self)

		summary = QLabel(
			"Review the environment before leaving automation unattended. "
			"Failures must be fixed; warnings are shown but may still be acceptable."
		)
		summary.setWordWrap(True)
		layout.addWidget(summary)

		self.table = QTableWidget(len(checks), 3)
		self.table.setHorizontalHeaderLabels(["Status", "Check", "Details"])
		self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
		self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
		self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
		self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
		self.table.setSelectionMode(QTableWidget.SelectionMode.NoSelection)
		self.table.verticalHeader().setVisible(False)
		for row, check in enumerate(checks):
			status_item = QTableWidgetItem(check.status.upper())
			color = {
				"pass": QColor(GC_MINT),
				"warn": QColor(GC_ORANGE),
				"fail": QColor(GC_RED),
			}.get(check.status, QColor(GC_MUTED))
			status_item.setForeground(color)
			self.table.setItem(row, 0, status_item)
			self.table.setItem(row, 1, QTableWidgetItem(check.name))
			detail_item = QTableWidgetItem(check.detail)
			detail_item.setToolTip(check.detail)
			self.table.setItem(row, 2, detail_item)
		layout.addWidget(self.table)

		counts = {
			"pass": sum(item.status == "pass" for item in checks),
			"warn": sum(item.status == "warn" for item in checks),
			"fail": sum(item.status == "fail" for item in checks),
		}
		self.summary_label = QLabel(
			f"{counts['pass']} passed, {counts['warn']} warnings, {counts['fail']} failures"
		)
		self.summary_label.setStyleSheet(
			f"color: {GC_RED if counts['fail'] else GC_MINT if counts['warn'] == 0 else GC_ORANGE}; "
			"font-weight: bold;"
		)
		layout.addWidget(self.summary_label)

		buttons = QDialogButtonBox()
		if allow_start:
			self.start_button = buttons.addButton("Start Overnight Run", QDialogButtonBox.ButtonRole.AcceptRole)
			self.start_button.setEnabled(counts["fail"] == 0)
		buttons.addButton(QDialogButtonBox.StandardButton.Close)
		buttons.rejected.connect(self.reject)
		if allow_start:
			buttons.accepted.connect(self.accept)
		layout.addWidget(buttons)

	@property
	def has_failures(self) -> bool:
		return any(check.status == "fail" for check in self.checks)

	@property
	def has_warnings(self) -> bool:
		return any(check.status == "warn" for check in self.checks)


def write_overnight_preflight_report(repo: Path, checks: list[PreflightCheck]) -> Path:
	path = repo / ".ai/state/overnight-preflight-latest.md"
	lines = [
		"# Overnight Preflight",
		"",
		f"- Checked: {datetime.now().isoformat(sep=' ', timespec='seconds')}",
		"",
	]
	for check in checks:
		lines.append(f"- [{check.status.upper()}] {check.name}: {check.detail}")
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text("\n".join(lines) + "\n", encoding="utf-8")
	return path


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

		snapshot.harness_status, snapshot.harness_g36, snapshot.harness_text = parse_harness_latest(repo)
		snapshot.agent_memory = agent_memory_for_repo(repo)

		if is_xash_repo_root(repo):
			load_dotenv(repo / ".env")
			branch = git_line_for_repo(repo, "branch", "--show-current", fallback="detached")
			porcelain = git_output_for_repo(repo, "status", "--porcelain")
			tracking_lines = git_output_for_repo(repo, "status", "--short", "--branch").splitlines()
			recent = git_line_for_repo(repo, "log", "-1", "--oneline")
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
			harness_status_line = "none recorded"
			if harness_latest.is_file():
				interesting = []
				for line in harness_latest.read_text(encoding="utf-8").splitlines():
					if line.startswith("- Status:") or line.startswith("- Visual:") or \
						line.startswith("- Analysis:") or line.startswith("- Next action:"):
						interesting.append(line.removeprefix("- ").strip())
				if interesting:
					harness_status_line = " / ".join(interesting)[:180]
			toolchain = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro")) / "devkitPPC/bin/powerpc-eabi-gcc"
			lines = [
				f"GIT       {branch}  {'DIRTY' if porcelain else 'CLEAN'}",
				f"TRACKING  {tracking_lines[0][3:] if tracking_lines else 'unknown'}",
				f"HEAD      {recent}",
				f"SUBMODULE {len(submodules)} present / {dirty_submodules} divergent",
				f"TOOLCHAIN {'READY' if toolchain.is_file() else 'MISSING'}  {toolchain}",
				f"CONTENT   {'READY' if valve.is_dir() else 'MISSING'}  Half-Life/valve",
				f"AIDER     {'AUTH INHERITED' if os.environ.get('OPENAI_API_KEY') else 'AUTH NOT IN ENVIRONMENT'}",
				f"BLOCKER   {blocker_tail}",
				f"DOLPHIN   {harness_status_line}",
			]
			snapshot.context = "\n".join(lines)
		else:
			snapshot.context = f"Repository telemetry unavailable: {repo_validation_detail(repo)}"

		snapshot.screenshot_path, snapshot.screenshot_status = latest_dolphin_screenshot_for_repo(repo)
	except (OSError, subprocess.SubprocessError, ValueError) as exc:
		snapshot.error = f"Telemetry unavailable: {exc}"
	return snapshot


class ClickableLabel(QLabel):
	clicked = pyqtSignal()

	def __init__(self, text: str = "", parent: QWidget | None = None) -> None:
		super().__init__(text, parent)
		self.setCursor(Qt.CursorShape.PointingHandCursor)

	def mousePressEvent(self, event) -> None:  # type: ignore[override]
		if event.button() == Qt.MouseButton.LeftButton:
			self.clicked.emit()
		super().mousePressEvent(event)


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
		self.gamecube_font_family = gamecube_font_family
		self.setWindowTitle("Xash3D on GameCube — Porting Console")
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
		self.dashboard_threads: list[QThread] = []
		self.dashboard_refresh_running = False
		self.dashboard_refresh_pending = False
		self.last_context = ""
		self.last_agent_memory = ""
		self.start_head = ""
		self.closing = False
		self.model_api_wait_attempts = 0
		self._layout_initialized = False
		self._layout_busy = False
		self._layout_restored_from_settings = False
		self._last_goals_signature = ""
		self._last_goal_state_signature = ""
		self._pipeline_states: dict[str, str] = {}
		self._last_model_api_summary = ""
		self.overnight_mode = False
		self.pending_overnight_automation = False
		self.overnight_started_at: datetime | None = None
		self.overnight_commits_at_start = 0
		self.overnight_pass_count = 0
		self.overnight_model_restarts = 0
		self.overnight_automation_restarts = 0
		self.overnight_stall_restarts = 0
		self.overnight_log_path: Path | None = None
		self.last_output_at = datetime.now()
		self.last_overnight_log_flush = datetime.now()
		self.overnight_watchdog = QTimer(self)
		self.overnight_watchdog.setInterval(60_000)
		self.overnight_watchdog.timeout.connect(self.check_overnight_watchdog)
		self._viewport_resize_timer = QTimer(self)
		self._viewport_resize_timer.setSingleShot(True)
		self._viewport_resize_timer.setInterval(120)
		self._viewport_resize_timer.timeout.connect(self.update_viewport_pixmap)
		self.setMinimumSize(1024, 680)
		self.setDockOptions(QMainWindow.DockOption.AllowTabbedDocks)
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
		central.setObjectName("AppCentral")
		central.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)
		self.setCentralWidget(central)
		layout = QVBoxLayout(central)
		layout.setSpacing(6)
		layout.setContentsMargins(8, 8, 8, 4)

		header_banner = QWidget()
		header_banner.setObjectName("HeaderBanner")
		header_banner.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)
		header_banner.setMaximumHeight(132)
		header_outer = QVBoxLayout(header_banner)
		header_outer.setContentsMargins(14, 10, 14, 10)
		header_outer.setSpacing(8)
		header = QHBoxLayout()
		mark = QSvgWidget(str(HEADER_MARK))
		mark.setFixedSize(60, 60)
		header.addWidget(mark)
		titles = QVBoxLayout()
		title_row = QHBoxLayout()
		title_row.setSpacing(8)
		title_prefix = QLabel("Xash3D")
		title_prefix.setObjectName("Title")
		title_row.addWidget(title_prefix)
		logo = QSvgWidget(str(HEADER_LOGO))
		logo.setFixedSize(112, 80)
		title_row.addWidget(logo, 0, Qt.AlignmentFlag.AlignVCenter)
		title_row.addStretch()
		version_badge = QLabel(f"v{APP_VERSION}")
		version_badge.setObjectName("VersionBadge")
		title_row.addWidget(version_badge, 0, Qt.AlignmentFlag.AlignTop)
		titles.addLayout(title_row)
		subtitle = QLabel("NINTENDO GAMECUBE PORTING CONSOLE")
		subtitle.setObjectName("Subtitle")
		titles.addWidget(subtitle)
		header.addLayout(titles, 1)
		goal_progress_box = QVBoxLayout()
		goal_progress_label = QLabel("GOAL PROGRESS")
		goal_progress_label.setObjectName("SectionLabel")
		self.goal_progress = QProgressBar()
		self.goal_progress.setObjectName("GoalProgress")
		self.goal_progress.setRange(0, 100)
		self.goal_progress.setValue(0)
		self.goal_progress.setFormat("0 / 0 goals")
		self.goal_progress.setFixedWidth(148)
		goal_progress_box.addWidget(goal_progress_label, 0, Qt.AlignmentFlag.AlignRight)
		goal_progress_box.addWidget(self.goal_progress)
		header.addLayout(goal_progress_box)
		header_outer.addLayout(header)
		rule = QFrame()
		rule.setObjectName("HeaderRule")
		rule.setFrameShape(QFrame.Shape.HLine)
		header_outer.addWidget(rule)
		layout.addWidget(header_banner)

		chip_scroll = QScrollArea()
		chip_scroll.setWidgetResizable(True)
		chip_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
		chip_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
		chip_scroll.setFrameShape(QFrame.Shape.NoFrame)
		chip_scroll.setMaximumHeight(44)
		chip_scroll.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)
		chip_host = QWidget()
		chip_row = QHBoxLayout(chip_host)
		chip_row.setContentsMargins(0, 0, 0, 0)
		chip_row.setSpacing(6)
		self.dol_chip = QLabel("DOL  —")
		self.iso_chip = QLabel("ISO  —")
		self.dolphin_chip = QLabel("DOLPHIN CHECKING")
		self.harness_chip = ClickableLabel("HARNESS  —")
		self.harness_chip.setObjectName("ChipClickable")
		self.harness_chip.setToolTip("Click to open the latest Dolphin harness report")
		self.harness_chip.clicked.connect(self.open_harness_report)
		self.model_chip = QLabel("MODEL CHECKING")
		self.save_chip = QLabel("GIT SAVED")
		for chip in (self.dol_chip, self.iso_chip, self.dolphin_chip, self.model_chip, self.save_chip):
			chip.setObjectName("Chip")
			chip_row.addWidget(chip)
		chip_row.addWidget(self.harness_chip)
		chip_row.addStretch()
		chip_scroll.setWidget(chip_host)
		layout.addWidget(chip_scroll)

		self.configure_menus()

		progress_panel = QWidget()
		progress_layout = QVBoxLayout(progress_panel)
		progress_layout.setContentsMargins(10, 8, 10, 8)
		progress_layout.setSpacing(6)
		self.status_label = QLabel("Idle — goal console ready")
		self.progress = QProgressBar()
		self.progress.setRange(0, 1)
		self.progress.setValue(0)
		self.progress.setFormat("No automation running")
		progress_layout.addWidget(self.status_label)
		progress_layout.addWidget(self.progress)
		self.session_stats_label = QLabel("Overnight session idle")
		self.session_stats_label.setWordWrap(True)
		self.session_stats_label.setStyleSheet(f"color: {GC_MUTED}; font-size: 11px;")
		progress_layout.addWidget(self.session_stats_label)
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
		self.model_max_seqs_spin = QSpinBox()
		self.model_max_seqs_spin.setRange(1, 8)
		self.model_max_seqs_spin.setValue(int(os.environ.get("QWABLE_5_MAX_NUM_SEQS", "1")))
		self.model_gpu_util_spin = QDoubleSpinBox()
		self.model_gpu_util_spin.setRange(0.50, 0.95)
		self.model_gpu_util_spin.setSingleStep(0.05)
		self.model_gpu_util_spin.setDecimals(2)
		self.model_gpu_util_spin.setValue(float(os.environ.get("QWABLE_5_GPU_MEMORY_UTILIZATION", "0.85")))
		self.model_max_len_spin = QSpinBox()
		self.model_max_len_spin.setRange(8192, 131072)
		self.model_max_len_spin.setSingleStep(1024)
		self.model_max_len_spin.setValue(int(os.environ.get("QWABLE_5_MAX_MODEL_LEN", "65536")))
		self.model_tool_choice = QCheckBox("Enable vLLM tool choice (Aider does not need this)")
		self.model_tool_choice.setChecked(
			os.environ.get("QWABLE_5_ENABLE_TOOL_CHOICE", "").strip().lower() in {"1", "true", "yes"})
		self.model_reasoning_parser = QCheckBox(
			"Enable vLLM reasoning parser (off for Aider diff output)")
		self.model_reasoning_parser.setChecked(
			os.environ.get("QWABLE_5_REASONING_PARSER", "").strip().lower() in {"qwen3", "1", "true", "yes"})
		self.aider_history_spin = QSpinBox()
		self.aider_history_spin.setRange(256, 4096)
		self.aider_history_spin.setSingleStep(256)
		self.aider_history_spin.setValue(int(os.environ.get("AIDER_MAX_CHAT_HISTORY_TOKENS", "1024")))
		self.aider_overhead_spin = QSpinBox()
		self.aider_overhead_spin.setRange(4096, 20000)
		self.aider_overhead_spin.setSingleStep(512)
		self.aider_overhead_spin.setValue(int(os.environ.get("AIDER_SYSTEM_OVERHEAD_TOKENS", "8192")))
		recommended_btn = QPushButton("Apply Recommended")
		recommended_btn.setToolTip("Rebuild the launch command for single-client Aider automation")
		recommended_btn.clicked.connect(self.apply_recommended_model_settings)
		test_api_btn = QPushButton("Test API")
		test_api_btn.clicked.connect(self.refresh_model_api_summary)
		preflight_btn = QPushButton("Context Preflight")
		preflight_btn.setToolTip("Estimate whether the active goal context fits the vLLM window")
		preflight_btn.clicked.connect(self.run_context_preflight)
		model_command_label = QLabel("Command:")
		model_api_label = QLabel("API base:")
		model_controls = QHBoxLayout()
		self.start_model_btn = QPushButton("▶  START")
		self.start_model_btn.setObjectName("PrimaryButton")
		self.start_model_btn.clicked.connect(self.start_model)
		self.kill_model_btn = QPushButton("■  KILL")
		self.kill_model_btn.setObjectName("DangerButton")
		self.kill_model_btn.clicked.connect(self.kill_model)
		model_controls.addWidget(self.start_model_btn)
		model_controls.addWidget(self.kill_model_btn)
		model_controls.addWidget(recommended_btn)
		model_controls.addWidget(test_api_btn)
		model_controls.addWidget(preflight_btn)
		self.model_status_label = QLabel("Model API not checked yet")
		self.model_status_label.setWordWrap(True)
		self.model_status_label.setStyleSheet(f"color: {GC_MUTED};")
		model_form.addWidget(model_command_label, 0, 0, 1, 2)
		model_form.addWidget(self.model_command_edit, 1, 0, 1, 2)
		model_form.addWidget(QLabel("Max seqs:"), 2, 0)
		model_form.addWidget(self.model_max_seqs_spin, 2, 1)
		model_form.addWidget(QLabel("GPU util:"), 3, 0)
		model_form.addWidget(self.model_gpu_util_spin, 3, 1)
		model_form.addWidget(QLabel("Max context:"), 4, 0)
		model_form.addWidget(self.model_max_len_spin, 4, 1)
		model_form.addWidget(self.model_tool_choice, 5, 0, 1, 2)
		model_form.addWidget(self.model_reasoning_parser, 6, 0, 1, 2)
		model_form.addWidget(QLabel("Aider history:"), 7, 0)
		model_form.addWidget(self.aider_history_spin, 7, 1)
		model_form.addWidget(QLabel("Prompt overhead:"), 8, 0)
		model_form.addWidget(self.aider_overhead_spin, 8, 1)
		model_form.addWidget(model_api_label, 9, 0)
		model_form.addWidget(self.model_api_edit, 9, 1)
		model_form.addLayout(model_controls, 10, 0, 1, 2)
		model_form.addWidget(self.model_status_label, 11, 0, 1, 2)
		model_form.setColumnStretch(0, 1)
		model_form.setColumnStretch(1, 1)
		for widget in (
			self.model_max_seqs_spin, self.model_gpu_util_spin, self.model_max_len_spin,
			self.aider_history_spin, self.aider_overhead_spin,
		):
			widget.valueChanged.connect(self.sync_model_command_from_tuning)
		self.model_tool_choice.toggled.connect(self.sync_model_command_from_tuning)
		self.model_reasoning_parser.toggled.connect(self.sync_model_command_from_tuning)
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
		self.goal_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
		self.goal_table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
		self.goal_table.cellDoubleClicked.connect(self.show_goal_detail)
		goals_layout.addWidget(self.goal_table)
		harness_label = QLabel("Dolphin Harness")
		harness_label.setObjectName("SectionLabel")
		goals_layout.addWidget(harness_label)
		self.harness_view = QPlainTextEdit()
		self.harness_view.setReadOnly(True)
		self.harness_view.setMaximumBlockCount(40)
		self.harness_view.setMaximumHeight(120)
		self.harness_view.setPlaceholderText("Run Boot Probe or RC Check to populate harness telemetry.")
		goals_layout.addWidget(self.harness_view)
		self.add_panel("Goals", goals_panel, Qt.DockWidgetArea.LeftDockWidgetArea)

		context_panel = QWidget()
		context_layout = QVBoxLayout(context_panel)
		context_layout.setContentsMargins(8, 8, 8, 8)
		context_layout.setSpacing(6)
		telemetry_header = QHBoxLayout()
		telemetry_label = QLabel("Repository Telemetry")
		telemetry_label.setObjectName("SectionLabel")
		telemetry_header.addWidget(telemetry_label)
		telemetry_header.addStretch()
		self.telemetry_refresh_btn = QPushButton("Refresh")
		self.telemetry_refresh_btn.setObjectName("ToolButton")
		self.telemetry_refresh_btn.clicked.connect(self.refresh_context)
		telemetry_header.addWidget(self.telemetry_refresh_btn)
		context_layout.addLayout(telemetry_header)
		self.context_view = QPlainTextEdit()
		self.context_view.setReadOnly(True)
		self.context_view.setPlaceholderText("Loading repository telemetry…")
		self.context_view.setMinimumHeight(160)
		self.context_view.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
		context_layout.addWidget(self.context_view, 3)
		memory_label = QLabel("Agent Memory")
		memory_label.setObjectName("SectionLabel")
		context_layout.addWidget(memory_label)
		self.agent_memory_view = QPlainTextEdit()
		self.agent_memory_view.setReadOnly(True)
		self.agent_memory_view.setPlaceholderText("Loading ConAct memory from .ai/state/goal-loop-memory.json…")
		self.agent_memory_view.setMinimumHeight(140)
		self.agent_memory_view.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
		context_layout.addWidget(self.agent_memory_view, 2)
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
		self._viewport_scaled_path = ""
		self.add_panel("Dolphin Viewport", viewport_panel, Qt.DockWidgetArea.BottomDockWidgetArea)

		automation_panel = QWidget()
		automation_panel.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)
		automation_layout = QVBoxLayout(automation_panel)
		automation_layout.setContentsMargins(6, 4, 6, 4)
		automation_layout.setSpacing(4)
		controls = QHBoxLayout()
		self.passes_spin = QSpinBox()
		self.passes_spin.setRange(0, 100)
		self.passes_spin.setSpecialValueText("Unlimited")
		self.passes_spin.setValue(0)
		self.recovery_spin = QSpinBox()
		self.recovery_spin.setRange(1, 50)
		self.recovery_spin.setValue(8)
		self.cycle_sleep_spin = QSpinBox()
		self.cycle_sleep_spin.setRange(5, 120)
		self.cycle_sleep_spin.setValue(10)
		self.cycle_sleep_spin.setToolTip("Seconds to wait between supervisor retries")
		self.loop_btn = QPushButton("▶  ACCOMPLISH ALL GOALS")
		self.loop_btn.setObjectName("PrimaryButton")
		self.loop_btn.setToolTip(
			"Run the goal supervisor until every automatic GameCube port goal is "
			"complete, blocked, or needs manual review.")
		self.loop_btn.clicked.connect(self.run_goal_loop)
		self.stop_btn = QPushButton("■  STOP AUTOMATION")
		self.stop_btn.setObjectName("DangerButton")
		self.stop_btn.clicked.connect(self.stop_process)
		self.stop_btn.setEnabled(False)
		controls.addWidget(self.loop_btn, 2)
		controls.addWidget(QLabel("Pass limit:"))
		controls.addWidget(self.passes_spin)
		controls.addWidget(QLabel("Recovery retries:"))
		controls.addWidget(self.recovery_spin)
		controls.addWidget(QLabel("Cycle sleep:"))
		controls.addWidget(self.cycle_sleep_spin)
		controls.addWidget(self.stop_btn)
		automation_layout.addLayout(controls)
		overnight_row = QHBoxLayout()
		self.overnight_btn = QPushButton("▶  OVERNIGHT RUN")
		self.overnight_btn.setObjectName("PrimaryButton")
		self.overnight_btn.setToolTip(
			"Apply recommended vLLM settings, auto-start the model if needed, "
			"then run all automatic goals until complete or the runtime limit is reached."
		)
		self.overnight_btn.clicked.connect(self.start_overnight_run)
		self.max_runtime_spin = QDoubleSpinBox()
		self.max_runtime_spin.setRange(0.5, 24.0)
		self.max_runtime_spin.setSingleStep(0.5)
		self.max_runtime_spin.setDecimals(1)
		self.max_runtime_spin.setValue(8.0)
		self.max_runtime_spin.setSuffix(" h")
		self.max_runtime_spin.setToolTip("Stop automation gracefully after this many hours")
		self.auto_start_model = QCheckBox("Auto-start vLLM")
		self.auto_start_model.setChecked(True)
		self.auto_start_model.setToolTip("Launch qwable-5 automatically when starting overnight automation")
		overnight_row.addWidget(self.overnight_btn, 2)
		overnight_row.addWidget(QLabel("Max runtime:"))
		overnight_row.addWidget(self.max_runtime_spin)
		overnight_row.addWidget(self.auto_start_model)
		self.overnight_preflight_btn = QPushButton("Preflight")
		self.overnight_preflight_btn.setToolTip("Run the overnight environment checklist without starting automation")
		self.overnight_preflight_btn.clicked.connect(self.show_overnight_preflight)
		overnight_row.addWidget(self.overnight_preflight_btn)
		automation_layout.addLayout(overnight_row)
		automation_panel.setMaximumHeight(88)
		self.add_panel("Automation", automation_panel, Qt.DockWidgetArea.TopDockWidgetArea)

		pipeline_panel = QWidget()
		pipeline_row = QHBoxLayout(pipeline_panel)
		pipeline_row.setContentsMargins(4, 4, 4, 4)
		pipeline_host = QWidget()
		pipeline_inner = QHBoxLayout(pipeline_host)
		pipeline_inner.setContentsMargins(0, 0, 0, 0)
		for index, name in enumerate(("AIDER", "REVIEW", "VERIFY", "DOL", "ISO", "DOLPHIN", "VISION")):
			if index:
				arrow = QLabel("▶")
				arrow.setStyleSheet(f"color: {GC_CYAN};")
				pipeline_inner.addWidget(arrow)
			node = QLabel(name)
			node.setAlignment(Qt.AlignmentFlag.AlignCenter)
			node.setObjectName("PipelineIdle")
			node.setMinimumWidth(72)
			self.pipeline[name] = node
			pipeline_inner.addWidget(node)
		pipeline_inner.addStretch()
		pipeline_scroll = QScrollArea()
		pipeline_scroll.setWidgetResizable(True)
		pipeline_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
		pipeline_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
		pipeline_scroll.setFrameShape(QFrame.Shape.NoFrame)
		pipeline_scroll.setWidget(pipeline_host)
		pipeline_row.addWidget(pipeline_scroll)
		self.add_panel("Pipeline", pipeline_panel, Qt.DockWidgetArea.TopDockWidgetArea)

		tools_panel = QWidget()
		tools = QHBoxLayout(tools_panel)
		tools.setContentsMargins(4, 4, 4, 4)
		tools_host = QWidget()
		tools_row = QHBoxLayout(tools_host)
		tools_row.setContentsMargins(0, 0, 0, 0)
		tools_row.setSpacing(6)
		for label, command in (
			("Verify", ["scripts/ai-verify.sh"]),
			("Review HEAD", ["scripts/ai-review.sh"]),
			("Build DOL", ["scripts/build-gamecube.sh"]),
			("Build disc ISO", ["scripts/build-gamecube-disc.py", "--output", "OUT/xash3d-gc.iso"]),
			("Boot Probe", ["scripts/dolphin-boot-probe.sh"]),
			("RC Check", ["scripts/gamecube-rc-check.sh"]),
		):
			button = QPushButton(label)
			button.setObjectName("ToolButton")
			button.clicked.connect(lambda _checked=False, c=command, n=label: self.start(c, n))
			tools_row.addWidget(button)
		self.dolphin_btn = QPushButton("Build & Boot in Dolphin")
		self.dolphin_btn.setObjectName("ToolButton")
		self.dolphin_btn.clicked.connect(self.boot_dolphin)
		tools_row.addWidget(self.dolphin_btn)
		self.vision_btn = QPushButton("Dolphin Screenshot Vision Test")
		self.vision_btn.setObjectName("ToolButton")
		self.vision_btn.clicked.connect(self.run_dolphin_vision_test)
		tools_row.addWidget(self.vision_btn)
		tools_row.addStretch()
		tools_scroll = QScrollArea()
		tools_scroll.setWidgetResizable(True)
		tools_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
		tools_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
		tools_scroll.setFrameShape(QFrame.Shape.NoFrame)
		tools_scroll.setWidget(tools_host)
		tools.addWidget(tools_scroll)
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
		self.log_filter = QComboBox()
		self.log_filter.addItems(("All output", "Harness markers", "Errors & warnings"))
		self.log_filter.setToolTip("Filter the console log view")
		self.log_filter.currentIndexChanged.connect(self.apply_log_filter)
		self.follow_log = QCheckBox("Auto-scroll log")
		self.follow_log.setChecked(True)
		for button in (self.copy_log_btn, self.save_log_btn, self.clear_log_btn,
			self.open_logs_btn):
			log_tools.addWidget(button)
		log_tools.addStretch()
		log_tools.addWidget(QLabel("Filter:"))
		log_tools.addWidget(self.log_filter)
		log_tools.addWidget(self.follow_log)
		console_layout.addLayout(log_tools)
		self.log_buffer: list[str] = []
		self.log = QPlainTextEdit()
		self.log.setReadOnly(True)
		self.log.setMaximumBlockCount(10000)
		console_layout.addWidget(self.log)
		self.add_panel("Log", console_panel, Qt.DockWidgetArea.BottomDockWidgetArea)

		self.status_bar = QStatusBar()
		self.setStatusBar(self.status_bar)
		self.status_bar.showMessage("GameCube porting console ready")

		self.install_default_layout()
		self.load_saved_settings()
		load_gamecube_env(self.repo())
		self.sync_model_command_from_tuning()
		self.write_settings_file(include_layout=False)
		self.prime_goal_ledger()
		self.refresh_harness_view()
		self.refresh_context()

		self.timer = QTimer(self)
		self.timer.setInterval(5000)
		self.timer.timeout.connect(self.refresh_dashboard)
		self.timer.start()
		self.goal_refresh_timer = QTimer(self)
		self.goal_refresh_timer.setInterval(2000)
		self.goal_refresh_timer.timeout.connect(self.prime_goal_ledger)
		self.goal_refresh_timer.start()
		self.refresh_dashboard()

	def configure_menus(self) -> None:
		self.file_menu = self.menuBar().addMenu("&File")
		save_settings_action = QAction("Save Settings", self)
		save_settings_action.setShortcut("Ctrl+S")
		save_settings_action.triggered.connect(self.save_settings)
		self.file_menu.addAction(save_settings_action)
		commit_gui_action = QAction("Commit GUI WIP", self)
		commit_gui_action.setToolTip(
			"Commit scripts/xash3d-gc-aider-gui.py and .sh as a standalone chore commit")
		commit_gui_action.triggered.connect(lambda: self.commit_gui_wip())
		self.file_menu.addAction(commit_gui_action)
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
		fit_layout_action = QAction("Fit Panels to Window", self)
		fit_layout_action.triggered.connect(self.apply_default_dock_sizes)
		self.layout_menu.addAction(fit_layout_action)
		self.view_menu.addSeparator()

		self.run_menu = self.menuBar().addMenu("&Run")
		run_goal = QAction("Accomplish All Goals", self)
		run_goal.setToolTip("Run automation through every pending automatic goal in order")
		run_goal.setShortcut("Ctrl+Return")
		run_goal.triggered.connect(self.run_goal_loop)
		self.run_menu.addAction(run_goal)
		overnight_action = QAction("Start Overnight Run", self)
		overnight_action.setShortcut("Ctrl+Shift+Return")
		overnight_action.triggered.connect(self.start_overnight_run)
		self.run_menu.addAction(overnight_action)
		stop_action = QAction("Stop Automation", self)
		stop_action.setShortcut("Escape")
		stop_action.triggered.connect(self.stop_process)
		self.run_menu.addAction(stop_action)
		self.run_menu.addSeparator()
		start_model = QAction("Start Model Server", self)
		start_model.setShortcut("Ctrl+M")
		start_model.triggered.connect(self.start_model)
		self.run_menu.addAction(start_model)
		kill_model = QAction("Kill Model Server", self)
		kill_model.setShortcut("Ctrl+Shift+M")
		kill_model.triggered.connect(self.kill_model)
		self.run_menu.addAction(kill_model)

		self.tools_menu = self.menuBar().addMenu("&Tools")
		for label, shortcut, command in (
			("Verify", "Ctrl+1", ["scripts/ai-verify.sh"]),
			("Boot Probe", "Ctrl+2", ["scripts/dolphin-boot-probe.sh"]),
			("RC Check", "Ctrl+3", ["scripts/gamecube-rc-check.sh"]),
			("Build DOL", "Ctrl+4", ["scripts/build-gamecube.sh"]),
			("Review HEAD", "Ctrl+5", ["scripts/ai-review.sh"]),
		):
			action = QAction(label, self)
			action.setShortcut(shortcut)
			action.triggered.connect(lambda _checked=False, c=command, n=label: self.start(c, n))
			self.tools_menu.addAction(action)
		self.tools_menu.addSeparator()
		context_action = QAction("Context Preflight", self)
		context_action.setShortcut("Ctrl+P")
		context_action.triggered.connect(self.run_context_preflight)
		self.tools_menu.addAction(context_action)
		harness_action = QAction("Open Harness Report", self)
		harness_action.setShortcut("Ctrl+H")
		harness_action.triggered.connect(self.open_harness_report)
		self.tools_menu.addAction(harness_action)
		session_action = QAction("Open Overnight Session Report", self)
		session_action.setShortcut("Ctrl+Shift+H")
		session_action.triggered.connect(self.open_overnight_report)
		self.tools_menu.addAction(session_action)
		overnight_preflight_action = QAction("Overnight Preflight Checklist", self)
		overnight_preflight_action.setShortcut("Ctrl+Shift+P")
		overnight_preflight_action.triggered.connect(self.show_overnight_preflight)
		self.tools_menu.addAction(overnight_preflight_action)
		preflight_report_action = QAction("Open Overnight Preflight Report", self)
		preflight_report_action.triggered.connect(self.open_overnight_preflight_report)
		self.tools_menu.addAction(preflight_report_action)

		self.about_menu = self.menuBar().addMenu("&About")
		about_action = QAction("About Xash3D GameCube Porting", self)
		about_action.triggered.connect(self.show_about_panel)
		self.about_menu.addAction(about_action)

	def read_settings_file(self) -> dict[str, object]:
		candidates = [self.settings_path(), SETTINGS_PATH]
		seen: set[Path] = set()
		for path in candidates:
			if path in seen or not path.is_file():
				continue
			seen.add(path)
			try:
				data = json.loads(path.read_text(encoding="utf-8"))
			except (OSError, json.JSONDecodeError) as exc:
				self.status_label.setText(f"Settings unavailable: {exc}")
				return {}
			return dict(data) if isinstance(data, Mapping) else {}
		return {}

	def current_settings(self, *, include_layout: bool) -> dict[str, object]:
		data: dict[str, object] = {
			"version": APP_VERSION,
			"repo": self.repo_edit.text().strip(),
			"model_command": self.model_command_edit.text().strip(),
			"model_api_base": self.model_api_edit.text().strip(),
			"model_max_num_seqs": self.model_max_seqs_spin.value(),
			"model_gpu_utilization": self.model_gpu_util_spin.value(),
			"model_max_model_len": self.model_max_len_spin.value(),
			"model_tool_choice": self.model_tool_choice.isChecked(),
			"model_reasoning_parser": self.model_reasoning_parser.isChecked(),
			"aider_history_tokens": self.aider_history_spin.value(),
			"aider_system_overhead_tokens": self.aider_overhead_spin.value(),
			"pass_limit": self.passes_spin.value(),
			"recovery_retries": self.recovery_spin.value(),
			"cycle_sleep_sec": self.cycle_sleep_spin.value(),
			"max_runtime_hours": self.max_runtime_spin.value(),
			"auto_start_model": self.auto_start_model.isChecked(),
			"follow_log": self.follow_log.isChecked(),
		}
		if include_layout:
			data["layout_version"] = DEFAULT_LAYOUT_VERSION
			data["geometry"] = bytes(self.saveGeometry().toBase64()).decode("ascii")
			data["dock_layout"] = bytes(self.saveState().toBase64()).decode("ascii")
		return data

	def write_settings_file(self, *, include_layout: bool) -> bool:
		data = self.read_settings_file()
		data.update(self.current_settings(include_layout=include_layout))
		path = self.settings_path()
		try:
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
				encoding="utf-8")
		except OSError as exc:
			QMessageBox.warning(self, "Save settings failed", str(exc))
			return False
		return True

	def save_settings(self) -> None:
		if self.write_settings_file(include_layout=False):
			self.status_label.setText(f"Saved settings: {self.settings_path().relative_to(self.repo())}")
			self.commit_gui_wip(quiet=True)

	def commit_gui_wip(self, *, quiet: bool = False) -> bool:
		script = self.repo() / "scripts/ai-commit-gui-wip.sh"
		if not script.is_file():
			if not quiet:
				QMessageBox.warning(self, "Commit GUI failed",
					f"Missing {script.relative_to(self.repo())}")
			return False
		try:
			result = subprocess.run(
				["bash", str(script)],
				cwd=self.repo(),
				text=True,
				capture_output=True,
				timeout=120,
				check=False,
			)
		except (OSError, subprocess.SubprocessError) as exc:
			if not quiet:
				QMessageBox.warning(self, "Commit GUI failed", str(exc))
			return False
		output = ((result.stdout or "") + (result.stderr or "")).strip()
		if result.returncode != 0:
			if not quiet:
				QMessageBox.warning(self, "Commit GUI failed", output or f"exit {result.returncode}")
			elif output:
				self.append(f"\n[GUI commit failed]\n{output}\n")
			return False
		if "committing GUI changes" in output:
			subject = git_line_for_repo(self.repo(), "log", "-1", "--format=%s",
				fallback="chore: update GameCube porting GUI")
			self.status_label.setText(f"Committed GUI: {subject}")
			if not quiet:
				self.append(f"\n[Committed GUI WIP: {subject}]\n")
		elif not quiet:
			QMessageBox.information(self, "Commit GUI", "No GUI file changes to commit.")
		return True

	def save_layout(self) -> None:
		if self.write_settings_file(include_layout=True):
			self.status_label.setText(f"Saved layout: {self.settings_path().relative_to(self.repo())}")

	def restore_saved_layout(self) -> None:
		data = self.read_settings_file()
		if not self.apply_layout_settings(data):
			QMessageBox.information(self, "No saved layout",
				"Save a layout first with File > Save Layout or View > Layout > Save Layout.")
			return
		self.status_label.setText("Restored saved dock layout")

	def apply_layout_settings(self, data: Mapping[str, object]) -> bool:
		layout_version = data.get("layout_version")
		if layout_version != DEFAULT_LAYOUT_VERSION:
			data.pop("geometry", None)
			data.pop("dock_layout", None)
			data["layout_version"] = DEFAULT_LAYOUT_VERSION
			try:
				SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
				SETTINGS_PATH.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
					encoding="utf-8")
			except OSError:
				pass
			return False
		restored = False
		geometry = data.get("geometry")
		if isinstance(geometry, str) and geometry:
			restored = self.restoreGeometry(QByteArray.fromBase64(geometry.encode("ascii"))) or restored
		layout = data.get("dock_layout")
		if isinstance(layout, str) and layout:
			if not self.restoreState(QByteArray.fromBase64(layout.encode("ascii"))):
				return False
			self._layout_restored_from_settings = True
			restored = True
		self.ensure_default_visible_docks()
		return restored

	def load_saved_settings(self) -> None:
		data = self.read_settings_file()
		if not data:
			return
		migrated_command = False
		repo = data.get("repo")
		if isinstance(repo, str) and repo:
			self.repo_edit.setText(repo)
		model_command = data.get("model_command")
		if isinstance(model_command, str) and model_command:
			migrated = migrate_model_command(model_command)
			self.model_command_edit.setText(migrated)
			if migrated != model_command:
				migrated_command = True
			if "model_max_num_seqs" not in data:
				self.populate_tuning_from_command(migrated)
		model_api_base = data.get("model_api_base")
		if isinstance(model_api_base, str) and model_api_base:
			self.model_api_edit.setText(model_api_base)
		max_num_seqs = data.get("model_max_num_seqs")
		if isinstance(max_num_seqs, int):
			self.model_max_seqs_spin.setValue(max(1, min(8, max_num_seqs)))
		gpu_util = data.get("model_gpu_utilization")
		if isinstance(gpu_util, (int, float)):
			self.model_gpu_util_spin.setValue(float(gpu_util))
		max_model_len = data.get("model_max_model_len")
		if isinstance(max_model_len, int):
			self.model_max_len_spin.setValue(max_model_len)
		tool_choice = data.get("model_tool_choice")
		if isinstance(tool_choice, bool):
			self.model_tool_choice.setChecked(tool_choice)
		reasoning_parser = data.get("model_reasoning_parser")
		if isinstance(reasoning_parser, bool):
			self.model_reasoning_parser.setChecked(reasoning_parser)
		aider_history = data.get("aider_history_tokens")
		if isinstance(aider_history, int):
			self.aider_history_spin.setValue(aider_history)
		aider_overhead = data.get("aider_system_overhead_tokens")
		if isinstance(aider_overhead, int):
			self.aider_overhead_spin.setValue(aider_overhead)
		self.sync_model_command_from_tuning()
		pass_limit = data.get("pass_limit")
		if isinstance(pass_limit, int):
			self.passes_spin.setValue(max(self.passes_spin.minimum(), min(self.passes_spin.maximum(), pass_limit)))
		recovery_retries = data.get("recovery_retries")
		if isinstance(recovery_retries, int):
			self.recovery_spin.setValue(max(self.recovery_spin.minimum(), min(self.recovery_spin.maximum(), recovery_retries)))
		cycle_sleep = data.get("cycle_sleep_sec")
		if isinstance(cycle_sleep, int):
			self.cycle_sleep_spin.setValue(max(self.cycle_sleep_spin.minimum(), min(self.cycle_sleep_spin.maximum(), cycle_sleep)))
		max_runtime = data.get("max_runtime_hours")
		if isinstance(max_runtime, (int, float)):
			self.max_runtime_spin.setValue(float(max_runtime))
		auto_start_model = data.get("auto_start_model")
		if isinstance(auto_start_model, bool):
			self.auto_start_model.setChecked(auto_start_model)
		follow_log = data.get("follow_log")
		if isinstance(follow_log, bool):
			self.follow_log.setChecked(follow_log)
		if migrated_command or "model_max_num_seqs" not in data:
			self.write_settings_file(include_layout=False)
		if not self.apply_layout_settings(data):
			self.install_default_layout()
			QTimer.singleShot(100, self.apply_default_dock_sizes)
		elif not self._layout_restored_from_settings:
			QTimer.singleShot(100, self.apply_default_dock_sizes)
		QTimer.singleShot(150, self.ensure_default_visible_docks)

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
			f"Settings file: {SETTINGS_PATH.relative_to(DEFAULT_REPO)} "
			f"(also saved under the active repo's .ai/state/)"
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
		widget.setProperty("panelSurface", True)
		widget.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
		dock.setWidget(widget)
		allowed = {
			"Workspace": Qt.DockWidgetArea.LeftDockWidgetArea,
			"Goals": Qt.DockWidgetArea.LeftDockWidgetArea,
			"Telemetry": Qt.DockWidgetArea.RightDockWidgetArea,
			"Log": Qt.DockWidgetArea.BottomDockWidgetArea,
			"Dolphin Viewport": Qt.DockWidgetArea.BottomDockWidgetArea,
		}.get(title, Qt.DockWidgetArea.TopDockWidgetArea)
		dock.setAllowedAreas(allowed)
		dock.setFloating(False)
		dock.setFeatures(
			QDockWidget.DockWidgetFeature.DockWidgetClosable |
			QDockWidget.DockWidgetFeature.DockWidgetMovable |
			QDockWidget.DockWidgetFeature.DockWidgetFloatable
		)
		min_sizes = {
			"Workspace": (180, 72),
			"Goals": (220, 140),
			"Telemetry": (220, 140),
			"Log": (240, 120),
			"Dolphin Viewport": (220, 140),
			"Progress": (280, 64),
			"Model Server": (320, 96),
			"Automation": (280, 64),
			"Pipeline": (280, 64),
			"Tools": (280, 64),
		}
		max_heights = {
			"Automation": 132,
		}
		if title in min_sizes:
			width, height = min_sizes[title]
			dock.setMinimumWidth(width)
			dock.setMinimumHeight(height)
		if title in max_heights:
			dock.setMaximumHeight(max_heights[title])
		dock.topLevelChanged.connect(lambda floating, item=dock: self.dock_floating_changed(item, floating))
		self.addDockWidget(area, dock)
		self.docks[title] = dock
		self.view_menu.addAction(dock.toggleViewAction())
		return dock

	def install_default_layout(self) -> None:
		required = {"Workspace", "Model Server", "Goals", "Telemetry",
			"Automation", "Pipeline", "Tools", "Dolphin Viewport", "Log", "Progress"}
		if not required.issubset(self.docks):
			return
		self._layout_busy = True
		try:
			for dock in self.docks.values():
				dock.setFloating(False)
				dock.show()
				self.removeDockWidget(dock)
			for title, area in DEFAULT_DOCK_AREAS.items():
				self.addDockWidget(area, self.docks[title])
			for title in TOP_DOCK_TITLES[1:]:
				self.tabifyDockWidget(self.docks[TOP_DOCK_TITLES[0]], self.docks[title])
			self.docks[TOP_DOCK_TITLES[0]].raise_()
			self.splitDockWidget(self.docks["Workspace"], self.docks["Goals"], Qt.Orientation.Vertical)
			self.splitDockWidget(self.docks["Telemetry"], self.docks["Log"], Qt.Orientation.Vertical)
			self.splitDockWidget(self.docks["Log"], self.docks["Dolphin Viewport"], Qt.Orientation.Horizontal)
			self.ensure_default_visible_docks()
		finally:
			self._layout_busy = False

	def ensure_default_visible_docks(self) -> None:
		for title in ("Log", "Goals", "Automation", "Model Server"):
			dock = self.docks.get(title)
			if dock is not None:
				dock.show()

	def apply_default_dock_sizes(self) -> None:
		if not self.isVisible() or self._layout_busy:
			return
		if not {"Goals", "Telemetry", "Log", "Dolphin Viewport"}.issubset(self.docks):
			return
		height = max(self.height(), self.minimumHeight())
		width = max(self.width(), self.minimumWidth())
		self.resizeDocks(
			[self.docks["Goals"], self.docks["Telemetry"]],
			[max(280, int(width * 0.24)), max(320, int(width * 0.28))],
			Qt.Orientation.Horizontal,
		)
		self.resizeDocks(
			[self.docks["Log"], self.docks["Telemetry"]],
			[max(220, int(height * 0.28)), max(180, int(height * 0.18))],
			Qt.Orientation.Vertical,
		)
		self.resizeDocks(
			[self.docks["Log"], self.docks["Dolphin Viewport"]],
			[max(360, int(width * 0.42)), max(280, int(width * 0.32))],
			Qt.Orientation.Horizontal,
		)
		self.resizeDocks(
			[self.docks["Workspace"], self.docks["Goals"]],
			[max(120, int(height * 0.18)), max(220, int(height * 0.32))],
			Qt.Orientation.Vertical,
		)
		if TOP_DOCK_TITLES[0] in self.docks:
			self.resizeDocks(
				[self.docks[TOP_DOCK_TITLES[0]]],
				[112],
				Qt.Orientation.Vertical,
			)

	def showEvent(self, event) -> None:
		super().showEvent(event)
		if not self._layout_initialized:
			self._layout_initialized = True
			if not self._layout_restored_from_settings:
				QTimer.singleShot(100, self.apply_default_dock_sizes)

	def dock_floating_changed(self, dock: QDockWidget, floating: bool) -> None:
		if floating:
			return

	def reset_dock_layout(self) -> None:
		required = {"Workspace", "Model Server", "Goals", "Telemetry",
			"Automation", "Pipeline", "Tools", "Dolphin Viewport", "Log", "Progress"}
		if not required.issubset(self.docks):
			return
		self.install_default_layout()
		QTimer.singleShot(100, self.apply_default_dock_sizes)
		self.clear_saved_layout(silent=True)
		self.status_label.setText("Dock layout reset")
		self.status_bar.showMessage("Dock layout reset to default", 4000)

	def clear_saved_layout(self, *, silent: bool = False) -> None:
		data = self.read_settings_file()
		if not data:
			return
		data.pop("geometry", None)
		data.pop("dock_layout", None)
		data.pop("layout_version", None)
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
		text = self.repo_edit.text().strip()
		if not text:
			return DEFAULT_REPO.resolve()
		return Path(text).expanduser().resolve()

	def settings_path(self) -> Path:
		return self.repo() / ".ai/state/xash3d-gc-aider-gui-settings.json"

	def valid_repo(self) -> bool:
		root = self.repo()
		ok = is_xash_repo_root(root)
		if not ok:
			QMessageBox.warning(
				self,
				"Invalid repository",
				f"{repo_validation_detail(root)}\n\n"
				"Set the Xash3D repository root in the Workspace panel.",
			)
		return ok

	def set_chip_state(self, chip: QLabel, text: str, state: str = "idle") -> None:
		object_name = {
			"ok": "ChipOk",
			"warn": "ChipWarn",
			"bad": "ChipBad",
			"info": "ChipInfo",
			"idle": "ChipClickable" if isinstance(chip, ClickableLabel) else "Chip",
		}.get(state, "Chip")
		if chip.text() != text:
			chip.setText(text)
		if chip.objectName() != object_name:
			chip.setObjectName(object_name)
			chip.style().unpolish(chip)
			chip.style().polish(chip)

	def refresh_harness_view(self) -> None:
		path = self.repo() / ".ai/state/dolphin-harness-latest.md"
		if not path.is_file():
			self.harness_view.setPlainText("No harness report yet. Run Boot Probe or RC Check.")
			return
		self.harness_view.setPlainText(path.read_text(encoding="utf-8").strip())

	def open_harness_report(self) -> None:
		path = self.repo() / ".ai/state/dolphin-harness-latest.md"
		self.refresh_harness_view()
		if path.is_file():
			QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))
			self.status_bar.showMessage(f"Opened {path.relative_to(self.repo())}", 5000)
		else:
			self.open_logs_folder()

	def show_goal_detail(self, row: int, _column: int) -> None:
		item = self.goal_table.item(row, 0)
		if item is None:
			return
		goal_id = item.text()
		goals = self.read_goals()
		match = next((goal for goal in goals if goal[0] == goal_id), None)
		if match is None:
			return
		_, state, title, body = match
		dialog = QDialog(self)
		dialog.setWindowTitle(f"{goal_id} — {title}")
		dialog.setMinimumWidth(560)
		layout = QVBoxLayout(dialog)
		header = QLabel(f"{goal_id}  [{state}]  {title}")
		header.setObjectName("SectionLabel")
		header.setWordWrap(True)
		layout.addWidget(header)
		body_view = QPlainTextEdit()
		body_view.setReadOnly(True)
		body_view.setPlainText(body or "(no details recorded)")
		layout.addWidget(body_view)
		buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
		buttons.rejected.connect(dialog.reject)
		buttons.accepted.connect(dialog.accept)
		layout.addWidget(buttons)
		dialog.exec()

	def log_line_matches_filter(self, line: str) -> bool:
		mode = self.log_filter.currentText()
		if mode == "All output":
			return True
		if mode == "Harness markers":
			return bool(re.search(
				r"(?i)MAP_READY|G36_|FRAME_BUDGET|VISUAL_STATUS|Dolphin|Boot Probe|RC check|harness",
				line,
			))
		return bool(re.search(r"(?i)error|fail|warning|blocked|timeout|oom|fatal|guest_", line))

	def append_log_line(self, line: str) -> None:
		writer = QTextCursor(self.log.document())
		writer.movePosition(QTextCursor.MoveOperation.End)
		fmt = QTextCharFormat()
		fmt.setForeground(QColor(GC_TEXT))
		for pattern, color in LOG_HIGHLIGHT_RULES:
			if pattern.search(line):
				fmt.setForeground(QColor(color))
				fmt.setFontWeight(QFont.Weight.Bold)
				break
		writer.insertText(line, fmt)

	def apply_log_filter(self) -> None:
		combined = "".join(self.log_buffer)
		self.log.clear()
		for line in combined.splitlines(keepends=True):
			if self.log_line_matches_filter(line.rstrip("\r\n")):
				self.append_log_line(line)
		if self.follow_log.isChecked():
			self.log.moveCursor(QTextCursor.MoveOperation.End)
			self.log.ensureCursorVisible()

	def pick_repo(self) -> None:
		path = QFileDialog.getExistingDirectory(self, "Select repository", str(self.repo()))
		if path:
			self.repo_edit.setText(path)

	def append(self, text: str) -> None:
		if self.closing or not hasattr(self, "log"):
			return
		if text.strip():
			self.last_output_at = datetime.now()
		self.log_buffer.append(text)
		visible_cursor = self.log.textCursor()
		had_selection = visible_cursor.hasSelection()
		position = visible_cursor.position()
		anchor = visible_cursor.anchor()
		scrollbar = self.log.verticalScrollBar()
		scroll_position = scrollbar.value()

		if self.log_filter.currentIndex() == 0:
			for line in text.splitlines(keepends=True):
				self.append_log_line(line)
		else:
			for line in text.splitlines(keepends=True):
				if self.log_line_matches_filter(line.rstrip("\r\n")):
					self.append_log_line(line)

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
		self.log_buffer.clear()
		self.log.clear()
		self.status_label.setText("Console log cleared")
		self.status_bar.showMessage("Console log cleared", 3000)

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

	def _chip_state_for_color(self, color: str) -> str:
		return {
			GC_MINT: "ok",
			GC_CYAN: "info",
			GC_ORANGE: "warn",
			GC_RED: "bad",
			GC_MUTED: "idle",
		}.get(color, "info")

	def set_model_state(self, text: str, color: str) -> None:
		if self.closing:
			return
		self.set_chip_state(self.model_chip, text, self._chip_state_for_color(color))

	def set_harness_state(self, text: str, color: str) -> None:
		state = {
			GC_MINT: "ok",
			GC_ORANGE: "warn",
			GC_CYAN: "info",
		}.get(color, "info")
		if "guest_failure" in text or "boot_failure" in text:
			state = "bad"
		self.set_chip_state(self.harness_chip, text, state)

	def set_save_state(self, text: str, color: str) -> None:
		self.set_chip_state(self.save_chip, text, self._chip_state_for_color(color))

	def process_error(self, error: QProcess.ProcessError) -> None:
		if not self.closing:
			self.append(f"\nProcess error: {error.name}\n")

	def model_process_error(self, error: QProcess.ProcessError, program: str) -> None:
		if not self.closing:
			self.append(f"\nModel process error: {error.name} while starting {program}\n")

	def apply_automation_env_from_ui(self) -> None:
		apply_model_tuning_to_environ(
			self.model_max_seqs_spin.value(),
			self.model_gpu_util_spin.value(),
			self.model_max_len_spin.value(),
			self.model_tool_choice.isChecked(),
			self.aider_history_spin.value(),
			self.aider_overhead_spin.value(),
			self.model_reasoning_parser.isChecked(),
		)
		os.environ["OPENAI_API_BASE"] = self.model_api_edit.text().strip()

	def populate_tuning_from_command(self, command_text: str) -> None:
		try:
			command = shlex.split(command_text)
		except ValueError:
			return
		seqs = command_flag_value(command, "--max-num-seqs")
		if seqs and seqs.isdigit():
			self.model_max_seqs_spin.setValue(max(1, min(8, int(seqs))))
		gpu_util = command_flag_value(command, "--gpu-memory-utilization")
		if gpu_util:
			try:
				self.model_gpu_util_spin.setValue(float(gpu_util))
			except ValueError:
				pass
		max_len = command_flag_value(command, "--max-model-len")
		if max_len and max_len.isdigit():
			self.model_max_len_spin.setValue(int(max_len))
		self.model_tool_choice.setChecked(command_has_flag(command, "--enable-auto-tool-choice"))
		reasoning = command_flag_value(command, "--reasoning-parser")
		self.model_reasoning_parser.setChecked(bool(reasoning))

	def sync_model_command_from_tuning(self) -> None:
		self.apply_automation_env_from_ui()
		self.model_command_edit.setText(vllm_qwable_command())

	def apply_recommended_model_settings(self) -> None:
		self.model_max_seqs_spin.setValue(1)
		self.model_gpu_util_spin.setValue(0.85)
		self.model_max_len_spin.setValue(65536)
		self.model_tool_choice.setChecked(False)
		self.model_reasoning_parser.setChecked(False)
		self.aider_history_spin.setValue(1024)
		self.aider_overhead_spin.setValue(8192)
		self.sync_model_command_from_tuning()
		self.status_label.setText("Applied recommended model tuning for single-client Aider automation")

	def refresh_model_api_summary(self) -> bool:
		summary = fetch_model_api_summary(self.model_api_edit.text().strip())
		if summary != self._last_model_api_summary:
			self._last_model_api_summary = summary
			self.model_status_label.setText(summary)
			color = GC_MINT if summary.startswith("Model API:") else GC_ORANGE
			self.model_status_label.setStyleSheet(f"color: {color};")
		return summary.startswith("Model API:")

	def poll_model_api_ready(self) -> None:
		if self.closing or self.model_process is None:
			self.model_api_wait_attempts = 0
			return
		if self.refresh_model_api_summary():
			self.set_model_state("MODEL  READY", GC_MINT)
			self.status_label.setText("qwable-5 API is ready")
			self.model_api_wait_attempts = 0
			if self.pending_overnight_automation:
				self.pending_overnight_automation = False
				QTimer.singleShot(1000, self.begin_overnight_automation)
			return
		self.model_api_wait_attempts += 1
		if self.model_api_wait_attempts >= 90:
			if self.overnight_mode:
				self.append("\n[Overnight: model API warmup timed out; retrying launch]\n")
				self.overnight_model_restarts += 1
				self.model_api_wait_attempts = 0
				QTimer.singleShot(15000, self.start_model_if_needed_for_overnight)
			else:
				self.status_label.setText("qwable-5 started but API is not responding yet")
				self.model_api_wait_attempts = 0
			return
		self.set_model_state("MODEL  WARMING", GC_ORANGE)
		QTimer.singleShot(2000, self.poll_model_api_ready)

	def run_context_preflight(self) -> None:
		if not self.valid_repo():
			return
		self.apply_automation_env_from_ui()
		command = [
			"python3", "scripts/aider-context-estimate.py",
			"--repo", str(self.repo()),
			"--attempt", "1",
			"--output-tokens", "2048",
			"--max-context", os.environ.get("AIDER_MODEL_MAX_CONTEXT", "65536"),
			"read:.ai/goals/GAMECUBE_PORT_GOALS.md",
			"read:engine/platform/gamecube/vid_gamecube.c",
			"read:engine/client/cl_scrn.c",
		]
		try:
			result = subprocess.run(command, cwd=self.repo(), text=True,
				capture_output=True, timeout=20, check=False)
		except (OSError, subprocess.SubprocessError) as exc:
			self.model_status_label.setText(f"Context preflight failed: {exc}")
			self.model_status_label.setStyleSheet(f"color: {GC_ORANGE};")
			return
		message = (result.stdout or result.stderr).strip().splitlines()[-1] if \
			(result.stdout or result.stderr).strip() else f"exit {result.returncode}"
		ok = result.returncode == 0 and message.startswith("OK:")
		color = GC_MINT if ok else GC_ORANGE
		self.model_status_label.setText(message)
		self.model_status_label.setStyleSheet(f"color: {color};")
		self.status_label.setText("Context preflight passed" if ok else "Context preflight over budget")
		self.status_bar.showMessage(self.status_label.text(), 5000)

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
		seqs = command_flag_value(command, "--max-num-seqs")
		if seqs and seqs.isdigit() and int(seqs) > 2:
			self.append(
				f"\nWarning: --max-num-seqs {seqs} is high for single-client Aider. "
				"Use Apply Recommended before starting vLLM.\n"
			)
		if command_has_flag(command, "--reasoning-parser"):
			self.append(
				"\nWarning: --reasoning-parser is enabled. Aider automation expects plain "
				"diff output; leave this off unless you are not using Aider.\n"
			)

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
		load_gamecube_env(self.repo())
		self.apply_automation_env_from_ui()
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
		self.model_api_wait_attempts = 0
		proc.start(command[0], command[1:])
		QTimer.singleShot(3000, self.poll_model_api_ready)

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
		if self.overnight_mode and exit_code != 0 and \
			(self.process is not None or self.pending_overnight_automation):
			self.overnight_model_restarts += 1
			self.append("\n[Overnight: vLLM exited unexpectedly; restarting in 15s]\n")
			self.set_model_state("MODEL  RESTART", GC_ORANGE)
			QTimer.singleShot(15000, self.start_model_if_needed_for_overnight)

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

	def read_goals(self) -> list[tuple[str, str, str, str]]:
		return read_goals_for_repo(self.repo())

	def active_goal_from_runner_state(
		self,
		goals: list[tuple[str, str, str, str]],
	) -> tuple[str, str, str, str] | None:
		state_path = self.repo() / ".ai/logs/goal-loop-state.json"
		if not state_path.is_file():
			return None
		try:
			data = json.loads(state_path.read_text(encoding="utf-8"))
		except (OSError, json.JSONDecodeError):
			return None
		if not isinstance(data, Mapping):
			return None
		if data.get("state") not in {
			"running", "recovering", "resuming-after-commit",
			"recovering-after-unrelated-commit", "review-required",
		}:
			return None
		goal_data = data.get("goal")
		goal_id = goal_data.get("goal_id") if isinstance(goal_data, Mapping) else None
		if not isinstance(goal_id, str):
			return None
		return next((goal for goal in goals if goal[0] == goal_id), None)

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
		snapshot.active_goal = self.active_goal_from_runner_state(goals)
		if snapshot.active_goal is None:
			snapshot.active_goal = next((goal for goal in goals
			if goal[1] in {" ", "~"} and not goal_is_blocked(goal[3])), None)
		self.apply_goals_snapshot(snapshot)

	def scroll_goals_to_active(self, active: tuple[str, str, str, str] | None) -> None:
		if not active:
			return
		for row in range(self.goal_table.rowCount()):
			item = self.goal_table.item(row, 0)
			if item and item.text() == active[0]:
				self.goal_table.selectRow(row)
				self.goal_table.scrollToItem(item, QTableWidget.ScrollHint.PositionAtCenter)
				return

	def apply_goals_snapshot(self, snapshot: DashboardSnapshot) -> None:
		goals = snapshot.goals or []
		active = snapshot.active_goal
		signature_parts = [
			f"{goal_id}:{state}:{goal_is_blocked(body)}"
			for goal_id, state, _title, body in goals
		]
		signature_parts.append(
			f"counts:{snapshot.complete_goals}/{snapshot.automatic_goals}/{snapshot.blocked_goals}"
		)
		if active:
			signature_parts.append(f"active:{active[0]}")
		signature = "|".join(signature_parts)
		if signature == self._last_goals_signature:
			self.scroll_goals_to_active(active)
			return
		self._last_goals_signature = signature
		self.goal_table.setRowCount(len(goals))
		if not goals:
			ledger = self.repo() / ".ai/goals/GAMECUBE_PORT_GOALS.md"
			self.goal_summary.setText(f"No goal ledger loaded from {ledger}")
			self.goal_progress.setValue(0)
			self.goal_progress.setFormat("0 / 0 goals")
			return
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
		if snapshot.automatic_goals:
			percent = int((snapshot.complete_goals / snapshot.automatic_goals) * 100)
			self.goal_progress.setValue(percent)
			self.goal_progress.setFormat(f"{snapshot.complete_goals} / {snapshot.automatic_goals} goals")
		else:
			self.goal_progress.setValue(0)
			self.goal_progress.setFormat("0 / 0 goals")
		self.scroll_goals_to_active(active)

	def refresh_context(self) -> None:
		snapshot = build_dashboard_snapshot(self.repo(), *self.model_host_port())
		self.apply_context_snapshot(snapshot)

	def apply_context_snapshot(self, snapshot: DashboardSnapshot) -> None:
		if snapshot.error:
			self.context_view.setPlainText(snapshot.error)
		elif snapshot.context:
			self.context_view.setPlainText(snapshot.context)
			self.last_context = snapshot.context
		elif not self.context_view.toPlainText():
			self.context_view.setPlainText("Telemetry pending…")

		memory = snapshot.agent_memory or "No agent memory recorded yet."
		if memory != self.last_agent_memory or not self.agent_memory_view.toPlainText().strip():
			self.agent_memory_view.setPlainText(memory)
			self.last_agent_memory = memory

	def latest_dolphin_screenshot(self) -> tuple[Path | None, str]:
		path, status = latest_dolphin_screenshot_for_repo(self.repo())
		return (Path(path), status) if path else (None, status)

	def refresh_dolphin_viewport(self) -> None:
		path, status = self.latest_dolphin_screenshot()
		self.apply_dolphin_viewport_snapshot(path, status)

	def apply_dolphin_viewport_snapshot(self, path: Path | None, status: str) -> None:
		if path is None:
			self.viewport_path = ""
			self._viewport_scaled_path = ""
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
		status_text = f"{status}  -  {path_text}"
		if self.viewport_status.text() != status_text:
			self.viewport_status.setText(status_text)
		if path_text != getattr(self, "_viewport_scaled_path", ""):
			self._viewport_scaled_path = path_text
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
		except (OSError, ValueError) as exc:
			self.apply_context_snapshot(DashboardSnapshot(error=f"Telemetry unavailable: {exc}"))
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
		self.set_chip_state(self.dol_chip, snapshot.dol_text, "ok" if snapshot.dol_exists else "warn")
		self.set_chip_state(self.iso_chip, snapshot.iso_text, "ok" if snapshot.iso_exists else "warn")
		active_node = self.pipeline_node(self.operation) if self.process else ""
		if active_node != "DOL":
			self.set_pipeline_state("DOL", "Success" if snapshot.dol_exists else "Idle")
		if active_node != "ISO":
			self.set_pipeline_state("ISO", "Success" if snapshot.iso_exists else "Idle")
		self.set_chip_state(
			self.dolphin_chip,
			"DOLPHIN  READY" if snapshot.dolphin_ready else "DOLPHIN  MISSING",
			"ok" if snapshot.dolphin_ready else "warn",
		)
		harness_color = {
			"map_ready": GC_MINT,
			"guest_failure": GC_ORANGE,
			"boot_failure": GC_ORANGE,
		}.get(snapshot.harness_status, GC_CYAN)
		self.set_harness_state(snapshot.harness_text or "HARNESS  —", harness_color)
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
		self.refresh_model_api_summary()
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
		self.overnight_btn.setEnabled(False)
		self.overnight_preflight_btn.setEnabled(False)
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
		load_gamecube_env(self.repo())
		self.apply_automation_env_from_ui()
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
			"--recoverable-retries", str(recoveries),
			"--sleep", str(self.cycle_sleep_spin.value())],
			"Goal automation", passes)

	def model_api_ready(self) -> bool:
		return self.refresh_model_api_summary()

	def model_host_port(self) -> tuple[str, int]:
		api_base = self.model_api_edit.text().strip() or "http://127.0.0.1:8072/v1"
		parsed = urlparse(api_base)
		host = parsed.hostname or "127.0.0.1"
		port = parsed.port or (443 if parsed.scheme == "https" else 8072)
		return host, port

	def start_model_if_needed_for_overnight(self) -> None:
		if self.closing or not self.overnight_mode:
			return
		host, port = self.model_host_port()
		if self.model_process is not None or model_port_open(host, port):
			if self.process is None and not self.pending_overnight_automation:
				self.begin_overnight_automation()
			return
		if self.auto_start_model.isChecked():
			self.pending_overnight_automation = self.process is None
			self.start_model()
		elif self.process is None:
			self.append("\n[Overnight: model API offline and auto-start disabled; waiting…]\n")
			QTimer.singleShot(30000, self.start_model_if_needed_for_overnight)

	def begin_overnight_automation(self) -> None:
		if self.closing or not self.overnight_mode or self.process is not None:
			return
		if not self.model_api_ready():
			self.pending_overnight_automation = True
			self.start_model_if_needed_for_overnight()
			return
		self.pending_overnight_automation = False
		self.append("\n[Overnight: starting goal automation supervisor]\n")
		self.run_goal_loop()

	def update_session_stats_label(self) -> None:
		if not self.overnight_mode or self.overnight_started_at is None:
			self.session_stats_label.setText("Overnight session idle")
			return
		elapsed = (datetime.now() - self.overnight_started_at).total_seconds()
		remaining = max(0.0, self.max_runtime_spin.value() * 3600 - elapsed)
		commits = max(0, count_repo_commits(self.repo()) - self.overnight_commits_at_start)
		self.session_stats_label.setText(
			f"Overnight: {format_duration(elapsed)} elapsed, "
			f"{format_duration(remaining)} remaining | "
			f"commits {commits} | passes {self.overnight_pass_count} | "
			f"model restarts {self.overnight_model_restarts} | "
			f"automation restarts {self.overnight_automation_restarts}"
		)

	def flush_overnight_log(self) -> None:
		if self.overnight_log_path is None:
			return
		try:
			self.overnight_log_path.parent.mkdir(parents=True, exist_ok=True)
			self.overnight_log_path.write_text(self.log.toPlainText(), encoding="utf-8")
			self.last_overnight_log_flush = datetime.now()
		except OSError as exc:
			self.append(f"\n[Overnight log flush failed: {exc}]\n")

	def build_overnight_report(self, reason: str) -> dict[str, object]:
		repo = self.repo()
		elapsed = 0.0
		if self.overnight_started_at is not None:
			elapsed = (datetime.now() - self.overnight_started_at).total_seconds()
		harness_status, harness_g36, harness_text = parse_harness_latest(repo)
		active_goal = "(unknown)"
		goals = self.read_goals()
		for goal_id, state, title, _body in goals:
			if state in {" ", "~"} and not goal_is_blocked(_body):
				active_goal = f"{goal_id} {title}"
				break
		return {
			"started": self.overnight_started_at.isoformat(sep=" ", timespec="seconds")
				if self.overnight_started_at else "(unknown)",
			"ended": datetime.now().isoformat(sep=" ", timespec="seconds"),
			"duration": format_duration(elapsed),
			"reason": reason,
			"commits_start": self.overnight_commits_at_start,
			"commits_end": count_repo_commits(repo),
			"commits_made": max(0, count_repo_commits(repo) - self.overnight_commits_at_start),
			"pass_count": self.overnight_pass_count,
			"model_restarts": self.overnight_model_restarts,
			"automation_restarts": self.overnight_automation_restarts,
			"stall_restarts": self.overnight_stall_restarts,
			"active_goal": active_goal,
			"harness": harness_text or f"{harness_status}/{harness_g36}",
			"log_path": str(self.overnight_log_path.relative_to(repo))
				if self.overnight_log_path and self.overnight_log_path.is_file() else "(none)",
			"head": git_line_for_repo(repo, "log", "-1", "--oneline"),
		}

	def finish_overnight_session(self, reason: str) -> None:
		if not self.overnight_mode:
			return
		self.overnight_watchdog.stop()
		self.flush_overnight_log()
		report = self.build_overnight_report(reason)
		report_path = write_overnight_session_report(self.repo(), report)
		self.overnight_mode = False
		self.pending_overnight_automation = False
		self.overnight_btn.setEnabled(True)
		self.overnight_preflight_btn.setEnabled(True)
		self.loop_btn.setEnabled(self.process is None)
		self.update_session_stats_label()
		self.append(
			f"\n[Overnight session ended: {reason}]\n"
			f"[Session report: {report_path.relative_to(self.repo())}]\n"
		)
		self.status_label.setText(f"Overnight session ended: {reason}")
		self.status_bar.showMessage(self.status_label.text(), 10000)

	def check_overnight_watchdog(self) -> None:
		if not self.overnight_mode or self.overnight_started_at is None:
			return
		self.update_session_stats_label()
		elapsed = (datetime.now() - self.overnight_started_at).total_seconds()
		if elapsed >= self.max_runtime_spin.value() * 3600:
			self.append("\n[Overnight: max runtime reached; stopping automation]\n")
			if self.process is not None:
				self.user_stopping = True
				self.process.terminate()
			self.finish_overnight_session("max runtime reached")
			return
		if (datetime.now() - self.last_overnight_log_flush).total_seconds() >= 300:
			self.flush_overnight_log()
		if self.process is None:
			return
		stall_seconds = (datetime.now() - self.last_output_at).total_seconds()
		if stall_seconds >= 45 * 60:
			self.overnight_stall_restarts += 1
			self.append(
				f"\n[Overnight: no output for {int(stall_seconds // 60)} minutes; "
				"restarting automation supervisor]\n"
			)
			self.user_stopping = True
			self.process.terminate()
			QTimer.singleShot(10000, self.restart_goal_automation)

	def collect_overnight_preflight_checks(self) -> list[PreflightCheck]:
		load_dotenv(self.repo() / ".env")
		load_gamecube_env(self.repo())
		self.apply_automation_env_from_ui()
		return collect_overnight_preflight_checks(
			self.repo(),
			command_text=self.model_command_edit.text().strip(),
			api_base=self.model_api_edit.text().strip(),
			auto_start_model=self.auto_start_model.isChecked(),
			max_runtime_hours=self.max_runtime_spin.value(),
		)

	def show_overnight_preflight(self) -> bool:
		if not self.valid_repo():
			return False
		checks = self.collect_overnight_preflight_checks()
		report_path = write_overnight_preflight_report(self.repo(), checks)
		dialog = OvernightPreflightDialog(self, checks, allow_start=False)
		dialog.exec()
		self.status_label.setText(
			"Overnight preflight failed" if dialog.has_failures else
			"Overnight preflight passed with warnings" if dialog.has_warnings else
			"Overnight preflight passed"
		)
		self.status_bar.showMessage(f"Saved {report_path.relative_to(self.repo())}", 5000)
		return not dialog.has_failures

	def open_overnight_preflight_report(self) -> None:
		path = self.repo() / ".ai/state/overnight-preflight-latest.md"
		if not path.is_file():
			QMessageBox.information(self, "No preflight report",
				"Run the overnight preflight checklist first.")
			return
		QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))

	def confirm_overnight_preflight(self) -> bool:
		checks = self.collect_overnight_preflight_checks()
		report_path = write_overnight_preflight_report(self.repo(), checks)
		dialog = OvernightPreflightDialog(self, checks, allow_start=True)
		if dialog.exec() != QDialog.DialogCode.Accepted:
			return False
		self.append(f"\n[Overnight preflight saved: {report_path.relative_to(self.repo())}]\n")
		if dialog.has_warnings:
			warning_names = ", ".join(check.name for check in checks if check.status == "warn")
			answer = QMessageBox.question(
				self,
				"Continue with warnings?",
				f"The preflight checklist passed with warnings:\n{warning_names}\n\n"
				"Start the overnight run anyway?",
				QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
				QMessageBox.StandardButton.No,
			)
			if answer != QMessageBox.StandardButton.Yes:
				return False
		return True

	def begin_overnight_session(self) -> None:
		self.apply_recommended_model_settings()
		self.save_settings()
		if not self.commit_gui_wip(quiet=True):
			self.overnight_mode = False
			QMessageBox.warning(self, "Overnight run blocked",
				"Could not commit pending GUI changes. Fix syntax errors and retry.")
			return
		self.overnight_mode = True
		self.pending_overnight_automation = False
		self.overnight_started_at = datetime.now()
		self.overnight_commits_at_start = count_repo_commits(self.repo())
		self.overnight_pass_count = 0
		self.overnight_model_restarts = 0
		self.overnight_automation_restarts = 0
		self.overnight_stall_restarts = 0
		self.last_output_at = datetime.now()
		self.last_overnight_log_flush = datetime.now()
		logs = self.repo() / ".ai/logs"
		logs.mkdir(parents=True, exist_ok=True)
		self.overnight_log_path = logs / f"overnight-{self.overnight_started_at.strftime('%Y%m%d-%H%M%S')}.log"
		self.overnight_btn.setEnabled(False)
		self.overnight_preflight_btn.setEnabled(False)
		self.overnight_watchdog.start()
		self.update_session_stats_label()
		self.append(
			f"\n[Overnight session started at {self.overnight_started_at:%Y-%m-%d %H:%M:%S}]\n"
			f"[Console log: {self.overnight_log_path.relative_to(self.repo())}]\n"
		)
		host, port = self.model_host_port()
		if self.auto_start_model.isChecked() and self.model_process is None and not model_port_open(host, port):
			self.pending_overnight_automation = True
			self.start_model()
		else:
			QTimer.singleShot(1000, self.begin_overnight_automation)

	def start_overnight_run(self) -> None:
		if not self.valid_repo():
			return
		if self.process is not None:
			QMessageBox.information(self, "Automation already running",
				"Goal automation is already active.")
			return
		if not self.confirm_overnight_preflight():
			return
		self.begin_overnight_session()

	def open_overnight_report(self) -> None:
		path = self.repo() / ".ai/state/overnight-session-latest.md"
		if not path.is_file():
			QMessageBox.information(self, "No overnight report",
				"No overnight session report has been written yet.")
			return
		QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))

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
		if "MAP_READY:" in text:
			self.set_pipeline_state("DOLPHIN", "Success")
		if "G36_STATUS:" in text or "FRAME_BUDGET_STATS:" in text:
			self.set_pipeline_state("VERIFY", "Running")
		if "G36_STATUS: PASS" in text or "G36_STATUS: WEAK" in text:
			self.set_pipeline_state("VERIFY", "Success")
		if "RC summary:" in text or "RC check passed" in text:
			self.set_pipeline_state("VERIFY", "Success")
		if "RC check failed" in text:
			self.set_pipeline_state("VERIFY", "Failed")
		if "RC_CHECK:" in text or "frame budget" in text.lower():
			self.set_pipeline_state("VERIFY", "Running")
		if self.operation == "Goal automation":
			for line in text.splitlines():
				if line.startswith("GOAL PASS "):
					try:
						pass_value = int(line.split()[2].split("/")[0])
						if self.overnight_mode:
							self.overnight_pass_count = max(self.overnight_pass_count, pass_value)
						if self.expected_passes == 0:
							self.progress.setFormat(f"Goal automation: pass {pass_value}")
						else:
							self.progress.setValue(max(0, pass_value - 1))
					except (IndexError, ValueError):
						pass
				if self.overnight_mode and line.startswith("== supervisor cycle "):
					self.update_session_stats_label()

	def pipeline_node(self, operation: str) -> str:
		return {
			"Goal automation": "AIDER", "Review HEAD": "REVIEW", "Verify": "VERIFY",
			"Build DOL": "DOL", "Build disc ISO": "ISO",
			"Dolphin vision test": "VISION", "Boot Probe": "DOLPHIN",
			"RC Check": "VERIFY",
		}.get(operation, "AIDER")

	def set_pipeline_state(self, name: str, state: str) -> None:
		if self._pipeline_states.get(name) == state:
			return
		self._pipeline_states[name] = state
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
		self.loop_btn.setEnabled(not self.overnight_mode)
		if not self.overnight_mode:
			self.overnight_btn.setEnabled(True)
			self.overnight_preflight_btn.setEnabled(True)
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
		if operation == "Goal automation":
			if succeeded and self.overnight_mode:
				self.finish_overnight_session("all automatic goals complete or supervisor finished")
			elif not succeeded and not self.user_stopping:
				if self.overnight_mode:
					self.overnight_automation_restarts += 1
				self.append("\n[Goal automation restarting in 10s]\n")
				self.status_label.setText("Restarting goal automation after worker exit")
				QTimer.singleShot(10000, self.restart_goal_automation)
			elif not succeeded and self.overnight_mode and self.user_stopping:
				self.finish_overnight_session("stopped by user")

	def restart_goal_automation(self) -> None:
		if self.closing or self.process is not None or not self.last_command:
			return
		if self.overnight_mode and not self.model_api_ready():
			self.pending_overnight_automation = True
			self.start_model_if_needed_for_overnight()
			return
		self.start(self.last_command, "Goal automation", self.last_passes)

	def stop_process(self) -> None:
		if self.process is None:
			return
		if self.overnight_mode:
			title = "Stop overnight run?"
			message = (
				"Stopping now ends the overnight session and writes a session report. "
				"The current model response may not reach a verified Git commit. Stop anyway?"
			)
		else:
			title = "Stop automation?"
			message = (
				"The current model response may not have reached an applied, verified Git commit. "
				"Stopping now discards incomplete model output. Stop anyway?"
			)
		answer = QMessageBox.question(self, title, message,
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
			if self.overnight_mode:
				self.finish_overnight_session("GUI closed while idle overnight session")
			self.closing = True
			self.timer.stop()
			self.shutdown_dashboard_threads()
			event.accept()
			return
		if self.overnight_mode and (self.process is not None or self.model_process is not None):
			title = "Overnight run is active"
			message = (
				"Goal automation and/or vLLM are still running. Minimize this window instead of "
				"closing it if you want the overnight porting session to continue. Close anyway?"
			)
		elif self.process is not None:
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
			self.overnight_watchdog.stop()
			if self.overnight_mode:
				self.flush_overnight_log()
				write_overnight_session_report(
					self.repo(),
					self.build_overnight_report("GUI closed during active run"),
				)
				self.overnight_mode = False
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
		if hasattr(self, "_viewport_resize_timer"):
			self._viewport_scaled_path = ""
			self._viewport_resize_timer.start()

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
		self.set_chip_state(self.dol_chip, artifact("DOL", dol), "ok" if dol.is_file() else "warn")
		self.set_chip_state(self.iso_chip, artifact("ISO", iso), "ok" if iso.is_file() else "warn")
		active_node = self.pipeline_node(self.operation) if self.process else ""
		if active_node != "DOL":
			self.set_pipeline_state("DOL", "Success" if dol.is_file() else "Idle")
		if active_node != "ISO":
			self.set_pipeline_state("ISO", "Success" if iso.is_file() else "Idle")
		dolphin_ready = bool(os.environ.get("DOLPHIN_EXECUTABLE") or
			shutil.which("dolphin-emu") or shutil.which("dolphin") or shutil.which("flatpak"))
		self.set_chip_state(
			self.dolphin_chip,
			"DOLPHIN  READY" if dolphin_ready else "DOLPHIN  MISSING",
			"ok" if dolphin_ready else "warn",
		)

	def refresh_model_status(self) -> None:
		if self.model_process is not None:
			self.set_model_state("MODEL  RUNNING", GC_CYAN)
			self.start_model_btn.setEnabled(False)
			self.kill_model_btn.setEnabled(True)
		elif self.model_port_open():
			self.set_model_state("MODEL  READY", GC_MINT)
			self.start_model_btn.setEnabled(False)
			self.kill_model_btn.setEnabled(True)
		else:
			self.set_model_state("MODEL  DOWN", GC_ORANGE)
			self.start_model_btn.setEnabled(True)
			self.kill_model_btn.setEnabled(True)
		self.refresh_model_api_summary()


def main() -> int:
	load_dotenv(DEFAULT_REPO / ".env")
	load_gamecube_env(DEFAULT_REPO)
	app = QApplication(sys.argv)
	app.setStyle("Fusion")
	rodin_family = load_font(DEFAULT_REPO / "fonts/FOT-Rodin Pro DB.otf", "Sans Serif")
	gamecube_family = load_font(DEFAULT_REPO / "fonts/GameCube.ttf", rodin_family)
	app.setStyleSheet(stylesheet(gamecube_family))
	font = QFont(rodin_family, 9)
	font.setStyleHint(QFont.StyleHint.SansSerif)
	app.setFont(font)
	window = PortWindow(gamecube_family)
	window.show()
	window.status_bar.showMessage(f"Xash3D GameCube Porting Console {APP_VERSION}", 8000)
	return app.exec()


if __name__ == "__main__":
	raise SystemExit(main())
