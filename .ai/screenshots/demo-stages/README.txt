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
  Dolphin DumpFrames after G148: area-prioritized faces + 96px cache.
  Probe: .ai/logs/dolphin-probe-20260720-140641 (framedump_10).

stage-04b-live-gx-present.png
  Live GX present (framedump_15); uniq 2194→4054 vs G147.

stage-04c-outdoor-c1a0a.png
  Outdoor c1a0a (framedump_17); long dark runs 4→1 vs G147.

Engine notes:
  Disc bake: resource/gc_menu/loading.tga + intro.tga
  Runtime: GC_DrawLoadingStatus + GC_SetLoadingProgress
  Dump/live: G148 area-pri faces + 96px cache + G147 emit/scrub
