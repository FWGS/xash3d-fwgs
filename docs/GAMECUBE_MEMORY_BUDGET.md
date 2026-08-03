# GameCube Memory Evidence

This is an evidence report, not a hand-authored memory budget. Run
`python3 scripts/gamecube-memory-evidence.py --repo .` after a build and after
each representative map probe. The generated report is the source of truth;
missing measurements are reported as `UNAVAILABLE` and must not be replaced by
estimates.

## Memory model used by this project

The release model currently treats approximately **24 MiB of MEM1/main
memory** and **16 MiB of MEM2/auxiliary memory** as the relevant operating
assumptions. These are capacity assumptions, not allocations available to the
engine. The report must account for executable sections, runtime arenas,
stacks, libraries, caches, and temporary staging before claiming headroom.

No fixed pool split is authorized by this document. In particular, this report
does not assume a 32 MiB VRAM budget. GPU-visible memory, embedded framebuffers,
texture residency, and auxiliary-memory use must be identified from linker,
runtime, or hardware evidence separately.

## Required generated evidence

| Evidence | Source | Required result |
|---|---|---|
| ELF/DOL section sizes | ELF section table and DOL header | text/data/BSS and every DOL load section, in bytes and address range |
| Linker map | linker map emitted by the build | largest symbols/sections and placement in MEM1/MEM2; `UNAVAILABLE` if absent |
| MEM1/MEM2 high-water | Dolphin/hardware log telemetry | base, peak, free-at-peak, stage, map, and route; never infer MEM2 from MEM1 |
| Per-map peak usage | map compatibility/campaign/probe logs | one row per map and route, including failures |
| Texture/lightmap/audio/cache | tagged allocation telemetry or allocator report | subsystem, requested bytes, resident peak, and owner; `UNAVAILABLE` if untagged |
| Largest failed allocation | `mem FAIL`, allocator, or guest-fatal marker | requested bytes, subsystem, map, total/HWM, and source location |

## Current evidence state

Run the generator to refresh this section’s companion artifact:

```sh
python3 scripts/gamecube-memory-evidence.py \
  --repo . \
  --output .ai/state/gamecube-memory-evidence.json \
  --markdown .ai/state/gamecube-memory-evidence.md
```

The checked-in interpretation is intentionally conservative:

- MEM1 capacity: approximately 24 MiB project assumption; measured available
  capacity and free-at-peak: **UNAVAILABLE until a current runtime report is
  generated**.
- MEM2 capacity: approximately 16 MiB project assumption; measured use and
  high-water: **UNAVAILABLE unless explicitly emitted by the runtime**.
- ELF/DOL sections: **read from the current artifact by the generator**; a DOL
  file size is not a RAM-usage measurement.
- Linker map: **UNAVAILABLE when the build does not preserve one**. Add map
  output to the build before making placement or BSS claims.
- Per-map peaks: **only accepted from fresh logs** containing map and stage
  markers. Historical prose does not establish a current peak.
- Texture, lightmap, audio, and cache peaks: **UNAVAILABLE unless allocations
  carry subsystem tags**. Renderer source size or a guessed cache capacity is
  not residency evidence.
- Largest failed allocation: **taken from the largest parsed failure marker**;
  if no marker exists, it is `UNAVAILABLE`, not zero.

## Runtime markers currently consumed

The existing MEM1 telemetry is useful and is consumed without changing its
meaning:

```text
Xash3D GameCube: mem stage=<stage> total=<size> delta=<size> hwm=<size> map=<map>
Xash3D GameCube: map-load pressure stage=<stage> peak=<size> delta=<size> base=<size>
Xash3D GameCube: mem FAIL subsystem=<name> size=<size> map=<map> at=<file>:<line> total=<size> hwm=<size>
```

These lines describe the allocator/accounting path currently instrumented.
They do not prove that all allocations are in MEM1, do not expose MEM2 unless
the platform allocator reports it, and do not partition renderer/audio/cache
usage without subsystem tags.

## Acceptance rules

Memory pressure is a secondary hypothesis for the map failure, not a license to
hide the filesystem failure or skip `Delta_Init()`. A memory claim is accepted
only when it includes:

1. artifact identity and ELF/DOL section data;
2. linker-map evidence or an explicit missing-map result;
3. fresh runtime high-water data for the named map and route;
4. the largest failed request and owning subsystem, when a failure occurred;
5. separate MEM1 and MEM2 fields, with `UNAVAILABLE` where the runtime did not
   measure the region.

Until those fields exist, use the wording **“memory pressure is plausible but
unproven”** and continue debugging the first failing filesystem/map ladder
gate.
