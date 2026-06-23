# GameCube Hardware Notes

Use this as compact hardware context for native Xash3D GameCube work.

## CPU And ABI

- CPU is IBM PowerPC Gekko: big-endian, 32-bit, alignment-sensitive.
- Avoid unaligned typed loads/stores. Use byte-safe reads or explicit endian
  helpers for file formats, network data, save data, and packed structures.
- Do not introduce x86 assumptions, JIT code, inline x86 asm, or little-endian
  binary shortcuts.

## Memory

- Main memory is 24 MiB MEM1. Treat this as the primary hard limit.
- ARAM is 16 MiB audio/auxiliary memory, not ordinary `malloc` space.
- Prefer bounded allocations, static pools, streaming, and low-memory modes.
- Record subsystem, size, and map context for allocation failures.

## Runtime Environment

- There is no general-purpose POSIX OS.
- Dynamic libraries are unavailable in the normal GameCube target; modules
  must be statically linked or exposed through the existing static loader path.
- Disc content is read-only. Route generated state to an explicit writable
  backend or skip it with a documented fallback.
- Keep offline boot independent of HTTP, TLS, master servers, and external
  network services.

## libogc Facilities

- Use libogc APIs through isolated GameCube platform code.
- Diagnostic output should use project-approved libogc reporting paths already
  present in the source, such as `SYS_Report` wrappers or existing console
  reporting helpers.
- Do not reference proprietary Nintendo SDK names or headers.

## Verification

- A clean build is necessary but not sufficient.
- Runtime claims need Dolphin logs, OSReport markers, map probe output, or
  hardware validation notes.
- Manual hardware goals cannot be completed by local automation alone.
