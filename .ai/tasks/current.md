All Xash3D GameCube port automatable goals (G83-G276) are complete.

Manual checkpoint:
G38 physical GameCube validation is pending operator execution. The hardware
handoff package is ready at `.ai/logs/hardware-handoff-20260725-183248/`.

Current state:
- All automatic goals completed (G83-G276)
- G38 manual validation pending (operator-only)
- Build verified: boot.dol (5,823,404 bytes), xash (33,122,656 bytes)
- SHA256: bdf3f7c5... (commit e5a483b773)
- Asset staging tools created (stage-sd-assets.sh, stage_sd_assets.py)
- Unified asset management system created (asset-manager.py)
- G410-G412 completed: Asset root discovery and SD staging
- G420 in progress: Added telemetry configuration options (gc_telemetry, gc_telemetry_format cvars)
- Telemetry infrastructure already exists in mem_gamecube.c/h
- BSP loading already implemented in GC_PrepareMapLoadBuffer functions

Next step:
- G421: BSP loading - Test and document BSP loading optimization
- G422: Server - Test server functionality and performance
- G423: Client - Test client functionality and performance
- G424: Playable frame - Integrate all components and test gameplay loop

Manual hardware validation with physical GameCube, Swiss loader, or compatible
Wii/GameCube-mode hardware. See `docs/HARDWARE_TESTING_GUIDE.md` for procedures.

Asset management:
- Use `scripts/asset-manager.py discover` to find asset roots
- Use `scripts/asset-manager.py stage --source <path> --sd <path>` to stage assets
- Use `scripts/asset-manager.py validate --sd <path>` to validate assets
