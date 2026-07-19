GameCube Dolphin stage demo screenshots (2026-07-19)

stage-00-main-menu.png
  Half-Life main menu (retail menu path / baked GC menu assets).

stage-01-map-load-please-wait.png
  Live Dolphin DumpFrames: MAP LOAD / PLEASE WAIT - VIDEO ALIVE.

stage-02-loading-bsp.png / stage-03-loading-bsp-progress.png
  Live Dolphin DumpFrames: LOADING BSP during New Game / changelevel.

Gameplay (c1a0a after changelevel, G125 fire+steps) is proven in
.ai/logs/dolphin-probe-20260719-005629 OSREPORT (G125 preload, G122 fire,
ric1, ASND peak). Dolphin DumpFrames/XFB after the SW loading screens currently
captures EFB noise for this GX present path — window grabs match that noise.
