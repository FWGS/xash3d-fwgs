---
name: Visual glitches report
about: Something doesn't look right
title: ''
labels: bug, ray tracing, visual bug
assignees: ''

---

Note that:
- this is only for Vulkan/Ray tracing renderer. Prior to submitting anything here make sure that the game looks correct with native GL renderer (`-ref gl`).
- the renderer is WIP so there are way too many known visual bugs. Make sure to search issues first for it is very likely that we already know about it.
- Traditional rasterizer is not being actively maintained, so visual glitches in that won't be addressed for a while (unless they stall rt renderer progress).

**To reproduce**
1. Map name or attached save file
2. Steps to do (e.g. go to a specific room and perform some action)

**Screenshots**
1. The thing that looks wrong
2. How it's supposed to look, e.g.:
  - screenshot from the same angle made using vanilla GL renderer
  - screenshot from a production ready PBR/RT renderer of a similar scene with similar materials and lighting parameters.

**Moar context**
- Commit hash
- OS
- GPU vendor and model
- Driver version
