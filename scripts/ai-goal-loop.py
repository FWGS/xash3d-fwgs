#!/usr/bin/env python3
"""Advance the Xash3D GameCube port through evidence-gated Aider goals."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import shutil
import subprocess
import sys
import tempfile
from itertools import count
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

try:
	import fcntl
except ImportError:  # pragma: no cover - non-Unix fallback
	fcntl = None

GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |~|x|X|MANUAL)\]\s+(.+)$")
DOLPHIN_PROBE_GOALS = frozenset({"G14", "G19", "G21", "G34", "G35", "G40"})
PROBE_AUTO_COMPLETE = {
	"G19": "MAP_READY:",
	"G35": "MAP_READY:",
}
MEMORY_FILE = Path(".ai/state/goal-loop-memory.json")
MEMORY_MAX_DRAWERS = 12
MEMORY_MAX_TOOL_CALLS = 40
CONACT_HISTORY_MAX = 10
CONACT_FACT_MAX = 24
CONACT_RECENT_MAX = 6
FAULT_PATTERNS = (
	r"Host_ErrorInit:.*",
	r"Host_Error:.*",
	r"Sys_Error:.*",
	r"_Mem_Alloc.*",
	r"Could not load .*",
	r"missing .*",
	r"fatal.*",
	r"verify: .*failed",
	r"token/context limit",
	r"Aider made no edit",
	r"MAP_READY:.*",
	r"DIAGNOSTIC MARKER VISIBLE",
	r"black screen",
	r"audio .*",
	r"read-only fallback.*",
)
COMMON_CONTEXT = (
	"docs/GAMECUBE_PORT_PLAN.md",
	".ai/goals/GAMECUBE_PORT_GOALS.md",
)
COMMON_READ_CONTEXT = (
	".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
	".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
)
GOAL_CONTEXT = {
	"G01": ("engine/server/sv_game.c", "engine/server/server.h",
		"engine/edict.h", "engine/progdefs.h"),
	"G02": ("scripts/build-gamecube-disc.py", "scripts/gamecube-apploader.c",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G03": ("engine/platform/gamecube/vid_gamecube.c", "ref/gx/r_context.c",
		"ref/gx/r_main.c", "ref/gx/r_local.h"),
	"G04": ("engine/platform/gamecube/in_gamecube.c", "engine/client/input.h",
		"engine/client/input/input.c"),
	"G05": ("engine/client/sound/s_main.c", "engine/client/sound.h",
		"engine/platform/gamecube/dll_gamecube.c"),
	"G06": ("engine/platform/gamecube/sys_gamecube.c", "engine/host.c",
		"filesystem/filesystem.c"),
	"G07": (),
	"G09": ("scripts/hlsdk-gamecube-probe.sh", "scripts/build-gamecube.sh",
		"scripts/ai-verify.sh", "Documentation/development/engine-porting-guide.md"),
	"G10": ("scripts/hlsdk-gamecube-probe.sh", "scripts/hlsdk-gamecube-build.sh",
		"scripts/build-gamecube.sh",
		"scripts/gha/build_nswitch_docker.sh", "scripts/gha/build_psvita.sh"),
	"G11": ("scripts/hlsdk-gamecube-probe.sh", "scripts/hlsdk-gamecube-build.sh",
		"scripts/hlsdk-gamecube-apply-patch.py",
		"Documentation/development/engine-porting-guide.md"),
	"G12": ("engine/platform/gamecube/dll_gamecube.c", "engine/wscript",
		"stub/client/client_export.c", "stub/server/server_export.c",
		"scripts/hlsdk-gamecube-build.sh", "scripts/hlsdk-gamecube-apply-patch.py"),
	"G13": ("engine/wscript", "stub/client/client_export.c",
		"scripts/hlsdk-gamecube-build.sh", "scripts/hlsdk-gamecube-apply-patch.py"),
	"G14": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G16": ("engine/platform/gamecube/sys_gamecube.c",
		"engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/cl_scrn.c",
		"engine/client/dll_int/cl_game.c"),
	"G17": ("engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/sound/s_main.c", "engine/client/sound.h",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G18": ("engine/common/host.c", "engine/common/system.c",
		"engine/common/filesystem_engine.c", "engine/common/net_ws.h",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G19": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/platform/gamecube/sys_gamecube.c",
		"engine/platform/gamecube/in_gamecube.c"),
	"G21": ("engine/common/host.c", "engine/common/model.c",
		"engine/common/filesystem_engine.c", "engine/common/system.c",
		"scripts/dolphin-boot-probe.sh"),
	"G22": ("engine/common/zone.c", "engine/common/common.h",
		"engine/common/host.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G23": ("engine/common/zone.c", "engine/common/mod_bmodel.c",
		"engine/common/mod_studio.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G24": ("engine/platform/gamecube/vid_gamecube.c",
		"engine/client/cl_scrn.c", "engine/client/cl_sprite.c",
		"engine/client/cl_tent.c", "engine/client/dll_int/cl_render.c",
		"engine/common/mod_studio.c", "engine/common/mod_bmodel.c",
		"ref/gx/r_context.c", "ref/gx/r_main.c", "ref/gx/r_surf.c",
		"ref/gx/r_studio.c", "ref/gx/r_part.c", "ref/gx/r_sprite.c",
		"ref/gx/r_image.c", "ref/gx/r_local.h"),
	"G25": ("engine/client/dll_int/cl_game.c", "engine/client/cl_scrn.c",
		"stub/client/client_export.c", "3rdparty/hlsdk-portable/cl_dll/hud.cpp",
		"3rdparty/hlsdk-portable/cl_dll/hud.h"),
	"G26": ("engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/sound/s_main.c", "engine/client/soundlib/snd_main.c",
		"engine/client/sound.h"),
	"G27": ("engine/client/dll_int/cl_game.c",
		"engine/client/sound/s_main.c", "engine/common/sounds.c",
		"engine/platform/gamecube/snddma_gamecube.c"),
	"G28": ("engine/common/filesystem_engine.c", "engine/common/host.c",
		"engine/common/system.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G29": ("engine/common/net_ws.h", "engine/common/net_buffer.c",
		"engine/server/sv_client.c", "engine/client/cl_main.c",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G30": ("engine/platform/gamecube/in_gamecube.c", "engine/client/input.h",
		"engine/client/input/input.c", "engine/client/console.c"),
	"G31": ("engine/server/sv_init.c", "engine/server/sv_client.c",
		"engine/client/cl_main.c", "engine/common/host.c"),
	"G32": ("engine/server/sv_init.c", "engine/server/sv_client.c",
		"engine/common/filesystem_engine.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G33": ("scripts/build-gamecube-disc.py", "scripts/dolphin-boot-probe.sh",
		"scripts/ai-verify.sh", ".gitignore"),
	"G34": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/common/host.c", "engine/common/model.c"),
	"G35": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/server/sv_init.c", "engine/common/model.c",
		"engine/common/filesystem_engine.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G36": ("engine/platform/gamecube/vid_gamecube.c", "engine/client/cl_scrn.c",
		"engine/common/mod_bmodel.c", "engine/common/mod_studio.c"),
	"G37": ("engine/common/host.c", "engine/common/system.c",
		"engine/common/zone.c", "engine/platform/gamecube/sys_gamecube.c",
		"scripts/dolphin-boot-probe.sh"),
	"G38": ("scripts/build-gamecube-disc.py", "scripts/dolphin-boot-probe.sh",
		"docs/GAMECUBE_PORT_PLAN.md", "docs/GAMECUBE_HARDWARE_VALIDATION.md"),
	"G39": ("scripts/build-gamecube-disc.py", "Documentation/development/engine-porting-guide.md",
		"docs/GAMECUBE_PORT_PLAN.md"),
	"G40": ("scripts/gamecube-campaign-audit.sh", "scripts/dolphin-boot-probe.sh",
		"scripts/build-gamecube-disc.py", "docs/GAMECUBE_PORT_PLAN.md"),
	"G41": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/dolphin-boot-probe.sh", "scripts/ai-verify.sh"),
	"G42": ("docs/GAMECUBE_PORT_PLAN.md", ".ai/goals/GAMECUBE_PORT_GOALS.md",
		"Documentation/development/engine-porting-guide.md"),
	"G43": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/dolphin-boot-probe.sh", "docs/GAMECUBE_HARDWARE_VALIDATION.md"),
	"G44": ("engine/platform/gamecube/vid_gamecube.c", "engine/client/cl_scrn.c",
		"docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G45": ("engine/platform/gamecube/in_gamecube.c", "engine/client/input.h",
		"engine/client/input/input.c", "docs/GAMECUBE_HARDWARE_VALIDATION.md"),
	"G46": ("engine/common/filesystem_engine.c", "engine/common/host.c",
		"engine/server/sv_init.c", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G47": ("scripts/build-gamecube-disc.py", "engine/common/filesystem_engine.c",
		"scripts/ai-verify.sh", ".gitignore"),
	"G48": ("engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/sound/s_main.c", "engine/client/soundlib/snd_main.c",
		".ai/prompts/GAMECUBE_AUDIO_NOTES.md"),
	"G49": ("engine/platform/gamecube/vid_gamecube.c", "engine/common/host.c",
		"engine/common/zone.c", ".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G50": ("engine/common/host.c", "engine/common/system.c",
		"engine/common/zone.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G51": ("engine/platform/gamecube/in_gamecube.c", "engine/client/console.c",
		"engine/client/cl_scrn.c", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G52": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/gamecube-homebrew-compliance-check.py",
		"docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G53": ("docs/GAMECUBE_HARDWARE_VALIDATION.md",
		"docs/GAMECUBE_PORT_PLAN.md", ".ai/goals/GAMECUBE_PORT_GOALS.md"),
	"G54": ("engine/common/host.c", "engine/common/zone.c",
		"scripts/dolphin-boot-probe.sh", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G55": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/ai-verify.sh", ".gitignore", "docs/GAMECUBE_PORT_PLAN.md"),
	"G56": ("docs/GAMECUBE_HARDWARE_VALIDATION.md",
		"docs/GAMECUBE_HOMEBREW_COMPLIANCE.md", "docs/GAMECUBE_PORT_PLAN.md"),
	"G57": ("engine/common/zone.c", "engine/platform/gamecube/sys_gamecube.c",
		"engine/platform/gamecube/vid_gamecube.c", ".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G58": ("engine/common/filesystem_engine.c", "engine/common/host.c",
		"engine/server/sv_init.c", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G59": ("engine/platform/gamecube/in_gamecube.c", "engine/client/input.h",
		"engine/client/input/input.c", "engine/client/console.c"),
	"G60": ("engine/client/cl_scrn.c", "engine/common/host.c",
		"engine/common/filesystem_engine.c", "engine/platform/gamecube/vid_gamecube.c"),
	"G61": ("engine/platform/gamecube/vid_gamecube.c", "engine/platform/gamecube/sys_gamecube.c",
		"engine/client/cl_scrn.c", "docs/GAMECUBE_PORT_PLAN.md"),
	"G62": ("scripts/dolphin-boot-probe.sh", "engine/server/sv_phys.c",
		"engine/server/sv_client.c", "engine/client/cl_main.c"),
	"G63": ("scripts/dolphin-boot-probe.sh", "engine/server/sv_init.c",
		"engine/server/sv_phys.c", "engine/common/host.c"),
	"G64": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/dolphin-boot-probe.sh", "scripts/ai-verify.sh",
		"scripts/gamecube-homebrew-compliance-check.py"),
	"G65": ("docs/GAMECUBE_PORT_PLAN.md", "docs/GAMECUBE_HARDWARE_VALIDATION.md",
		".ai/goals/GAMECUBE_PORT_GOALS.md", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G66": ("docs/GAMECUBE_HARDWARE_VALIDATION.md",
		"docs/GAMECUBE_PORT_PLAN.md", ".ai/goals/GAMECUBE_PORT_GOALS.md"),
}
GOAL_REQUIRED_CONTEXT = {
	"G24": ("ref/gx/r_context.c", "ref/gx/r_main.c", "ref/gx/r_surf.c",
		"ref/gx/r_studio.c", "ref/gx/r_part.c", "ref/gx/r_sprite.c",
		"ref/gx/r_image.c", "ref/gx/r_local.h"),
}
GOAL_CONTEXT_SLICES = {
	"G24": (
		("ref/gx/r_main.c",),
		("ref/gx/r_surf.c",),
		("engine/platform/gamecube/vid_gamecube.c", "engine/client/cl_sprite.c",
			"ref/gx/r_part.c", "ref/gx/r_sprite.c"),
		("ref/gx/r_image.c",),
		("ref/gx/r_local.h",),
	),
	"G36": (
		("engine/platform/gamecube/vid_gamecube.c", "engine/client/cl_scrn.c"),
		("engine/common/mod_bmodel.c",),
		("engine/common/mod_studio.c",),
		("scripts/dolphin-boot-probe.sh",),
	),
}
G24_SUBGOALS = (
	{
		"id": "G24a",
		"title": "renderer quality entrypoint",
		"files": ("ref/gx/r_main.c",),
		"focus": (
			"Keep renderer initialization wired to the low-memory quality mode "
			"without duplicating `GC_GetVisualQuality` declarations."
		),
	},
	{
		"id": "G24b",
		"title": "surface cache and world draw budget",
		"files": ("ref/gx/r_surf.c",),
		"focus": (
			"Bound world-surface cache behavior and fallback drawing for quality 0 "
			"while preserving higher-quality paths."
		),
	},
	{
		"id": "G24c",
		"title": "sprites, particles, and client visual skips",
		"files": ("engine/platform/gamecube/vid_gamecube.c", "engine/client/cl_sprite.c",
			"ref/gx/r_part.c", "ref/gx/r_sprite.c"),
		"focus": (
			"Replace smoke-only sprite or particle skips with stable quality-aware "
			"fallback modes."
		),
	},
	{
		"id": "G24d",
		"title": "image upload and texture pressure",
		"files": ("ref/gx/r_image.c",),
		"focus": (
			"Reduce texture-memory pressure or add quality-aware fallbacks without "
			"breaking normal texture upload."
		),
	},
	{
		"id": "G24e",
		"title": "renderer local quality helpers",
		"files": ("ref/gx/r_local.h",),
		"focus": (
			"Keep shared renderer quality helpers simple, local, and safe for "
			"non-GameCube builds."
		),
	},
)
GOAL_READ_CONTEXT = {
	"G03": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G05": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G14": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G15": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G16": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md"),
	"G17": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",),
	"G18": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G19": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G21": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G22": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",),
	"G23": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",),
	"G24": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G25": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G26": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G27": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G28": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",),
	"G29": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",),
	"G30": (".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",),
	"G31": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G32": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G33": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",),
	"G34": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G35": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G36": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G37": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G38": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
		".ai/prompts/GAMECUBE_HARDWARE_NOTES.md"),
	"G39": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",),
	"G40": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_AUDIO_NOTES.md"),
	"G41": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G42": (".ai/prompts/GAMECUBE_CONTEXT_INDEX.md",
		".ai/prompts/GAMECUBE_HARDWARE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G43": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G44": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md"),
	"G45": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md"),
	"G46": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G47": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G48": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_AUDIO_NOTES.md"),
	"G49": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G50": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G51": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md"),
	"G52": (".ai/prompts/GAMECUBE_CONTEXT_INDEX.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G53": (".ai/prompts/GAMECUBE_HARDWARE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G54": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G55": (".ai/prompts/GAMECUBE_CONTEXT_INDEX.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G56": (".ai/prompts/GAMECUBE_HARDWARE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G57": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G58": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G59": (".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G60": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G61": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G62": (".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G63": (".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G64": (".ai/prompts/GAMECUBE_CONTEXT_INDEX.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G65": (".ai/prompts/GAMECUBE_HARDWARE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G66": (".ai/prompts/GAMECUBE_HARDWARE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
}
GOAL_COMMIT_SUBJECT = {
	"G01": "fix: resolve GameCube edict warning audit",
	"G02": "feat: improve bounded Dolphin boot probing",
	"G03": "feat: advance GameCube GX video",
	"G04": "feat: advance GameCube controller input",
	"G05": "feat: advance GameCube audio",
	"G06": "feat: advance GameCube engine startup",
	"G07": "feat: advance GameCube map loading",
	"G09": "feat: probe GameCube HLSDK integration",
	"G10": "feat: build GameCube HLSDK",
	"G11": "feat: add GameCube HLSDK hooks",
	"G12": "feat: integrate GameCube HLSDK exports",
	"G13": "feat: integrate GameCube HLSDK client",
	"G14": "test: smoke GameCube map loading",
	"G16": "feat: stabilize GameCube client modes",
	"G17": "feat: advance GameCube audio mode",
	"G18": "feat: harden GameCube local startup",
	"G19": "test: prove GameCube gameplay smoke",
	"G21": "fix: resolve GameCube map lookup",
	"G22": "feat: add GameCube memory telemetry",
	"G23": "feat: define GameCube memory budget",
	"G24": "feat: stabilize GameCube visuals",
	"G25": "feat: stabilize GameCube HUD",
	"G26": "feat: add GameCube audio backend",
	"G27": "feat: define GameCube music policy",
	"G28": "feat: route GameCube writable storage",
	"G29": "feat: restore GameCube local networking",
	"G30": "feat: improve GameCube controls",
	"G31": "feat: support GameCube changelevel",
	"G32": "feat: support GameCube save load",
	"G33": "feat: validate GameCube content staging",
	"G34": "test: probe GameCube campaign maps",
	"G35": "test: prove GameCube early route",
	"G36": "perf: improve GameCube frame budget",
	"G37": "feat: harden GameCube diagnostics",
	"G38": "test: validate GameCube hardware",
	"G39": "docs: define GameCube loader matrix",
	"G40": "test: audit GameCube campaign",
	"G41": "build: prepare GameCube release scripts",
	"G42": "docs: finalize GameCube port guide",
	"G43": "test: validate GameCube boot media",
	"G44": "test: validate GameCube video modes",
	"G45": "feat: harden GameCube controller states",
	"G46": "feat: harden GameCube save integrity",
	"G47": "test: audit GameCube filesystem behavior",
	"G48": "test: validate GameCube audio behavior",
	"G49": "perf: validate GameCube frame timing",
	"G50": "feat: improve GameCube fatal errors",
	"G51": "feat: complete GameCube UX checks",
	"G52": "build: add GameCube release manifest",
	"G53": "docs: track GameCube hardware matrix",
	"G54": "test: add GameCube compliance evidence",
	"G55": "build: add GameCube artifact manifest checks",
	"G56": "docs: add GameCube hardware boot checklist",
	"G57": "perf: gate GameCube runtime memory thresholds",
	"G58": "test: prove GameCube writable media round trips",
	"G59": "feat: finalize GameCube controller profiles",
	"G60": "feat: add GameCube loading feedback",
	"G61": "feat: define final GameCube quality profiles",
	"G62": "test: validate GameCube combat route",
	"G63": "test: validate GameCube scripted route",
	"G64": "test: add GameCube release candidate suite",
	"G65": "docs: freeze GameCube release candidate notes",
	"G66": "test: sign off GameCube hardware release",
}
RECOVERABLE_EXIT_CODES = {
	10: "Aider made no edit",
	16: "Automation harness preflight failed",
	17: "Aider model call timed out",
	18: "Aider hit a token/context limit",
}
UNLIMITED_PASSES_LABEL = "unlimited"


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


@dataclass
class Goal:
	goal_id: str
	state: str
	title: str
	body: str

	@property
	def complete(self) -> bool:
		return self.state.lower() == "x"

	@property
	def manual(self) -> bool:
		return self.state == "MANUAL"

	@property
	def blocked(self) -> bool:
		return bool(re.search(r"(?im)^\s*-\s*Status:\s*BLOCKED\b", self.body))

	@property
	def automatic_done(self) -> bool:
		return self.complete or self.manual or self.blocked


def parse_goals(path: Path) -> list[Goal]:
	goals: list[Goal] = []
	current: tuple[str, str, str] | None = None
	body: list[str] = []
	for line in path.read_text(encoding="utf-8").splitlines():
		match = GOAL_RE.match(line)
		if match:
			if current:
				goals.append(Goal(*current, "\n".join(body).strip()))
			current = match.groups()
			body = []
		elif current:
			body.append(line)
	if current:
		goals.append(Goal(*current, "\n".join(body).strip()))
	return goals


def run(command: list[str], root: Path, *, capture: bool = False,
	env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
	print("$ " + " ".join(command), flush=True)
	return subprocess.run(command, cwd=root, text=True, check=False,
		capture_output=capture, env=env or os.environ.copy())


def harness_preflight(root: Path) -> subprocess.CompletedProcess[str]:
	scripts = ("scripts/ai-aider-pass.sh", "scripts/ai-verify.sh")
	output: list[str] = []
	for script in scripts:
		result = run(["bash", "-n", script], root, capture=True)
		output.append((result.stdout or "") + (result.stderr or ""))
		if result.returncode != 0:
			return subprocess.CompletedProcess(["bash", "-n", *scripts],
				result.returncode, "".join(output), "")
	return subprocess.CompletedProcess(["bash", "-n", *scripts], 0,
		"".join(output), "")


def commit_with_body(root: Path, subject: str, body: str) -> int:
	command = ["git", "commit", "-m", subject]
	if body.strip():
		command.extend(["-m", body.strip()])
	return run(command, root).returncode


def git_context(root: Path) -> str:
	commands = (
		["git", "status", "--short", "--branch"],
		["git", "log", "-5", "--oneline"],
		["git", "submodule", "status", "--recursive"],
	)
	chunks: list[str] = []
	for command in commands:
		result = run(command, root, capture=True)
		chunks.append(f"$ {' '.join(command)}\n{result.stdout.strip()}")
	return "\n\n".join(chunks)


def api_models_url(api_base: str) -> str:
	parsed = urlparse(api_base)
	if parsed.path.rstrip("/").endswith("/v1"):
		return api_base.rstrip("/") + "/models"
	return api_base.rstrip("/") + "/v1/models"


def model_ready(api_base: str) -> bool:
	request = Request(api_models_url(api_base))
	if os.environ.get("OPENAI_API_KEY"):
		request.add_header("Authorization", f"Bearer {os.environ['OPENAI_API_KEY']}")
	try:
		with urlopen(request, timeout=3) as response:
			return 200 <= response.status < 500
	except (OSError, URLError):
		return False


def dolphin_executable() -> str:
	if os.environ.get("DOLPHIN_EXECUTABLE"):
		return os.environ["DOLPHIN_EXECUTABLE"]
	if shutil.which("dolphin-emu"):
		return shutil.which("dolphin-emu") or "dolphin-emu"
	if shutil.which("dolphin"):
		return shutil.which("dolphin") or "dolphin"
	flatpak = shutil.which("flatpak")
	flatpak_id = os.environ.get("DOLPHIN_FLATPAK_ID", "org.DolphinEmu.dolphin-emu")
	if flatpak:
		result = subprocess.run([flatpak, "info", flatpak_id],
			text=True, capture_output=True, check=False)
		if result.returncode == 0:
			return f"flatpak:{flatpak_id}"
	return "unavailable"


def automation_context() -> str:
	api_base = os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")
	api_endpoint = "/models" if urlparse(api_base).path.rstrip("/").endswith("/v1") else "/v1/models"
	return "\n".join((
		f"MODEL_API=reachable OpenAI-compatible endpoint ({api_endpoint})",
		f"DOLPHIN_EXECUTABLE={dolphin_executable()}",
		f"DOLPHIN_FLATPAK_ID={os.environ.get('DOLPHIN_FLATPAK_ID', 'org.DolphinEmu.dolphin-emu')}",
	))


def run_dolphin_probe(root: Path) -> subprocess.CompletedProcess[str]:
	env = os.environ.copy()
	env.setdefault("DOLPHIN_TIMEOUT", os.environ.get("DOLPHIN_TIMEOUT", "120"))
	env.setdefault("DOLPHIN_EXECUTABLE", dolphin_executable())
	return run(["scripts/dolphin-boot-probe.sh"], root, capture=True, env=env)


def acquire_loop_lock(root: Path):
	if fcntl is None:
		return None
	lock_path = root / ".ai/goal-loop.lock"
	lock_path.parent.mkdir(parents=True, exist_ok=True)
	lock_file = lock_path.open("w", encoding="utf-8")
	try:
		fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
	except BlockingIOError:
		print("goal-loop: another goal loop is already running", file=sys.stderr)
		lock_file.close()
		return None
	lock_file.write(str(os.getpid()))
	lock_file.truncate()
	lock_file.flush()
	return lock_file


def probe_log_dir(output: str) -> str | None:
	match = re.search(r"^Logs: (.+)$", output, re.MULTILINE)
	return match.group(1).strip() if match else None


def probe_log_tail(root: Path, log_dir: str | None, *, lines: int = 80) -> str:
	if not log_dir:
		return "(no log directory recorded)"
	stderr = root / log_dir / "stderr.log"
	if not stderr.is_file():
		return f"(missing {stderr})"
	content = stderr.read_text(encoding="utf-8", errors="replace").splitlines()
	return "\n".join(content[-lines:])


def clip_text(text: str, limit: int = 360) -> str:
	text = re.sub(r"\s+", " ", text).strip()
	if len(text) <= limit:
		return text
	return text[:limit - 3].rstrip() + "..."


def load_loop_memory(root: Path) -> dict[str, object]:
	path = root / MEMORY_FILE
	if not path.is_file():
		return {
			"version": 2,
			"task_goal": "Port Xash3D to run Half-Life 1 on native GameCube hardware.",
			"rooms": {},
			"past_tool_calls": [],
			"conact": default_conact_state(),
		}
	try:
		data = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		data = {}
	if not isinstance(data, dict):
		data = {}
	data.setdefault("version", 2)
	data.setdefault("task_goal", "Port Xash3D to run Half-Life 1 on native GameCube hardware.")
	data.setdefault("rooms", {})
	data.setdefault("past_tool_calls", [])
	ensure_conact_state(data)
	return data


def save_loop_memory(root: Path, memory: dict[str, object]) -> None:
	path = root / MEMORY_FILE
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(memory, indent=2, sort_keys=True) + "\n",
		encoding="utf-8")


def memory_room(memory: dict[str, object], goal: Goal) -> dict[str, object]:
	rooms = memory.setdefault("rooms", {})
	if not isinstance(rooms, dict):
		rooms = {}
		memory["rooms"] = rooms
	key = f"goal:{goal.goal_id}"
	room = rooms.setdefault(key, {
		"title": goal.title,
		"drawers": [],
		"investigative_gaps": [],
	})
	if not isinstance(room, dict):
		room = {"title": goal.title, "drawers": [], "investigative_gaps": []}
		rooms[key] = room
	room["title"] = goal.title
	room.setdefault("drawers", [])
	room.setdefault("investigative_gaps", [])
	return room


def default_conact_state() -> dict[str, object]:
	return {
		"folded_action_history": [],
		"folded_port_state": [],
		"recent_step_record": [],
	}


def ensure_conact_state(memory: dict[str, object]) -> dict[str, object]:
	conact = memory.setdefault("conact", default_conact_state())
	if not isinstance(conact, dict):
		conact = default_conact_state()
		memory["conact"] = conact
	for key in ("folded_action_history", "folded_port_state", "recent_step_record"):
		if not isinstance(conact.get(key), list):
			conact[key] = []
	return conact


def conact_fact(fact_id: str, description: str, content: str,
	goal_id: str, evidence: str | None = None) -> dict[str, object]:
	return {
		"id": fact_id,
		"description": description,
		"content": clip_text(content, 520),
		"goal": goal_id,
		"evidence": evidence,
		"updated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
	}


def conact_upsert_fact(conact: dict[str, object], fact: dict[str, object]) -> None:
	facts = conact.setdefault("folded_port_state", [])
	if not isinstance(facts, list):
		facts = []
		conact["folded_port_state"] = facts
	for index, item in enumerate(facts):
		if isinstance(item, dict) and item.get("id") == fact.get("id"):
			facts[index] = fact
			break
	else:
		facts.append(fact)
	del facts[:-CONACT_FACT_MAX]


def conact_extract_facts(goal: Goal, phase: str, exit_code: int,
	output: str, log_path: str | None) -> list[dict[str, object]]:
	joined = output.lower()
	facts: list[dict[str, object]] = []
	evidence = log_path
	if "map_ready:" in joined or "xash3d gamecube: map loaded c0a0e" in joined:
		facts.append(conact_fact(
			"runtime.map_ready.c0a0e",
			"c0a0e reaches MAP_READY",
			"Treat G35 c0a0e map discovery as solved unless a newer probe regresses below MAP_READY.",
			goal.goal_id, evidence))
	if "could not load model maps" in joined and "map_ready:" not in joined:
		facts.append(conact_fact(
			f"blocker.{goal.goal_id}.asset_lookup",
			"latest asset lookup blocker",
			"Latest evidence mentions a missing model/map load path; verify against newer MAP_READY facts before acting.",
			goal.goal_id, evidence))
	if "diagnostic marker" in joined or "no non-black pixel" in joined:
		facts.append(conact_fact(
			"runtime.visual.diagnostic_marker",
			"visual output still needs source proof",
			"VI/XFB appears alive, but renderer content may still be black or missing; prefer renderer/client source fixes over more probe parsing.",
			goal.goal_id, evidence))
	if "frame budget" in joined or goal.goal_id in {"G36", "G49"}:
		facts.append(conact_fact(
			"runtime.frame_budget.g36_focus",
			"G36 should prioritize source-level frame/visual fixes",
			"Probe instrumentation is already extensive; avoid adding more G36_PATCH detectors unless replacing duplicated logic.",
			goal.goal_id, evidence))
	if "token/context" in joined or "context limit" in joined:
		facts.append(conact_fact(
			f"automation.{goal.goal_id}.context_budget",
			"context budget pressure",
			"Use the smallest source slice and folded memory; do not load broad docs unless required for acceptance evidence.",
			goal.goal_id, evidence))
	if phase == "review" and exit_code != 0:
		facts.append(conact_fact(
			f"automation.{goal.goal_id}.review_gate",
			"review gate blocked the pass",
			f"Review exited {exit_code}; inspect the verification gate before retrying with more edits.",
			goal.goal_id, evidence))
	return facts


def conact_dedupe_history(history: list[object]) -> None:
	"""Keep the newest entry for repeated failure signatures."""
	if len(history) < 2:
		return
	seen: dict[tuple[str, str], int] = {}
	for index, item in enumerate(history):
		if not isinstance(item, dict):
			continue
		summary = str(item.get("summary", ""))
		key = (str(item.get("span", "")), summary.split(";", 1)[0].strip())
		seen[key] = index
	keep = set(seen.values())
	history[:] = [item for index, item in enumerate(history) if index in keep]


def conact_record_step(memory: dict[str, object], goal: Goal, *, attempt: int,
	phase: str, exit_code: int, failure_class: str, hypothesis: str,
	output: str, log_path: str | None) -> None:
	conact = ensure_conact_state(memory)
	timestamp = datetime.now().astimezone().isoformat(timespec="seconds")
	record = {
		"timestamp": timestamp,
		"goal": goal.goal_id,
		"attempt": attempt,
		"phase": phase,
		"exit_code": exit_code,
		"observation": clip_text(output, 520) if output else "(no output captured)",
		"intent": hypothesis,
		"result": failure_class,
		"log": log_path,
	}
	recent = conact.setdefault("recent_step_record", [])
	if not isinstance(recent, list):
		recent = []
		conact["recent_step_record"] = recent
	recent.append(record)
	del recent[:-CONACT_RECENT_MAX]

	history = conact.setdefault("folded_action_history", [])
	if not isinstance(history, list):
		history = []
		conact["folded_action_history"] = history
	history.append({
		"span": f"{goal.goal_id} attempt {attempt} {phase}",
		"summary": f"{phase} exit {exit_code}; {failure_class}; {clip_text(hypothesis, 180)}",
		"log": log_path,
		"updated_at": timestamp,
	})
	conact_dedupe_history(history)
	del history[:-CONACT_HISTORY_MAX]

	for fact in conact_extract_facts(goal, phase, exit_code, output, log_path):
		conact_upsert_fact(conact, fact)


def conact_summary(memory: dict[str, object], goal: Goal) -> str:
	conact = ensure_conact_state(memory)
	lines = [
		"ConAct-style compact context:",
		"Folded action history:",
	]
	history = conact.get("folded_action_history", [])
	if isinstance(history, list) and history:
		for item in history[-4:]:
			if isinstance(item, dict):
				lines.append(f"- {item.get('span')}: {item.get('summary')}")
	else:
		lines.append("- No folded action history yet.")

	lines.append("Folded port state:")
	facts = conact.get("folded_port_state", [])
	relevant: list[dict[str, object]] = []
	if isinstance(facts, list):
		for item in facts:
			if not isinstance(item, dict):
				continue
			if item.get("goal") in {goal.goal_id, "G35"} or str(item.get("id", "")).startswith(("runtime.", "automation.")):
				relevant.append(item)
	if relevant:
		for item in relevant[-8:]:
			lines.append(
				f"- {item.get('id')}: {item.get('description')} — {item.get('content')}"
			)
	else:
		lines.append("- No persistent port facts recorded yet.")

	lines.append("Recent step record:")
	recent = conact.get("recent_step_record", [])
	if isinstance(recent, list) and recent:
		for item in recent[-2:]:
			if isinstance(item, dict):
				lines.append(
					f"- {item.get('goal')} {item.get('phase')} exit {item.get('exit_code')}: "
					f"{item.get('result')} / {item.get('intent')}"
				)
	else:
		lines.append("- No recent step records yet.")
	return "\n".join(lines)


def seed_conact_from_goal_state(memory: dict[str, object], goals: list[Goal]) -> None:
	"""Persist durable facts from the ledger when run-local memory is empty."""
	conact = ensure_conact_state(memory)
	complete = {goal.goal_id for goal in goals if goal.complete}
	if "G35" in complete:
		conact_upsert_fact(conact, conact_fact(
			"runtime.map_ready.c0a0e",
			"c0a0e reaches MAP_READY",
			"Goal ledger marks G35 complete; do not reopen c0a0e map lookup unless a newer probe regresses below MAP_READY.",
			"G35", ".ai/goals/GAMECUBE_PORT_GOALS.md"))
	if any(goal.goal_id == "G36" and not goal.automatic_done for goal in goals):
		conact_upsert_fact(conact, conact_fact(
			"runtime.frame_budget.g36_focus",
			"G36 should prioritize source-level frame/visual fixes",
			"Probe instrumentation is already extensive; editable context should prefer renderer/client/model source before scripts/dolphin-boot-probe.sh.",
			"G36", ".ai/goals/GAMECUBE_PORT_GOALS.md"))


def conact_content(memory: dict[str, object]) -> str:
	conact = ensure_conact_state(memory)
	parts: list[str] = []
	for key in ("folded_port_state", "folded_action_history", "recent_step_record"):
		value = conact.get(key, [])
		if isinstance(value, list):
			parts.extend(json.dumps(item, sort_keys=True) for item in value[-CONACT_FACT_MAX:])
	return "\n".join(parts).lower()


def conact_blocked_context_paths(goal_id: str, memory: dict[str, object]) -> set[str]:
	"""Return editable paths that folded memory says are counterproductive now."""
	blocked: set[str] = set()
	content = conact_content(memory)
	if goal_id == "G36" and not os.environ.get("AI_G36_ALLOW_PROBE_CONTEXT"):
		blocked.add("scripts/dolphin-boot-probe.sh")
	if goal_id == "G36" and "probe instrumentation is already extensive" in content:
		blocked.add("scripts/dolphin-boot-probe.sh")
	if "context budget pressure" in content and goal_id in {"G36", "G35", "G19"}:
		blocked.add("scripts/dolphin-boot-probe.sh")
		blocked.add("docs/GAMECUBE_PORT_PLAN.md")
	return blocked


def runtime_evidence_section(root: Path, *, lines: int = 40) -> str:
	"""Compact Dolphin/runtime evidence for runtime-first task prompts."""
	parts: list[str] = []
	latest = root / ".ai/state/dolphin-harness-latest.md"
	if latest.is_file():
		text = clip_text(latest.read_text(encoding="utf-8", errors="replace"), 2400)
		if text.strip():
			parts.append("Dolphin harness summary:")
			parts.append(text)
	memory = root / ".ai/state/dolphin-harness-memory.json"
	if memory.is_file():
		try:
			data = json.loads(memory.read_text(encoding="utf-8"))
			runs = data.get("runs", [])
			if isinstance(runs, list) and runs:
				run = runs[0] if isinstance(runs[0], dict) else {}
				summary = run.get("summary") or run.get("result") or run.get("status")
				log_dir = run.get("log_dir") or run.get("logs")
				if summary or log_dir:
					parts.append(
						f"Latest harness run: summary={summary or '(unknown)'} logs={log_dir or '(unknown)'}"
					)
		except (OSError, json.JSONDecodeError, TypeError):
			pass
	log_root = root / ".ai/logs"
	if log_root.is_dir():
		probe_dirs = sorted(log_root.glob("dolphin-probe-*"),
			key=lambda path: path.stat().st_mtime if path.exists() else 0)
		if probe_dirs:
			stderr = probe_dirs[-1] / "stderr.log"
			if stderr.is_file():
				tail = stderr.read_text(encoding="utf-8", errors="replace").splitlines()
				if tail:
					parts.append(f"Latest probe log ({stderr.relative_to(root)}) tail:")
					parts.extend(tail[-lines:])
	if not parts:
		return "No Dolphin runtime evidence captured yet."
	return "\n".join(parts)


def fault_evidence(output: str, *, max_items: int = 8) -> list[dict[str, object]]:
	evidence: list[dict[str, object]] = []
	lines = output.splitlines()
	for pattern in FAULT_PATTERNS:
		regex = re.compile(pattern, re.IGNORECASE)
		for index, line in enumerate(lines, start=1):
			if regex.search(line):
				evidence.append({
					"pattern": pattern,
					"line": index,
					"text": clip_text(line),
				})
				break
		if len(evidence) >= max_items:
			break
	return evidence


def hypothesis_for(exit_code: int, evidence: list[dict[str, object]], phase: str) -> str:
	joined = " ".join(str(item.get("text", "")) for item in evidence).lower()
	if exit_code == 0:
		return f"{phase} completed; preserve this evidence when deciding completion."
	if "token" in joined or "context" in joined:
		return "Model context or output budget constrained the previous attempt."
	if "aider made no edit" in joined:
		return "The model did not find or emit an applicable patch."
	if "could not load" in joined or "missing" in joined:
		return "The next blocker is likely asset lookup, staging, or path handling."
	if "_mem_alloc" in joined:
		return "The next blocker is likely memory pressure or an oversized cache/allocation."
	if "host_error" in joined or "sys_error" in joined or "fatal" in joined:
		return "The next blocker is a guest fatal path that needs the nearest log context."
	if "black screen" in joined or "diagnostic marker" in joined:
		return "The next blocker is likely visual output evidence or renderer presentation."
	if "audio" in joined:
		return "The next blocker is likely audio initialization, mixing, or output evidence."
	return f"{phase} exited {exit_code}; inspect the cited evidence before retrying."


def investigative_gap(evidence: list[dict[str, object]], phase: str) -> str:
	if evidence:
		first = str(evidence[0].get("text", ""))
		return f"Read around the first `{phase}` evidence line: {clip_text(first, 160)}"
	return f"Find the first decisive error or missing evidence from the `{phase}` trace."


def failure_class_for(exit_code: int, evidence: list[dict[str, object]], phase: str) -> str:
	joined = " ".join(str(item.get("text", "")) for item in evidence).lower()
	if exit_code == 0:
		return "accepted"
	if "token" in joined or "context" in joined:
		return "model_budget"
	if "aider made no edit" in joined:
		return "no_edit"
	if "verify:" in joined or "build failed" in joined:
		return "verification"
	if "could not load" in joined or "missing" in joined:
		return "asset_lookup"
	if "_mem_alloc" in joined:
		return "memory_pressure"
	if "black screen" in joined or "diagnostic marker" in joined:
		return "visual_runtime"
	if "audio" in joined:
		return "audio_runtime"
	if phase == "dolphin-probe":
		return "runtime_probe"
	return "unknown"


def g24_subgoal_for_attempt(attempt: int) -> dict[str, object]:
	return G24_SUBGOALS[(max(1, attempt) - 1) % len(G24_SUBGOALS)]


def g24_subgoal_id(attempt: int) -> str:
	return str(g24_subgoal_for_attempt(attempt)["id"])


def g24_subgoal_files(attempt: int, root: Path) -> list[str]:
	files = g24_subgoal_for_attempt(attempt).get("files", ())
	return [f"required:{path}" for path in files
		if isinstance(path, str) and (root / path).is_file()]


def g24_subgoal_memory(room: dict[str, object], subgoal_id: str) -> dict[str, object]:
	subgoals = room.setdefault("subgoals", {})
	if not isinstance(subgoals, dict):
		subgoals = {}
		room["subgoals"] = subgoals
	subgoal = subgoals.setdefault(subgoal_id, {
		"attempts": 0,
		"last_failure_class": None,
		"fix_classes_tried": [],
		"last_log": None,
	})
	if not isinstance(subgoal, dict):
		subgoal = {"attempts": 0, "last_failure_class": None,
			"fix_classes_tried": [], "last_log": None}
		subgoals[subgoal_id] = subgoal
	subgoal.setdefault("fix_classes_tried", [])
	return subgoal


def recent_log_text(root: Path, pattern: str = "aider-pass-*.log", *,
	max_chars: int = 12000) -> tuple[str, str | None]:
	logs = sorted((root / ".ai/logs").glob(pattern), key=lambda path: path.stat().st_mtime)
	if not logs:
		return "", None
	path = logs[-1]
	text = path.read_text(encoding="utf-8", errors="replace")
	return text[-max_chars:], str(path.relative_to(root))


def record_investigation(memory: dict[str, object], goal: Goal, *, attempt: int,
	phase: str, exit_code: int, output: str = "", log_path: str | None = None) -> None:
	evidence = fault_evidence(output)
	failure_class = failure_class_for(exit_code, evidence, phase)
	subgoal_id = g24_subgoal_id(attempt) if goal.goal_id == "G24" else None
	room = memory_room(memory, goal)
	drawers = room.setdefault("drawers", [])
	if not isinstance(drawers, list):
		drawers = []
		room["drawers"] = drawers
	entry = {
		"timestamp": datetime.now().astimezone().isoformat(timespec="seconds"),
		"attempt": attempt,
		"phase": phase,
		"exit_code": exit_code,
		"log": log_path,
		"subgoal": subgoal_id,
		"failure_class": failure_class,
		"hypothesis": hypothesis_for(exit_code, evidence, phase),
		"evidence": evidence,
		"gap": investigative_gap(evidence, phase),
	}
	drawers.append(entry)
	del drawers[:-MEMORY_MAX_DRAWERS]
	conact_record_step(memory, goal, attempt=attempt, phase=phase,
		exit_code=exit_code, failure_class=failure_class,
		hypothesis=str(entry["hypothesis"]), output=output,
		log_path=log_path)

	if subgoal_id:
		subgoal = g24_subgoal_memory(room, subgoal_id)
		subgoal["attempts"] = int(subgoal.get("attempts", 0) or 0) + 1
		subgoal["last_failure_class"] = failure_class
		subgoal["last_log"] = log_path
		fix_classes = subgoal.setdefault("fix_classes_tried", [])
		if isinstance(fix_classes, list) and failure_class not in {"accepted", "unknown"}:
			fix_classes.append(failure_class)
			del fix_classes[:-MEMORY_MAX_DRAWERS]

	gaps = room.setdefault("investigative_gaps", [])
	if isinstance(gaps, list):
		gaps.append(entry["gap"])
		del gaps[:-MEMORY_MAX_DRAWERS]

	tool_calls = memory.setdefault("past_tool_calls", [])
	if isinstance(tool_calls, list):
		tool_calls.append({
			"timestamp": entry["timestamp"],
			"goal": goal.goal_id,
			"subgoal": subgoal_id,
			"phase": phase,
			"exit_code": exit_code,
			"log": log_path,
		})
		del tool_calls[:-MEMORY_MAX_TOOL_CALLS]


def memory_summary(memory: dict[str, object], goal: Goal, attempt: int | None = None) -> str:
	room = memory_room(memory, goal)
	drawers = room.get("drawers", [])
	tool_calls = memory.get("past_tool_calls", [])
	lines = [
		f"Underlying task goal: {memory.get('task_goal', '')}",
		conact_summary(memory, goal),
		"",
		"Goal hypotheses and evidence:",
	]
	if isinstance(drawers, list) and drawers:
		for entry in drawers[-4:]:
			evidence = entry.get("evidence", []) if isinstance(entry, dict) else []
			if isinstance(evidence, list) and evidence:
				ev = "; ".join(clip_text(str(item.get("text", "")), 120)
					for item in evidence[:3] if isinstance(item, dict))
			else:
				ev = "(no decisive evidence captured yet)"
			lines.append(
				f"- attempt {entry.get('attempt')} {entry.get('phase')} exit {entry.get('exit_code')}: "
				f"{entry.get('hypothesis')} "
				f"{'Subgoal: ' + str(entry.get('subgoal')) + '. ' if entry.get('subgoal') else ''}"
				f"Failure class: {entry.get('failure_class', 'unknown')}. Evidence: {ev}"
			)
	else:
		lines.append("- No prior dynamic memory for this goal.")
	if goal.goal_id == "G24":
		subgoals = room.get("subgoals", {})
		active_id = g24_subgoal_id(attempt or 1)
		lines.append("G24 subgoal memory:")
		if isinstance(subgoals, dict) and subgoals:
			for subgoal in G24_SUBGOALS:
				subgoal_id = str(subgoal["id"])
				state = subgoals.get(subgoal_id, {})
				if isinstance(state, dict):
					fixes = state.get("fix_classes_tried", [])
					fix_text = ", ".join(str(item) for item in fixes[-4:]) \
						if isinstance(fixes, list) and fixes else "none"
					lines.append(
						f"- {subgoal_id} {subgoal['title']}: attempts={state.get('attempts', 0)} "
						f"last={state.get('last_failure_class', 'none')} tried={fix_text}"
					)
		else:
			lines.append("- No per-subgoal memory recorded yet.")
		lines.append(f"Next selected G24 subgoal may rotate from prior attempts; current memory anchor: {active_id}.")
	gaps = room.get("investigative_gaps", [])
	if isinstance(gaps, list) and gaps:
		lines.append("Investigative gaps:")
		for gap in gaps[-3:]:
			lines.append(f"- {gap}")
	if isinstance(tool_calls, list) and tool_calls:
		lines.append("Recent investigation tool calls:")
		for call in tool_calls[-5:]:
			if isinstance(call, dict):
				lines.append(
					f"- {call.get('goal')}{'/' + str(call.get('subgoal')) if call.get('subgoal') else ''} "
					f"{call.get('phase')} exit {call.get('exit_code')}"
					f"{' -> ' + call['log'] if call.get('log') else ''}"
				)
	return "\n".join(lines)


def insert_goal_evidence(text: str, goal_id: str, evidence_lines: list[str]) -> str:
	header_re = re.compile(rf"^(## {re.escape(goal_id)} \[)( \])", re.MULTILINE)
	match = header_re.search(text)
	if not match:
		return text
	text = header_re.sub(r"\1x]", text, count=1)
	insert_at = match.end()
	next_header = re.search(r"^## G\d+", text[insert_at:], re.MULTILINE)
	section_end = insert_at + next_header.start() if next_header else len(text)
	stamp = datetime.now().astimezone().strftime("%Y-%m-%d")
	block = "".join(f"\n- Verified {stamp}: {line}" for line in evidence_lines)
	return text[:section_end].rstrip() + block + "\n" + text[section_end:]


def commit_probe_success(root: Path, goal: Goal, log_dir: str, probe_output: str) -> int:
	goal_path = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"
	plan_path = root / "docs/GAMECUBE_PORT_PLAN.md"
	timeout = os.environ.get("DOLPHIN_TIMEOUT", "120")
	summary = probe_output.splitlines()[0] if probe_output else "MAP_READY"
	goal_path.write_text(insert_goal_evidence(
		goal_path.read_text(encoding="utf-8"),
		goal.goal_id,
		[
			f"`DOLPHIN_TIMEOUT={timeout} scripts/dolphin-boot-probe.sh`",
			f"Result: {summary}",
			f"Evidence: `{log_dir}/stderr.log`",
		],
	), encoding="utf-8")
	plan_note = (
		f"\n\n### {goal.goal_id} probe pass "
		f"({datetime.now().astimezone().strftime('%Y-%m-%d')})\n"
		f"- Command: `DOLPHIN_TIMEOUT={timeout} scripts/dolphin-boot-probe.sh`\n"
		f"- Result: `{summary}`\n"
		f"- Logs: `{log_dir}/stderr.log`\n"
	)
	plan_path.write_text(plan_path.read_text(encoding="utf-8").rstrip() + plan_note + "\n",
		encoding="utf-8")
	subject = GOAL_COMMIT_SUBJECT.get(goal.goal_id,
		f"test: complete GameCube goal {goal.goal_id}")
	add = run(["git", "add", str(goal_path.relative_to(root)),
		str(plan_path.relative_to(root))], root)
	if add.returncode != 0:
		return add.returncode
	body = "\n".join((
		f"Goal: {goal.goal_id} - {goal.title}",
		"Type: Dolphin probe acceptance",
		"",
		"Evidence:",
		f"- Command: DOLPHIN_TIMEOUT={timeout} scripts/dolphin-boot-probe.sh",
		f"- Result: {summary}",
		f"- Logs: {log_dir}/stderr.log",
		"",
		"Updated the goal ledger and port plan with the probe result before "
		"marking the goal complete.",
	))
	return commit_with_body(root, subject, body)


def task_for(goal: Goal, root: Path, attempt: int, investigation_memory: str,
	probe_result: tuple[int, str, str | None] | None = None) -> str:
	retry_instruction = ""
	if attempt > 1:
		retry_instruction = (
			"Previous attempt did not produce an accepted commit. Make a concrete "
			"smallest safe patch; do not ask for context.\n\n"
		)
	if attempt > 2:
		retry_instruction = (
			"Previous attempts hit an automation recovery path. Keep this pass surgical: "
			"prefer one source file plus the ledger/plan, avoid broad rewrites, and keep "
			"the response short enough to fit a reduced output budget. If the source file "
			"needed for the real fix is not loaded, update the goal ledger and port plan "
			"with the exact next file or blocker instead of stopping.\n\n"
		)
	probe_section = ""
	if probe_result is not None:
		exit_code, output, log_dir = probe_result
		probe_section = f"""
