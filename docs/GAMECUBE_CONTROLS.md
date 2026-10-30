# GameCube controller

Default layout on port 1 (any port works; the first pad with input takes over).

| Control | In game | In menus |
|---|---|---|
| Main stick | move / strafe | navigate |
| C-stick | look (yaw / pitch) | |
| A | jump (`+jump`) | confirm |
| B | use (`+use`) | back |
| X | reload (`+reload`) | |
| Y | flashlight (`impulse 100`) | |
| Z | duck (`+duck`) | |
| R | fire (`+attack`) | |
| L | alt fire (`+attack2`) | |
| D-pad up / down | previous / next weapon (`invprev` / `invnext`) | navigate |
| D-pad left / right | last weapon (`lastinv`) / next weapon | navigate |
| Start | pause / menu (`cancelselect`) | |

Look: the C-stick has a quadratic response (fine aim near the centre, full
speed at full tilt). Full tilt turns `joy_yaw` degrees per second
horizontally (default 160) and `joy_pitch` vertically (default 120). Up on
the C-stick looks up; set `joy_pitch` negative to invert.

## Rebinding

After the defaults are applied the engine runs `exec gamecube_pad.cfg`.
Put the file in the game directory: `Half-Life/valve/gamecube_pad.cfg`
before building the disc, or `xash3d/valve/gamecube_pad.cfg` on an SD
card. Gamepad key names: `A_BUTTON`, `B_BUTTON`, `X_BUTTON`, `Y_BUTTON`,
`Z_BUTTON`, `L1_BUTTON` (L), `R1_BUTTON` (R), `START_BUTTON`, `DPAD_UP`,
`DPAD_DOWN`, `DPAD_LEFT`, `DPAD_RIGHT`. Example:

```
// swap jump and duck, invert look, slower turning
bind A_BUTTON "+duck"
bind Z_BUTTON "+jump"
joy_pitch -120
joy_yaw 120
```

## Playing in Dolphin

The probe disc (`--probe-menu-newgame`) drives the menu with a scripted
pad and ignores a real controller. For playing, build the play disc, which
boots the same menu New Game route with the real controller:

```
python3 scripts/build-gamecube-disc.py --output OUT/xash3d-gc-play.iso --data Half-Life/valve --play
```

Map a controller to GameCube port 1 in Dolphin (Controllers > Standard
Controller > Configure).

## Automated controller tests

`scripts/dolphin-boot-probe.sh` with `DOLPHIN_PAD_PIPE=1` maps port 1 to a
Dolphin named-pipe device and enables background input. The FIFO needs a
POSIX filesystem, so also set `DOLPHIN_USER_DIR` to a tmpfs path. A driver
then writes commands into `$DOLPHIN_USER_DIR/Pipes/gcpad`, one per line:
`PRESS A`, `RELEASE A`, `SET MAIN x y`, `SET C x y` (0..1, 0.5 = centre,
y 0 = up). With `-gcprofile` (`XASH_GC_PROBE_PROFILE=1` when building the
disc) the game logs the raw pad, view angles, origin and usercmd move once
a second (`pad port=1 ...`) and every button press (`pad press ...`).
