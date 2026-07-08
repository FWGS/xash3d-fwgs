#!/usr/bin/env python3
"""Shared helpers for GameCube port automation scripts."""

from __future__ import annotations

import fcntl
import os
import re
import subprocess
import sys
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

try:
    import fcntl as _fcntl  # noqa: F401
except ImportError:  # pragma: no cover
    fcntl = None


def repo_root(start: Path | None = None) -> Path:
    if start is None:
        start = Path(__file__).resolve()

    for candidate in (start, *start.parents):
        if (candidate / ".git").exists():
            return candidate.resolve()

    try:
        out = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=True,
        )
        return Path(out.stdout.strip()).resolve()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return Path(__file__).resolve().parents[2]


REPO = repo_root()


def load_dotenv(path: Path | None = None) -> None:
    env_path = path or (REPO / ".env")
    if not env_path.is_file():
        return

    for raw_line in env_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        if line.startswith("export "):
            line = line[len("export ") :].lstrip()
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip().strip("'\"")
        if key and key not in os.environ:
            os.environ[key] = value


def source_gamecube_env() -> None:
    env_script = REPO / "scripts/gamecube-env.sh"
    if not env_script.is_file():
        return

    command = f"source {env_script}; env -0"
    proc = subprocess.run(
        ["bash", "-lc", command],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if proc.returncode != 0:
        return

    for entry in proc.stdout.split("\0"):
        if not entry or "=" not in entry:
            continue
        key, value = entry.split("=", 1)
        os.environ.setdefault(key, value)


def bootstrap_env() -> None:
    load_dotenv()
    source_gamecube_env()
    os.environ.setdefault("OPENAI_API_KEY", "local")
    os.environ.setdefault("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")


def api_models_url(api_base: str) -> str:
    parsed = urlparse(api_base)
    if parsed.path.rstrip("/").endswith("/v1"):
        return api_base.rstrip("/") + "/models"
    return api_base.rstrip("/") + "/v1/models"


def model_ready(api_base: str | None = None) -> bool:
    api_base = api_base or os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")
    request = Request(api_models_url(api_base))
    if os.environ.get("OPENAI_API_KEY"):
        request.add_header("Authorization", f"Bearer {os.environ['OPENAI_API_KEY']}")
    try:
        with urlopen(request, timeout=3) as response:
            return 200 <= response.status < 500
    except (OSError, URLError):
        return False


class SupervisorLock:
    def __init__(self, name: str = "gc-port-loop"):
        self.path = REPO / f".ai/{name}.lock"
        self._file = None

    def acquire(self) -> bool:
        if fcntl is None:
            return True

        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open("w", encoding="utf-8")
        try:
            fcntl.flock(self._file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            self._file.close()
            self._file = None
            return False

        self._file.write(str(os.getpid()))
        self._file.truncate()
        self._file.flush()
        return True

    def release(self) -> None:
        if self._file is None:
            return
        try:
            fcntl.flock(self._file.fileno(), fcntl.LOCK_UN)
        except OSError:
            pass
        self._file.close()
        self._file = None
        self.path.unlink(missing_ok=True)


def run(cmd: list[str], *, cwd: Path | None = None, timeout: int | None = None) -> tuple[int, str]:
    print("+", " ".join(cmd), flush=True)
    proc = subprocess.run(
        cmd,
        cwd=cwd or REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    output = proc.stdout or ""
    if output:
        print(output, end="" if output.endswith("\n") else "\n", flush=True)
    return proc.returncode, output


def git_status_lines(*, ignore_submodules: str = "untracked") -> list[str]:
    _, out = run(["git", "status", "--short", f"--ignore-submodules={ignore_submodules}"])
    return out.splitlines()


def git_changed_files(*, ignore_submodules: str = "untracked") -> list[str]:
    files = []
    for line in git_status_lines(ignore_submodules=ignore_submodules):
        if len(line) > 3:
            files.append(line[3:].strip())
    return files


def git_dirty_submodule_gitlinks() -> list[str]:
    dirty = []
    for line in git_status_lines(ignore_submodules="none"):
        if len(line) <= 3:
            continue
        path = line[3:].strip()
        if " -> " in path:
            path = path.split(" -> ", 1)[1].strip()
        if path in {"3rdparty/dolphin", "3rdparty/library_suffix"}:
            dirty.append(path)
    return dirty


def git_dirty_source_paths() -> list[str]:
    watched_roots = (
        "scripts",
        "engine",
        "ref",
        "stub",
        "public",
        "3rdparty/hlsdk-portable",
    )
    source_exts = {".c", ".cpp", ".cc", ".h", ".hpp", ".hh", ".cmake", ".py", ".sh"}
    source_names = {"CMakeLists.txt", "wscript", "wscript_build"}

    dirty = []
    for line in git_changed_files():
        path = line
        if " -> " in path:
            path = path.split(" -> ", 1)[1].strip()

        if path.startswith(".ai/") or path == "nohup.out":
            continue

        if not any(path.startswith(root + "/") or path == root for root in watched_roots):
            continue

        suffix = Path(path).suffix
        if suffix in source_exts or Path(path).name in source_names:
            dirty.append(path)

    return dirty


def commit_changes(message: str) -> bool:
    sync_script = REPO / "scripts/gamecube-submodule-sync.sh"
    if sync_script.is_file():
        run(["bash", str(sync_script), "--no-parent-commit"])

    changed = git_changed_files()
    if not changed:
        print("No changes to commit.")
        return False

    run(["git", "add", "-A"])
    code, _ = run(["git", "commit", "-m", message])
    return code == 0


def normalize_repo_path(path: str) -> str:
    path = path.strip().strip('"').strip("'")
    if path.startswith(str(REPO)):
        try:
            return str(Path(path).resolve().relative_to(REPO))
        except ValueError:
            return path

    path = path.lstrip("./")
    if path.startswith("../"):
        path = path[3:]
    return path


def repo_path_pattern() -> str:
    return re.escape(str(REPO))


def ensure_agent_imports() -> None:
    script_dir = Path(__file__).resolve().parent
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))
