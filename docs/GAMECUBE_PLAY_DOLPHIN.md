# Playing in Dolphin (with mute)

```
scripts/play-dolphin.sh            # start muted (default) with the mute window
scripts/play-dolphin.sh --unmuted  # start with sound on
scripts/play-dolphin.sh --unmuted --save-default   # make sound-on the default
scripts/play-dolphin.sh --build    # rebuild the play disc first
scripts/play-dolphin.sh --install-desktop          # add an app-menu entry
```

The launcher starts `OUT/xash3d-gc-play.iso` (menu New Game with the real
controller; built automatically when missing) in the project's Dolphin
(`3rdparty/dolphin/build/Binaries/dolphin-emu`, else `dolphin-emu`) with
the same core settings as the retail probe (JIT, CPU thread, DSP HLE, MMU,
fast disc), under X11 (`EGL_PLATFORM=x11`, `QT_QPA_PLATFORM=xcb`, no
`WAYLAND_DISPLAY`).

A small always-on-top window shows **MUTED** (red) or **SOUND ON** (green);
click it, or press Space or M while it has focus, to toggle. It reads the
real state back from the sound server every half second.

## How mute works

The window mutes Dolphin's own output stream in PulseAudio/PipeWire
(`pactl set-sink-input-mute`). Only the speakers go quiet: Dolphin and the
game keep running and mixing audio at full speed, so timing is unchanged.
Start-muted mutes the stream the moment Dolphin opens it, before the game
makes a sound. Dolphin's own mute hotkey was not used: with the hotkey
focus requirement off (needed for a separate window to trigger it), all of
Dolphin's keyboard hotkeys become global (Escape would stop the game from
any window).

Settings: `~/.config/xash3d-gc/play.conf` (`START_MUTED=1|0`, default 1).
The launcher's Dolphin profile is `~/.local/share/xash3d-gc/dolphin`; on the
first run it copies the controller mapping from your normal Dolphin profile
if there is one. Map the controller once under Controllers > Port 1 >
Standard Controller; it is kept. Controls: docs/GAMECUBE_CONTROLS.md.

## Probes

`scripts/dolphin-boot-probe.sh` is unaffected. Its audio checks (`nonzero
PCM`, the `-gcprofile` audio meter) measure the game's own mixer output,
not the speakers, so they hold with any Dolphin volume
(`DOLPHIN_MUTE_AUDIO=1` silences a probe run).

Verified 2026-10-29 with a PulseAudio null sink: started muted, RMS 0 at
the sink monitor; unmuted, RMS 8014 / peak 32004; muted again, RMS 0. The
game's audio meter stayed non-zero (peak 27-32k) throughout.