Dolphin boot probe (just executed):
- Exit code: {exit_code}
- Summary: {output.splitlines()[0] if output else '(empty)'}
- Logs: {log_dir or '(unknown)'}

Last stderr lines:
{probe_log_tail(root, log_dir)}

The probe did not satisfy `{goal.goal_id}`. Fix the guest-engine or probe
blocker shown above. Do not mark `{goal.goal_id}` complete and do not write
docs-only status updates when the probe still fails.
"""
	elif goal.goal_id in DOLPHIN_PROBE_GOALS:
		probe_section = (
			f"\nThis goal requires Dolphin runtime evidence. The goal runner executes "
			f"`scripts/dolphin-boot-probe.sh` before each pass; do not claim completion "
			f"without a successful probe result in the ledger.\n"
		)
	runtime_section = ""
	if goal.goal_id in DOLPHIN_PROBE_GOALS or goal.goal_id in {"G36", "G49"}:
		runtime_section = f"""
Runtime evidence (prefer this over git history):
{runtime_evidence_section(root)}
"""
	repo_section = ""
	if attempt <= 1 and goal.goal_id not in DOLPHIN_PROBE_GOALS:
		repo_section = f"""
Repository context:
{git_context(root)}
"""
	elif attempt <= 1:
		repo_section = f"""
