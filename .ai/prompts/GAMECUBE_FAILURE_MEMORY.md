# GameCube Failure Memory

Known bad assumptions and corrections:

- Do not use proprietary Nintendo SDK headers, names, or APIs.
- Do not assume `OSReport` exists in this project; use existing libogc/project
  reporting paths such as `SYS_Report` where already established.
- Do not treat Dolphin as unavailable without checking `DOLPHIN_EXECUTABLE`.
  The automation may expose a native path or `flatpak:org.DolphinEmu.dolphin-emu`.
- Do not assume dynamic libraries are available on GameCube; use the static
  loader/export path already in the port.
- Do not write generated state to `gcdisc:/` or other read-only disc paths.
- Do not treat ARAM as ordinary heap memory.
- Do not call a goal complete from reasoning alone. Completion needs command,
  result, log path, or runtime artifact evidence.
- Do not load every large source file into Aider. Use focused files and
  subsystem notes selected by the goal runner.
- Do not hide blockers with smoke flags. Temporary shortcuts must become
  explicit GameCube modes or be removed when the subsystem stabilizes.
