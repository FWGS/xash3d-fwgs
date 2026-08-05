# encoding: utf-8
"""Host mirror of filesystem/probe_save_gc.c path/name rules (G508).

Pure Python — no GameCube toolchain required. Keep in sync with:
  GC_ProbeSaveBasename, GC_ProbeSaveIsConfigName, GC_ProbeSavePathMatch
"""
from __future__ import annotations

from typing import Optional


CONFIG_NAMES = frozenset({
	"config.cfg",
	"config.cfg.new",
	"config.cfg.bak",
	"userconfig.cfg",
	"vfs.cfg",
	"vfs.cfg.new",
	"vfs.cfg.bak",
})


def probe_save_basename(path: Optional[str]) -> Optional[str]:
	if not path:
		return None
	slash = max(path.rfind("/"), path.rfind("\\"))
	return path[slash + 1 :] if slash >= 0 else path


def probe_save_is_config_name(base: Optional[str]) -> bool:
	if not base:
		return False
	return base.lower() in {name.lower() for name in CONFIG_NAMES}


def _ci_contains(haystack: str, needle: str) -> bool:
	return needle.lower() in haystack.lower()


def probe_save_path_match(
	path: Optional[str],
	*,
	enabled: bool,
	config_roundtrip: bool = True,
	newsaveload: bool = True,
) -> bool:
	"""Mirror GC_ProbeSavePathMatch when the probe bank is enabled."""
	if not enabled or not path:
		return False
	# Engine save paths under write root.
	lower = path.replace("\\", "/").lower()
	if lower.startswith("save/") or "/save/" in lower:
		return True
	if path.lower().startswith("gcprobe:"):
		return True
	if config_roundtrip or newsaveload:
		base = probe_save_basename(path)
		if probe_save_is_config_name(base):
			return True
	return False


def probe_save_rejects_non_gcprobe_paths(
	path: str,
	*,
	enabled: bool = True,
) -> bool:
	"""True when path must NOT enter the probe bank (ordinary game data)."""
	return not probe_save_path_match(
		path,
		enabled=enabled,
		config_roundtrip=True,
		newsaveload=True,
	)


class ProbeSaveBank:
	"""Minimal rename/delete simulator for Host_WriteConfig .new/.bak dance."""

	def __init__(self) -> None:
		self.files: dict[str, bytes] = {}

	def write(self, name: str, data: bytes) -> None:
		base = probe_save_basename(name) or name
		self.files[base] = data

	def read(self, name: str) -> Optional[bytes]:
		base = probe_save_basename(name) or name
		return self.files.get(base)

	def rename(self, old: str, new: str) -> bool:
		old_b = probe_save_basename(old) or old
		new_b = probe_save_basename(new) or new
		if old_b not in self.files:
			return False
		self.files[new_b] = self.files.pop(old_b)
		return True

	def delete(self, path: str) -> bool:
		base = probe_save_basename(path) or path
		if base not in self.files:
			return False
		del self.files[base]
		return True

	def config_roundtrip(self, body: bytes = b"unbindall\n") -> bool:
		"""Simulate Host_WriteConfig atomic dance on the probe bank."""
		self.write("config.cfg.new", body)
		# rotate: config.cfg -> config.cfg.bak, then .new -> config.cfg
		if "config.cfg" in self.files:
			self.rename("config.cfg", "config.cfg.bak")
		if not self.rename("config.cfg.new", "config.cfg"):
			return False
		return self.read("config.cfg") == body


# Expected guest markers for G508 fault/happy paths (host mirror of vid_gamecube.c).
G508_MARKERS = {
	"begin": "G508 config round trip begin",
	"write_ready": "G508 config write ready",
	"write_failed": "G508 config write failed",
	"read_ready": "G508 config read ready",
	"read_failed": "G508 config read failed",
	"ready": "G508 config round trip ready",
	"no_writable": "no writable storage",
	"writeconfig_disabled": "host_writeconfig: disabled",
}


def simulate_g508_fault(kind: str) -> dict:
	"""Return expected marker sets for G508 missing/corrupt/read-only/write-fail cases."""
	kind = (kind or "").lower().strip()
	if kind in {"happy", "ok", "ready"}:
		return {
			"kind": "happy",
			"ok": True,
			"expected_any": (G508_MARKERS["ready"],),
			"forbidden": (G508_MARKERS["write_failed"], G508_MARKERS["read_failed"]),
		}
	if kind in {"write_fail", "write-failed", "write_failed"}:
		return {
			"kind": "write_fail",
			"ok": False,
			"expected_any": (G508_MARKERS["write_failed"],),
			"forbidden": (G508_MARKERS["ready"],),
		}
	if kind in {"read_fail", "read-failed", "read_failed", "corrupt"}:
		return {
			"kind": "read_fail",
			"ok": False,
			"expected_any": (G508_MARKERS["read_failed"],),
			"forbidden": (G508_MARKERS["ready"],),
		}
	if kind in {"missing", "no_writable", "readonly", "read-only"}:
		return {
			"kind": "no_writable",
			"ok": False,
			"expected_any": (
				G508_MARKERS["no_writable"],
				G508_MARKERS["writeconfig_disabled"],
				G508_MARKERS["write_failed"],
			),
			"forbidden": (G508_MARKERS["ready"],),
		}
	raise ValueError(f"unknown G508 fault kind: {kind!r}")


def classify_g508_log(text: str) -> dict:
	"""Classify probe log text against G508 happy/fault marker sets."""
	body = text or ""
	if G508_MARKERS["ready"] in body and G508_MARKERS["write_failed"] not in body:
		return {"kind": "happy", "ok": True, "matched": G508_MARKERS["ready"]}
	for kind in ("write_fail", "read_fail", "missing"):
		spec = simulate_g508_fault(kind)
		if any(token in body for token in spec["expected_any"]):
			return {"kind": spec["kind"], "ok": False, "matched": [
				token for token in spec["expected_any"] if token in body
			]}
	return {"kind": "unseen", "ok": False, "matched": []}


class ProbeSaveBankFault(ProbeSaveBank):
	"""Probe bank variants that model writable-storage failures."""

	def __init__(self, *, readonly: bool = False, fail_write: bool = False):
		super().__init__()
		self.readonly = readonly
		self.fail_write = fail_write

	def write(self, name: str, data: bytes) -> None:
		if self.readonly or self.fail_write:
			raise OSError("G508 config write failed (host mirror)")
		super().write(name, data)

	def config_roundtrip(self, body: bytes = b"unbindall\n") -> bool:
		if self.readonly or self.fail_write:
			return False
		return super().config_roundtrip(body)


if __name__ == "__main__":
	import json

	samples = [
		"gcprobe:/xash3d/valve/config.cfg",
		"save/quick.sav",
		"valve/maps/c0a0.bsp",
		"config.cfg.new",
	]
	print(json.dumps({
		"paths": {path: probe_save_path_match(path, enabled=True) for path in samples},
		"faults": {kind: simulate_g508_fault(kind) for kind in ("happy", "write_fail", "read_fail", "missing")},
	}, indent=2))