Repository context (abbreviated):
{clip_text(git_context(root), 1200)}
"""
	goal_body = goal.body
	if goal.goal_id == "G24":
		subgoal = g24_subgoal_for_attempt(attempt)
		subgoal_files = ", ".join(str(path) for path in subgoal.get("files", ()))
		return f"""Advance G24 with exactly one small source edit.

Active goal: {goal.goal_id} - {goal.title}
Active subgoal: {subgoal["id"]} - {subgoal["title"]}
Attempt on this goal: {attempt}

Use only the editable file or files preloaded in this Aider chat. Do not edit
any file that was not added as editable context.

Task:
- Work this subgoal only: {subgoal["focus"]}
- Editable files for this subgoal: {subgoal_files}
- Wire the current renderer slice into `GC_GetVisualQuality()` when that can be
  done safely from the loaded source.
- `ref/gx/r_local.h` already provides the renderer-local
  `GC_GetVisualQuality( void )` helper when that header is included.
- Do not add another `extern` declaration or another implementation of
  `GC_GetVisualQuality`.
- Quality 0 is the low-memory smoke path.
- Quality 1/2 should preserve existing higher-quality behavior.
- Prefer a tiny helper or guard over broad rendering rewrites.

Structured failure memory:
{investigation_memory}

Output rules:
- Start immediately with the target source file path and SEARCH/REPLACE blocks.
- No explanation, checklist, plan, or markdown prose.
- Touch one loaded source file only.
- If the current slice is not sufficient for a safe source edit, make no edit;
  the goal runner will rotate to the next slice.
