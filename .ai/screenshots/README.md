# Screenshot Baselines

Use these baseline assets to keep Dolphin/GameCube rendering progress grounded
against repeatable visual checkpoints.

## Milestone manifest

Baseline definitions live in:

- `.ai/screenshots/baselines.json`

Each milestone records:

- `reference`: the reference image to compare against
- `candidate`: the latest GameCube/Dolphin image for that milestone
- `comparison`: where to write the side-by-side diff artifact

## Comparison command

Example:

```bash
python3 scripts/gamecube-screenshot-baseline.py \
  --milestone retail_main_menu \
  --result-json .ai/screenshots/comparisons/retail-main-menu.json
```

This writes:

- a side-by-side comparison image
- a compact JSON result with diff metrics and a coarse verdict

## Current milestones

- `retail_main_menu`
  - reference: `retail-main-menu-reference.png`
  - candidate: `gc-main-menu.png`
  - purpose: keep menu layout, readability, and overall presentation close to
    the retail baseline

- `world_present_demo`
  - reference: `demo-stages/stage-04-world-present.png`
  - candidate: `demo-stages/stage-04b-live-gx-present.png`
  - purpose: regression detection for the world-present renderer path even when
    an exact retail PC screenshot is not yet staged
