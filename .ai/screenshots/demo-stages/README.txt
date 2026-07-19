GameCube Dolphin stage demo screenshots (2026-07-19)

stage-00-intro-bald-scientist.png
  HL1 intro plaque: bald Black Mesa scientist (Walter) + HALF-LIFE title.

stage-00b-main-menu.png
  Half-Life main menu (retail menu path).

stage-01-live-dolphin-loading.png
  Live Dolphin DumpFrames of the new G60 loading UI (scientist + progress bar).

stage-01-map-load-please-wait.png / stage-02-loading-bsp.png / stage-03-loading-progress.png
  Host-rendered previews of the baked loading.tga + progress bar at ~85%/40%/75%.

Engine notes:
  Disc bake: resource/gc_menu/loading.tga + intro.tga
  Runtime: GC_DrawLoadingStatus + GC_SetLoadingProgress
  Probe: .ai/logs/dolphin-probe-20260719-011630 (HL1 loading plaque ready)