"""
	if goal.goal_id == "G36":
		return f"""Advance G36 with exactly one small source-level performance or visual-evidence patch.

Active goal: {goal.goal_id} - {goal.title}
Attempt on this goal: {attempt}

Use only the editable file or files preloaded in this Aider chat. Do not edit,
add, or request any file that was not added as editable context.

Task:
- Prefer source-level work over more probe-script instrumentation. G36 already
  has enough probe diagnostics to identify missing frame-budget or visual
  markers.
- If a renderer/client/model source file is editable, make exactly one small
  GameCube-only source change tied to frame budget, frame submission, visual
  evidence, or reducing route-time render cost.
- If `scripts/dolphin-boot-probe.sh` is the editable file, improve only a
  missing acceptance gate or remove duplicated/noisy G36 diagnostics. Do not add
  another one-off `G36_PATCH_v*` detector unless it replaces older duplicated
  logic.
- Do not update docs unless a docs file is explicitly editable in this chat.
- Do not mark G36 complete from reasoning alone; completion needs before/after
  runtime evidence from a Dolphin probe or hardware run.
- Treat G35 `MAP_READY` as already proven. Do not reopen the old
  `maps/c0a0e.bsp` lookup hypothesis unless this pass has newer contrary logs.

Structured failure memory:
{investigation_memory}

