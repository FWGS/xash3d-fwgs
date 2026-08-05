Host-only Swiss contracts (no toolchain required).

- `scripts/waifulib/gamecube_storage.py` — volume preference + layout + log parsers
- `SKIP_GAMECUBE_BUILD=1 scripts/ai-verify.sh` runs host unit tests
- Probe analyzer emits `STORAGE_STATUS` / `G508_STATUS`
- Release packet supports `--dry-run` evidence fixtures

```sh
SKIP_GAMECUBE_BUILD=1 scripts/ai-verify.sh
scripts/gamecube-hardware-layout-info.sh --route sdgecko
```
