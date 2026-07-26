All Xash3D GameCube port automatable goals (G83-G276) are complete.

Manual checkpoint:
G38 physical GameCube validation is pending operator execution. The hardware
handoff package is ready at `.ai/logs/hardware-handoff-20260725-183248/`.

Current state:
- All automatic goals completed (G83-G276, G400-G433)
- G38 manual validation pending (operator-only)
- Build verified: boot.dol (5.8M), xash (33M ELF 32-bit PowerPC)
- SHA256: bdf3f7c5... (commit e5a483b773)
- Stub modules inventoried (G400): server, client, pm_shared stubs
- Asset staging tools created (stage-sd-assets.sh, stage_sd_assets.py)
- Unified asset management system created (asset-manager.py)
- G400: Stub module inventory (completed)
- G410-G412: Asset root discovery and SD staging (completed)
- G420-G424: Telemetry, BSP loading, server/client verification (completed)
- G430-G433: Memory telemetry and optimization (completed)

Manual hardware validation with physical GameCube, Swiss loader, or compatible
Wii/GameCube-mode hardware. See `docs/HARDWARE_TESTING_GUIDE.md` for procedures.

Asset management:
- Use `scripts/asset-manager.py discover` to find asset roots
- Use `scripts/asset-manager.py stage --source <path> --sd <path>` to stage assets
- Use `scripts/asset-manager.py validate --sd <path>` to validate assets