Output rules:
- Start immediately with the target editable file path and SEARCH/REPLACE blocks.
- No explanation, checklist, plan, or markdown prose.
- Touch one loaded file only.
- Keep the patch below 120 changed lines.
- If the current slice is not sufficient for a safe patch, make no edit; the
  goal runner will rotate to the next slice.
"""
	return f"""You are autonomously advancing the native Xash3D GameCube port.

Active goal: {goal.goal_id} — {goal.title}
Attempt on this goal: {attempt}

{retry_instruction}{probe_section}{runtime_section}Acceptance criteria:
{goal_body}

{repo_section}Automation environment:
{automation_context()}

Investigation memory:
{investigation_memory}

Make one coherent patch using the preloaded files. Preserve non-GameCube
targets. Do not ask questions, propose commands, or stop at a plan. If the
premise is disproven, update the goal ledger and port plan with the blocker
instead of forcing an engine change.
Do not narrate your investigation. Emit only the Aider edit blocks needed for
the patch. Keep the reply under one file when possible and avoid repeating the
same SEARCH/REPLACE block.

Rules:
- Keep the commit below 400 changed lines and do not delete tracked files.
- Update `docs/GAMECUBE_PORT_PLAN.md` with commands and concrete evidence when
  it is loaded in the editable context.
