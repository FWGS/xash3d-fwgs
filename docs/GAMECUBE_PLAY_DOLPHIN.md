# Playing in Dolphin (with mute)

```
scripts/play-dolphin.sh            # start muted (default) with the mute window
scripts/play-dolphin.sh --unmuted  # start with sound on
scripts/play-dolphin.sh --unmuted --save-default   # make sound-on the default
scripts/play-dolphin.sh --build    # rebuild the play disc first
scripts/play-dolphin.sh --install-desktop          # add an app-menu entry
scripts/play-dolphin.sh --no-record                # do not record this session
```

The launcher starts `OUT/xash3d-gc-play.iso` (menu New Game with the real
controller; built automatically when missing) in the project's Dolphin
(`3rdparty/dolphin/build/Binaries/dolphin-emu`, else `dolphin-emu`) with
the same core settings as the retail probe (JIT, CPU thread, DSP HLE, MMU,
fast disc), under X11 (`EGL_PLATFORM=x11`, `QT_QPA_PLATFORM=xcb`, no
`WAYLAND_DISPLAY`).

A small always-on-top window shows **MUTED** (red) or **SOUND ON** (green),
and **● REC** while recording; click it, or press Space or M while it has
focus, to toggle. It reads the real state back from the sound server every
half second.

## How mute works

Only the speakers go quiet: Dolphin and the game keep running and mixing
audio at full speed, so timing is unchanged. Two ways, depending on
recording:

- Recording (default): Dolphin plays into a private null sink (see below).
  SOUND ON adds a loopback from that sink to the speakers, MUTED removes it.
  The window reads the state back from the sound server (the loopback
  modules that exist), so what it shows is what is heard.
- `--no-record`: the window mutes Dolphin's own output stream
  (`pactl set-sink-input-mute`), applied the moment Dolphin opens it.

Start-muted means no game sound reaches the speakers at any point. Dolphin's
own mute hotkey was not used: with the hotkey focus requirement off (needed
for a separate window to trigger it), all of Dolphin's keyboard hotkeys
become global (Escape would stop the game from any window).

Settings: `~/.config/xash3d-gc/play.conf` (`START_MUTED=1|0`, default 1).
The launcher's Dolphin profile is `~/.local/share/xash3d-gc/dolphin`; on the
first run it copies the controller mapping from your normal Dolphin profile
if there is one. Map the controller once under Controllers > Port 1 >
Standard Controller; it is kept. Controls: docs/GAMECUBE_CONTROLS.md.

## Recording playtests

Every manual session (unless `--no-record`; the default is kept in
`play.conf` as `RECORD=1|0`) and every Dolphin probe run (unless
`DOLPHIN_RECORD=0`) is recorded with sound to `.ai/playtests/`:

- `<date>-<route>-<commit>.mp4`: the render window (x11grab, 30 fps,
  H.264 CRF 30) with the game's audio (AAC 96 kb/s), about 2 MB per minute.
  Written as Matroska while recording (survives a crash) and remuxed to MP4
  at the end; if the remux fails the `.mkv` is kept.
- `<same>.json`: commit, disc, route, duration, streams, audio mean/peak,
  size, MB/min, probe log dir and ladder result, `keep` flag.
- `INDEX.md`: newest first, regenerated after every recording.

Audio comes from a private PulseAudio/PipeWire null sink Dolphin plays into,
so the recording has the game's mix even while muted. In a manual session the
mute window plays that sink on the speakers through a loopback it adds
(SOUND ON) or removes (MUTED); the window shows "● REC" while recording.
`XASH_PLAY_LOOPBACK_SINK=<sink>` plays it on a specific output instead of the
default one. `pkill -USR1 -f '^python3 .*mute-toggle'` toggles from a shortcut.

Disk: recording is skipped when the drive has less than 5 GB free. Only
compressed output is kept; nothing raw is written. Retention:

```
scripts/prune-playtests.sh          # dry run: lists what would go
scripts/prune-playtests.sh 20 --yes # keep the newest 20 (default) + keep=true
scripts/playtest-record.py annotate .ai/playtests/<rec>.json keep=true
```

## Probes

`scripts/dolphin-boot-probe.sh` is unaffected. Its audio checks (`nonzero
PCM`, the `-gcprofile` audio meter) measure the game's own mixer output,
not the speakers, so they hold with any Dolphin volume
(`DOLPHIN_MUTE_AUDIO=1` silences a probe run).

Verified 2026-10-29 with a PulseAudio null sink: started muted, RMS 0 at
the sink monitor; unmuted, RMS 8014 / peak 32004; muted again, RMS 0. The
game's audio meter stayed non-zero (peak 27-32k) throughout.
