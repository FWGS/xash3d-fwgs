# Touch controls configuring

## Introduction

Thanks to mittorn, we have the ability to fully customize the controls in Xash3D. The new config allows you to not only add and change control buttons, but also create custom menus. There is also a built-in visual editor available, which simplifies the customization process without the need for manual file editing.

## Editor mode usage

1. Launch Xash3D and start the game.
2. To enter the edit mode, click on the gear icon (command `touch_enableedit`).
3. In the edit mode, the grid is displayed. The number of cells can be changed with the command `touch_grid_count` (default is 50). The grid can be disabled with the command `touch_grid_enable 0`.

*Touch controls layout editor*
![](images/editor.jpg)

*Touch profiles window*
![](images/touch-profiles.jpg)

*Touch buttons parameters window*
![](images/touch-buttons.jpg)

## Layout editor features

* **Moving buttons**: click on a button, drag it to the desired location, and release.
* **Resizing buttons**: place your first finger on the top left corner of the button, and use your second finger to resize it.
* **Hiding/showing buttons**: select the button (it will turn red), then use the menu:
    * **Close**: closes the editing mode (`touch_disableedit`).
    * **Reset**: resets the button to its default values.
    * **Hide/Show**: hides or shows the button (`touch_hide <name>` / `touch_show <name>`).

## Working with configuration files / console

All changes made in the visual editor are automatically saved to the touch profile file. The files are located in the `touch_profiles` folder inside game directory. The file name depends on the selected profile.

### Common commands in configuration files

```
// Comment: lines starting with // are ignored.

// Swipe zones for movement. Specify how far you need to swipe to speed up.
// The default values ​​are set to be optimal for a small swipe to immediately walk quickly.
touch_forwardzone "0.060000" ; touch_sidezone "0.060000"

// Sensitivity settings (pitch - horizontal, yaw - vertical).
touch_pitch ; touch_yaw

// Grid settings (see above).
touch_grid_count ; touch_grid_enable

// Draws a border around the button.
touch_set_stroke <thickness> <c> <v> <e> <t>
(<r> - red; <g> - green; <b> - blue; <a> - alpha/transparency)

// Show (1) or hide (0) client buttons.
touch_setclientonly

// Removes all buttons from the screen.
touch_removeall

// Shows all available buttons in the configuration file.
touch_list

// Saves the config after editing. You can specify a new file name.
touch_config_file command.cfg
```

*The full list of buttons in the standard `touch.cfg` file, displayed in the console when using `touch_list`.*
![](images/example2.jpg)


## Adding new buttons

To add a new button, use the console command:

```
touch_addbutton "digits" "touch/key_1.png" "toggle_digits" 0.340000 0.782222 0.400000 0.888889 255 255 255 100 0
```

### Parameters of `touch_addbutton` command

| Parameter | Description |
| --- | --- |
| `"digits"` | Unique name of the button. |
| `"touch/key_1.png"` | Path to the icon file (`.png` format). If the icon is not needed, leave `""`. |
| `"toggle_digits"` | Command to execute after clicking (for example, `"buy"` for purchase). |
| `0.340000` | X coordinate of the upper-left corner of the button. |
| `0.782222` | Y coordinate of the upper-left corner of the button. |
| `0.400000` | X coordinate of the lower-right corner of the button. |
| `0.888889` | Y coordinate of the lower-right corner of the button. |
| `255 255 255` | Button color in RGB format. |
| `100` | Button transparency (0 - fully transparent, 255 - fully opaque). |
| `0` | Flags (see next section). |

## List of flags

Flags define the behavior of the button. Their values ​​are powers of two:

| Flag | Value | Description |
| --- | --- | --- |
| `TOUCH_FL_HIDE` | 1 | Hides the button (not displayed in the game, but visible in the editor). |
| `TOUCH_FL_NOEDIT` | 2 | Disables editing of the button in the editor. |
| `TOUCH_FL_CLIENT` | 4 | The button is client-side (not saved in the main control file). |
| `TOUCH_FL_MP` | 8 | The button is displayed only in multiplayer. |
| `TOUCH_FL_SP` | 16 | The button is displayed only in singleplayer. |
| `TOUCH_FL_DEF_SHOW` | 32 | The button is always displayed on startup. |
| `TOUCH_FL_DEF_HIDE` | 64 | The button is always hidden on startup. |
| `TOUCH_FL_DRAW_ADDITIVE` | 128 | The button colors are added together in blend mode. |
| `TOUCH_FL_STROKE` | 256 | Enables outline stroke around the button. |

Flags can be combined by adding their values ​​together. For example, `5 = 1 + 4` is the combination of `TOUCH_FL_HIDE` and `TOUCH_FL_CLIENT` flags, which is a hidden client button.

*In the image below, the `spray`, `scores`, `messagemode` buttons are displayed simultaneously with the flag 8 and `loadquick`, `savequick` with the flag 16, and each is displayed in the corresponding game mode.*
![](images/example1.jpg)

## Useful commands