- If marking `{goal.goal_id}` done, include a command, result, and log path or
  runtime artifact in the goal notes and port plan; docs-only reasoning is not
  enough for completion.
- Update this goal's notes when useful.
- Mark `{goal.goal_id}` done only when every acceptance criterion is demonstrated.
  Otherwise leave it unchecked and state the next blocker in the port plan.
- Never mark MANUAL goals complete.
- Use the investigation memory as prior evidence, but verify stale hypotheses
  against current source, logs, or artifacts before acting on them.
- Stop after this coherent patch; the goal runner decides what comes next.
"""


def write_state(path: Path, **values: object) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	values["updated_at"] = datetime.now().astimezone().isoformat(timespec="seconds")
	path.write_text(json.dumps(values, indent=2) + "\n", encoding="utf-8")


def git_head(root: Path) -> str:
	return subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
		text=True, capture_output=True, check=False).stdout.strip()


def git_dirty(root: Path) -> bool:
	return bool(subprocess.run(["git", "status", "--porcelain"], cwd=root,
		text=True, capture_output=True, check=False).stdout.strip())


def clean_commit_advances_goal(root: Path, before: str, after: str,
	expected_subject: str, docs_required: bool = True) -> bool:
	if not before or not after or before == after or git_dirty(root):
		return False
	subject = subprocess.run(["git", "log", "-1", "--format=%s", after], cwd=root,
		text=True, capture_output=True, check=False).stdout.strip()
	if subject != expected_subject:
		return False
	changed = subprocess.run(["git", "diff", "--name-only", f"{before}..{after}"],
		cwd=root, text=True, capture_output=True, check=False).stdout.splitlines()
	if docs_required:
		return "docs/GAMECUBE_PORT_PLAN.md" in changed
	return bool(changed)


def goal_commit_subject(goal: Goal, active_subgoal: dict[str, object] | None) -> str:
	if goal.goal_id == "G24" and active_subgoal:
		subjects = {
			"G24a": "feat: wire GameCube renderer quality entrypoint",
			"G24b": "feat: bound GameCube surface cache drawing",
			"G24c": "feat: stabilize GameCube sprite visuals",
			"G24d": "feat: reduce GameCube texture pressure",
			"G24e": "feat: simplify GameCube renderer quality helpers",
		}
		return subjects.get(str(active_subgoal["id"]), "feat: stabilize GameCube visuals")
	return GOAL_COMMIT_SUBJECT.get(goal.goal_id,
		f"feat: advance GameCube port goal {goal.goal_id}")


def goal_commit_body(goal: Goal, *, attempt: int, context_files: list[str],
	read_context_files: list[str], active_subgoal: dict[str, object] | None,
	expected_subject: str, docs_required: str,
	blocked_context: set[str] | None = None) -> str:
	context = ", ".join(item.replace("required:", "").replace("read:", "")
		for item in context_files)
	read_context = ", ".join(item.replace("read:", "") for item in read_context_files)
	lines = [
		f"Goal: {goal.goal_id} attempt {attempt}",
	]
	if active_subgoal:
		lines.append(f"Subgoal: {active_subgoal['id']} - {active_subgoal['title']}")
	lines.extend((
		"",
		f"Editable: {context or '(none)'}",
		f"Read-only: {read_context or '(none)'}",
		f"Docs required: {docs_required}",
		f"ConAct blocked context: {', '.join(sorted(blocked_context or ())) or '(none)'}",
		"",
		"Verified by ai-aider-pass pre-commit and post-commit checks.",
	))
	return "\n".join(lines)


def dirty_commit_subject(goal_id: str | None = None) -> str:
	if goal_id:
		return f"chore: checkpoint automation state before {goal_id}"
	return "chore: checkpoint dirty automation state"


def commit_dirty_worktree(root: Path, goal_id: str | None = None) -> int:
	if not git_dirty(root):
		return 0
	subject = dirty_commit_subject(goal_id)
	print(f"goal-loop: dirty worktree detected; creating checkpoint commit: {subject}",
		file=sys.stderr, flush=True)
	add = run(["git", "add", "-A"], root)
	if add.returncode != 0:
		return add.returncode
	body = (
		"Checkpoint uncommitted work before automated goal execution.\n\n"
		f"Goal before checkpoint: {goal_id or '(startup)'}\n"
		"The checkpoint preserves user and generated changes so the automation "
		"can operate from a clean index."
	)
	return commit_with_body(root, subject, body)


def context_for_goal(goal_id: str, root: Path, attempt: int,
	memory: dict[str, object] | None = None) -> list[str]:
	"""Return a progressively smaller editable context for recovery retries."""
	if goal_id == "G24":
		return g24_subgoal_files(attempt, root)
	blocked_paths = conact_blocked_context_paths(goal_id, memory or {})
	if goal_id in GOAL_CONTEXT_SLICES:
		slices = GOAL_CONTEXT_SLICES[goal_id]
		candidate_slices = slices
		if blocked_paths:
			candidate_slices = tuple(
				tuple(path for path in paths if path not in blocked_paths)
				for paths in slices
			)
			candidate_slices = tuple(paths for paths in candidate_slices if paths)
		if not candidate_slices:
			candidate_slices = slices
		paths = candidate_slices[(max(1, attempt) - 1) % len(candidate_slices)]
		return [f"required:{path}" for path in paths if (root / path).is_file()]

	candidates: list[str] = []
	seen: set[str] = set()
	required_goal = set(GOAL_REQUIRED_CONTEXT.get(goal_id, ()))
	for path in (*COMMON_CONTEXT, *GOAL_CONTEXT.get(goal_id, ())):
		if path in blocked_paths:
			continue
		if path in seen or not (root / path).is_file():
			continue
		seen.add(path)
		candidates.append(path)
	size_limit = (
		45000 if attempt == 1 else
		20000 if attempt == 2 else
		12000 if attempt == 3 else
		8000
	)
	required = set(COMMON_CONTEXT if attempt <= 2 else (".ai/goals/GAMECUBE_PORT_GOALS.md",))
	selected: list[str] = []
	for path in candidates:
		file_path = root / path
		if path in required_goal:
			selected.append(f"required:{path}")
		elif path in required or file_path.stat().st_size <= size_limit:
			selected.append(path)
	if not selected:
		return candidates[:1]
	return selected


def read_context_for_goal(goal_id: str, root: Path, attempt: int = 1) -> list[str]:
	"""Return focused read-only notes for the active goal."""
	if os.environ.get("AIDER_AUTOMATION", "1") == "1" and attempt >= 3:
		return []
	common_reads = () if os.environ.get("AIDER_AUTOMATION", "1") == "1" else COMMON_READ_CONTEXT
	seen: set[str] = set()
	selected: list[str] = []
	for path in (*common_reads, *GOAL_READ_CONTEXT.get(goal_id, ())):
		if path in seen or not (root / path).is_file():
			continue
		seen.add(path)
		selected.append(f"read:{path}")
	return selected


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--max-passes", type=int, default=0,
		help="maximum passes to run; 0 means unlimited")
	parser.add_argument("--recoverable-retries", type=int, default=8,
		help="retry one goal this many times for token/timeout/no-edit failures")
	parser.add_argument("--max-attempts-per-goal", type=int,
		default=int(os.environ.get("AI_MAX_ATTEMPTS_PER_GOAL", "3")),
		help="stop G36+ automatic goals after this many attempts for review; 0 disables")
	parser.add_argument("--list", action="store_true", help="print goal state and exit")
	parser.add_argument("--status-json", action="store_true", help="emit machine-readable goal state")
	args = parser.parse_args()
	root = args.repo.expanduser().resolve()
	goal_file = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"
	state_file = root / ".ai/logs/goal-loop-state.json"
	memory = load_loop_memory(root)
	interrupted_signal = 0
	load_dotenv(root / ".env")

	def stop_cleanly(signum: int, _frame: object) -> None:
		nonlocal interrupted_signal
		interrupted_signal = signum
		write_state(state_file, state="stopped", signal=signum,
			message="Automation stopped by operator")
		print("\nGoal automation stopped by operator.", file=sys.stderr, flush=True)
		raise KeyboardInterrupt

	signal.signal(signal.SIGTERM, stop_cleanly)
	signal.signal(signal.SIGINT, stop_cleanly)
	if not goal_file.is_file():
		parser.error(f"goal file not found: {goal_file}")

	goals = parse_goals(goal_file)
	seed_conact_from_goal_state(memory, goals)
	save_loop_memory(root, memory)
	if args.status_json:
		print(json.dumps([asdict(goal) | {"complete": goal.complete,
			"manual": goal.manual, "blocked": goal.blocked} for goal in goals]))
		return 0
	if args.list:
		for goal in goals:
			state = "manual" if goal.manual else "blocked" if goal.blocked \
				else "complete" if goal.complete else "pending"
			print(f"{goal.goal_id}\t{state}\t{goal.title}")
		return 0
	loop_lock = acquire_loop_lock(root)
	if fcntl is not None and loop_lock is None:
		write_state(state_file, state="blocked",
			message="another goal loop is already running")
		return 2
	if args.max_passes < 0:
		parser.error("--max-passes must be zero or positive")
	if commit_dirty_worktree(root) != 0:
		print("goal-loop: failed to checkpoint the dirty worktree", file=sys.stderr)
		return 2
	if not os.environ.get("OPENAI_API_KEY"):
		print("goal-loop: OPENAI_API_KEY must be supplied by the launch environment", file=sys.stderr)
		return 2
	api_base = os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")
	if not model_ready(api_base):
		print(f"goal-loop: model API is not reachable at {api_base}", file=sys.stderr)
		print("Start the Qwable/vLLM server first, then retry.", file=sys.stderr)
		return 2

	attempts: dict[str, int] = {}
	pass_indexes = count(1) if args.max_passes == 0 else range(1, args.max_passes + 1)
	for pass_index in pass_indexes:
		goals = parse_goals(goal_file)
		seed_conact_from_goal_state(memory, goals)
		goal = next((item for item in goals if not item.automatic_done), None)
		if goal is None:
			write_state(state_file, state="complete", pass_index=pass_index - 1,
				message="All automatic goals are complete or blocked")
			print("All automatic GameCube port goals are complete or blocked.")
			return 0
		attempts[goal.goal_id] = attempts.get(goal.goal_id, 0) + 1
		goal_number = int(goal.goal_id[1:]) if goal.goal_id[1:].isdigit() else 0
		if args.max_attempts_per_goal > 0 and goal_number >= 36 and \
				attempts[goal.goal_id] > args.max_attempts_per_goal:
			write_state(state_file, state="review-required", pass_index=pass_index,
				goal=asdict(goal), attempt=attempts[goal.goal_id],
				message="G36+ attempt limit reached; run RC gate and Codex/review LLM before more edits")
			print(
				f"{goal.goal_id}: reached {args.max_attempts_per_goal} automated attempts; "
				"stopping for RC gate and review.",
				file=sys.stderr,
			)
			return 3
		if commit_dirty_worktree(root, goal.goal_id) != 0:
			write_state(state_file, state="failed", pass_index=pass_index,
				goal=asdict(goal), attempt=attempts[goal.goal_id],
				message="failed to checkpoint dirty worktree")
			return 2
		active_subgoal = g24_subgoal_for_attempt(attempts[goal.goal_id]) \
			if goal.goal_id == "G24" else None
		pass_limit_label = UNLIMITED_PASSES_LABEL if args.max_passes == 0 else str(args.max_passes)
		print(f"\n{'=' * 72}\nGOAL PASS {pass_index}/{pass_limit_label}: "
			f"{goal.goal_id} — {goal.title}\n{'=' * 72}", flush=True)
		if active_subgoal:
			print(f"Active subgoal: {active_subgoal['id']} — {active_subgoal['title']}",
				flush=True)
		write_state(state_file, state="running", pass_index=pass_index,
			goal=asdict(goal), attempt=attempts[goal.goal_id],
			subgoal=active_subgoal)
		probe_result: tuple[int, str, str | None] | None = None
		if goal.goal_id in DOLPHIN_PROBE_GOALS:
			print(f"\n--- Dolphin boot probe for {goal.goal_id} ---", flush=True)
			probe = run_dolphin_probe(root)
			probe_output = ((probe.stdout or "") + (probe.stderr or "")).strip()
			log_dir = probe_log_dir(probe_output)
			print(probe_output, flush=True)
			success_marker = PROBE_AUTO_COMPLETE.get(goal.goal_id)
			if probe.returncode == 0 and success_marker and success_marker in probe_output:
				record_investigation(memory, goal, attempt=attempts[goal.goal_id],
					phase="dolphin-probe", exit_code=probe.returncode,
					output=probe_output, log_path=f"{log_dir}/stderr.log" if log_dir else None)
				save_loop_memory(root, memory)
				if commit_probe_success(root, goal, log_dir or "", probe_output) != 0:
					write_state(state_file, state="failed", pass_index=pass_index,
						goal=asdict(goal), message="probe succeeded but commit failed")
					return 1
				review = run(["scripts/ai-review.sh"], root)
				if review.returncode != 0:
					write_state(state_file, state="failed-review", pass_index=pass_index,
						goal=asdict(goal), exit_code=review.returncode)
					return review.returncode
				print(f"{goal.goal_id}: probe satisfied acceptance criteria; continuing.",
					flush=True)
				continue
			probe_result = (probe.returncode, probe_output, log_dir)
			record_investigation(memory, goal, attempt=attempts[goal.goal_id],
				phase="dolphin-probe", exit_code=probe.returncode,
				output=probe_output, log_path=f"{log_dir}/stderr.log" if log_dir else None)
			save_loop_memory(root, memory)
		with tempfile.NamedTemporaryFile("w", suffix=".md", prefix="xash3d-gc-goal-",
			encoding="utf-8", delete=False) as task:
			task.write(task_for(goal, root, attempts[goal.goal_id],
				memory_summary(memory, goal, attempts[goal.goal_id]), probe_result))
			task_path = Path(task.name)
		head_before = git_head(root)
		child_failure_output = ""
		try:
			blocked_context = conact_blocked_context_paths(goal.goal_id, memory)
			context_files = context_for_goal(goal.goal_id, root, attempts[goal.goal_id], memory)
			read_context_files = read_context_for_goal(goal.goal_id, root, attempts[goal.goal_id])
			pass_env = os.environ.copy()
			expected_subject = goal_commit_subject(goal, active_subgoal)
			pass_env["AI_GOAL_ID"] = goal.goal_id
			pass_env["AI_COMMIT_SUBJECT"] = expected_subject
			pass_env["AI_DIRTY_COMMIT_SUBJECT"] = dirty_commit_subject(goal.goal_id)
			pass_env["AIDER_BUDGET_ATTEMPT"] = str(attempts[goal.goal_id])
			pass_env.setdefault("AIDER_AUTOMATION", "1")
			if goal.goal_id == "G24":
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_INITIAL", "2048")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_1", "1536")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_2", "1024")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_3", "768")
				pass_env.setdefault("AIDER_MAX_CHAT_HISTORY_TOKENS", "256")
				pass_env.setdefault("AI_VERIFY_REQUIRE_DOC_UPDATE", "0")
				if active_subgoal:
					pass_env.setdefault("AI_G24_SUBGOAL", str(active_subgoal["id"]))
			if goal.goal_id == "G36":
				pass_env.setdefault("AI_VERIFY_REQUIRE_DOC_UPDATE", "0")
			pass_env["AI_COMMIT_BODY"] = goal_commit_body(goal,
				attempt=attempts[goal.goal_id],
				context_files=context_files,
				read_context_files=read_context_files,
				active_subgoal=active_subgoal,
				expected_subject=expected_subject,
				docs_required=pass_env.get("AI_VERIFY_REQUIRE_DOC_UPDATE", "1"),
				blocked_context=blocked_context)
			if attempts[goal.goal_id] >= 3:
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_INITIAL", "768")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_1", "512")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_2", "384")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_3", "256")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_INITIAL", "14000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_1", "9000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_2", "6000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_3", "4000")
				pass_env.setdefault("AIDER_MAX_CHAT_HISTORY_TOKENS", "512")
			preflight = harness_preflight(root)
			if preflight.returncode != 0:
				child_failure_output = ((preflight.stdout or "") + (preflight.stderr or "")).strip()
				print(child_failure_output, file=sys.stderr, flush=True)
				result = subprocess.CompletedProcess(preflight.args, 16,
					preflight.stdout, preflight.stderr)
			else:
				result = run(["scripts/ai-aider-pass.sh", str(root), str(task_path),
					*context_files, *read_context_files], root, env=pass_env)
		finally:
			task_path.unlink(missing_ok=True)
		if result.returncode != 0:
			log_tail, log_path = recent_log_text(root)
			record_investigation(memory, goal, attempt=attempts[goal.goal_id],
				phase="aider-pass", exit_code=result.returncode,
				output=child_failure_output or log_tail,
				log_path=None if child_failure_output else log_path)
			save_loop_memory(root, memory)
			head_after = git_head(root)
			docs_required = pass_env.get("AI_VERIFY_REQUIRE_DOC_UPDATE", "1") == "1"
			if clean_commit_advances_goal(root, head_before, head_after,
					expected_subject, docs_required):
				write_state(state_file, state="resuming-after-commit", pass_index=pass_index,
					goal=asdict(goal), attempt=attempts[goal.goal_id],
					exit_code=result.returncode,
					message="Child pass exited nonzero after creating a clean commit; reviewing and continuing")
				print("Child pass exited nonzero after a clean commit; reviewing and continuing.",
					file=sys.stderr)
				review = run(["scripts/ai-review.sh"], root)
				if review.returncode != 0:
					write_state(state_file, state="failed-review", pass_index=pass_index,
						goal=asdict(goal), exit_code=review.returncode)
					return review.returncode
				continue
			if head_after and head_after != head_before:
				write_state(state_file, state="recovering-after-unrelated-commit",
					pass_index=pass_index, goal=asdict(goal),
					attempt=attempts[goal.goal_id], exit_code=result.returncode,
					message="HEAD moved, but not with the expected goal commit; treating child failure as recoverable when possible")
				print("HEAD moved without an accepted goal commit; continuing recovery logic.",
					file=sys.stderr)
			if result.returncode in RECOVERABLE_EXIT_CODES and \
					attempts[goal.goal_id] <= args.recoverable_retries:
				reason = RECOVERABLE_EXIT_CODES[result.returncode]
				write_state(state_file, state="recovering", pass_index=pass_index,
					goal=asdict(goal), attempt=attempts[goal.goal_id],
					exit_code=result.returncode, message=f"{reason}; retrying goal")
				print(f"{reason}; retrying this goal with a tighter context.",
					file=sys.stderr)
				continue
			write_state(state_file, state="failed", pass_index=pass_index,
				goal=asdict(goal), exit_code=result.returncode)
			return result.returncode
		log_tail, log_path = recent_log_text(root)
		record_investigation(memory, goal, attempt=attempts[goal.goal_id],
			phase="aider-pass", exit_code=result.returncode,
			output=log_tail, log_path=log_path)
		save_loop_memory(root, memory)
		review = run(["scripts/ai-review.sh"], root)
		if review.returncode != 0:
			record_investigation(memory, goal, attempt=attempts[goal.goal_id],
				phase="review", exit_code=review.returncode,
				output=f"scripts/ai-review.sh exited {review.returncode}")
			save_loop_memory(root, memory)
			write_state(state_file, state="failed-review", pass_index=pass_index,
				goal=asdict(goal), exit_code=review.returncode)
			return review.returncode

	if args.max_passes > 0:
		write_state(state_file, state="pass-limit", pass_index=args.max_passes,
			message="Pass limit reached with automatic goals remaining")
		print("Goal pass limit reached; stopping for human review.", file=sys.stderr)
		return 3
	return 0


if __name__ == "__main__":
	try:
		raise SystemExit(main())
	except KeyboardInterrupt:
		raise SystemExit(130) from None
