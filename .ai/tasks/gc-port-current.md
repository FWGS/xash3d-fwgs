Auto-port task for Xash3D GameCube
=================================

Current goal:
G508: Prove writable config/save round trips on the Dolphin-designated route

**IN PROGRESS (G508 source wiring)**:
- Extended gcprobe RAM bank for config.cfg (.new/.bak) write/read/rename/delete
- Added `-gcconfigroundtrip` guest flag and disc `configroundtrip` override
- Wired `DOLPHIN_G508=1` into dolphin-boot-probe / disc staging
- Release packet now derives persist/changelevel limitations from probe evidence
- Host contract test: `test_g508_config_roundtrip_probe_contract`

Next acceptance step:
- Run `DOLPHIN_NEWGAME=1 DOLPHIN_G508=1 scripts/dolphin-boot-probe.sh`
- Archive `G508 config round trip ready route=gcprobe|sd`
- Keep physical SD fault cases hardware-only

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