* `touch_hide <pattern>`: hides buttons by pattern.
* `touch_setcommand`: changes the command bound to a button.
* `touch_settexture`: quickly changes the button image.
* `touch_setcolor`: sets the button color.
* `touch_exportconfig`: exports the current configuration, including aspect ratio.

## Usage examples

* `touch_hide menu*` hides all buttons with names starting with `menu`.
* `touch_setcolor "attack" 255 160 0 128` changes the color of the primary fire button from opaque white to translucent orange, similar to the color of the HUD in Half-Life.

    *Result of executing this command*
    ![](images/example3.jpg)

* Example of adding a new button with a custom icon (the `lastinv` command is used - quick change between weapons)
    ![](images/example4.jpg)

    *View of this button in layout editor*
    ![](images/example5.jpg)

## Tips

* To prevent the `look` and `move` buttons from interfering with editing other elements, place them before the others in the configuration file.
* And vice versa, to make a button appear on top of others, place it at the end of the configuration file.
* You can assign commands to buttons that change other buttons, see the previous section "Usage examples".
* After editing each parameter of each individual button in the Touch Buttons section, do not forget to press Save, otherwise the applied parameters will not be saved.
* To create your own icon for the button, you can use any graphics editor (for Android, Photo Editor by iudesk is suitable). Saving conditions:
    * Image format - `.png` with transparency (e.g. alpha channel)
    * Aspect ratio / size - 1:1 / 256x256
    * Path to the icons location - `touch/gfx` inside game directory.
    
### Additional links
* [Handy palette for selecting color in RGB format](https://www.rapidtables.com/web/color/RGB_Color.html)


## Analog stick profile and safe areas

The optional **analog** and **analog_toggle** presets in `xash-extras` provide
circular movement and look controls with shared icons. Select a preset in the
menu's Touch options. Engine defaults and existing profiles remain available on
all platforms. Both presets use a single crouch button: `analog` holds crouch,
while `analog_toggle` toggles it through aliases. After loading either preset,
`exec touch_sticks/crouch_hold.cfg` and `exec touch_sticks/crouch_toggle.cfg`
switch the binding without engine-specific logic. Toggle bindings execute shared
scripts that define their own aliases, so saved profiles also work after restart.
The hold preset is recommended for duck jumps and the long-jump module, which
require coordinated crouch/jump presses. Toggle crouch is optional and changes
that timing. Its button binds an `exec` command, so releasing the touch does not
automatically issue `-duck`; the next press executes the opposite alias.

A button with flag `2048` (`TOUCH_FL_STICK`) extends existing `_joy` movement or
`_look` input with a visible thumb disc. Movement is proportional to displacement
and clamps diagonals to the circle. Look is neutral at pickup and turns while
held off-centre, using elapsed time rather than the simulation frame rate. There
is no dead zone. Without the flag, `_joy` and `_look` retain their old behavior.

The rate stick respects `m_pitch`'s sign (Invert mouse), `touch_pitch`, `touch_yaw`,
precision and the client DLL's aim/zoom sensitivity. `touch_look_speed` defaults
to 60 degrees/second before client sensitivity at full horizontal deflection;
vertical speed also includes `touch_pitch / 120`. `touch_look_curve` defaults to 2
(quadratic), with 1 for a linear response and a maximum of 3. A long frame is
capped at 0.1 seconds to avoid a jump after a stall. Rate input enters after the
mouse/swipe filter so release does not leave a smoothing tail.

Native profiles and their editor map coordinates into the drawable window's
safe area. Outside touches are not clamped onto edge buttons; captured fingers
still receive motion/release outside the rectangle. Game-provided touch callbacks
keep their existing screen-coordinate contract. Saved profile aspect handling is
retained, with safe-area scaling applied at rendering and native input boundaries.

SDL3 uses `SDL_GetWindowSafeArea`. SDL2 uses UIKit safe-area insets on iOS and an
Android UI-thread snapshot of surface-relative window/cutout insets. Other
backends return zero insets. Insets are normalized so high-density window units
and renderer pixels do not get mixed. `mainui_cpp` reads the `WindowInsets` native
object and sizes/positions its root menus inside the safe rectangle. Engine menu
draw and input APIs continue using screen coordinates; older menu DLLs work as
before but must adopt the native object to avoid cutouts.

SVG sources and pre-rendered PNGs live in `xash-extras/gfx/touch`. Normal builds
package the PNGs in `extras.pk3` on every platform without a new dependency. To
regenerate them, configure Waf with `--render-touch-icons` and an existing resvg or Inkscape
executable on PATH. This stages rendered assets under the build directory before
packaging; it does not overwrite the source PNGs.

Regression tests are registered in `engine/common/tests.h` under
`XASH_ENGINE_TESTS`: touch clamping, small displacements, neutral pickup,
frame-rate-independent look, inversion, release, legacy swipe input and window
inset validation. Run a test-enabled engine with `-dev 2 -runtests`. Simulator
interaction validates native safe-area reporting and menu hit targets; Android
and SDL3 require their own runtime validation before claiming coverage.

The earlier device screenshot shows the visual design; its saved profile predates
the shared presets:

![Analog controls in game](images/ios-touch-ingame.png)
