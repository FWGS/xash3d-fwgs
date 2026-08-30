Host-only G501/G504/G508 contracts (no toolchain).

```sh
python3 scripts/gamecube-runtime-ladder.py --fixture <probe-log-dir>
python3 scripts/gamecube-experiment-manifest.py --hypothesis '...' --dry-run
python3 -m unittest discover -s tests -p 'test_gamecube_host.py' -v
```

Ladder stops at first missing gate. Manifest records tier + OGC stack.
Probe-save path rules mirrored in `scripts/waifulib/gamecube_probe_save.py`.
