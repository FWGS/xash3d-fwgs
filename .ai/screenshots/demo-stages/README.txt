GameCube Dolphin stage demo screenshots (2026-07-19)

stage-00-intro-bald-scientist.png
  HL1 intro plaque: bald Black Mesa scientist (Walter) + HALF-LIFE title.

stage-00b-main-menu.png
  Half-Life main menu (retail menu path).

stage-01-live-dolphin-loading.png
  Live Dolphin DumpFrames of the loading UI (scientist + progress bar).
  G130 forces a CPU YUYV present from GC_DrawLoadingStatus so DumpFrames
  keep the plaque (GX tiled presents read as period-32 noise).

stage-01-map-load-please-wait.png / stage-02-loading-bsp.png / stage-03-loading-progress.png
  Host-rendered previews of the baked loading.tga + progress bar at ~85%/40%/75%.

stage-04-world-present.png
  Dolphin DumpFrames after G138: textured spans run live; dump rejects soft
  chroma (uniq≥48) and uses G136 zi near/wall/sky + WORLD PRESENT panel.
  Probe: .ai/logs/dolphin-probe-20260720-003435 (framedump_9, uniq≈62 pink=0).

Engine notes:
  Disc bake: resource/gc_menu/loading.tga + intro.tga
  Runtime: GC_DrawLoadingStatus + GC_SetLoadingProgress
  Dump path: R_GcmapShadeDumpFromDepth / GC_CoalesceDumpWorldBuffer + G128 CPU presents
