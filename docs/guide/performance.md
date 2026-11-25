# Performance and display

## What to expect

Little in Hadron has been optimised yet. Some games run at the display's refresh rate; others are
held back by translating the game's x86 code to Arm, which costs CPU time (Teardown's frame rate
is limited that way). The [list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md)
gives numbers where they were measured, and [HadronDB](https://hadrondb.com) collects how games
run for others.

- **The first start is slow.** A game's first launch makes its prefix and compiles its shaders.
  Later launches reuse both. The first launch after a Hadron update compiles shaders again.
- **To see the frame rate**, turn on **Metal HUD** on the game's
  [Compatibility page](./steam-games#the-compatibility-page), or add `MTL_HUD_ENABLED=1` to its
  launch options.
- **Tearing, or the frame rate locked to the display:** `HADRON_VSYNC=on` or `off` overrides the
  game's vsync choice for Direct3D 9 to 11 games ([Settings](./settings#display-and-frame-pacing)).

## Retina mode

Since Hadron 0.3.1, games see a Retina display at its real pixels, as Windows shows a high-density
display: the built-in panel of a MacBook Pro is 3024×1964 to the game, not 1512×982. That gives a
sharp picture, but a game that draws at the desktop's size now draws four times as many pixels as
it would at the size in points, which can cost frame rate.

To trade sharpness for speed:

- **Pick a lower resolution in the game's own settings**, such as 1512×982 (half the size each
  way) or lower.
- For **Direct3D 10 and 11 games**, combine a lower resolution with
  [MetalFX upscaling](#metalfx-upscaling).

A windowed game looks half as large on screen as it did before 0.3.1, since its pixels are now the
display's. Windows programs are still told the display is at 96 DPI.

### Turning Retina mode off

Retina mode is Wine's `RetinaMode` setting, a string value under
`HKEY_CURRENT_USER\Software\Wine\Mac Driver` in a prefix's registry. Hadron makes it the default;
`RetinaMode` set to `n` in a prefix gives that prefix's programs the display in points again.
Every process in a prefix has to agree on it, which is why it is a setting of the prefix (one per
Steam game, in `steamapps/compatdata/<app id>/pfx`, and one for all
[programs outside Steam](./windows-programs#the-shared-programs-prefix)) and not a launch option.
Hadron has no switch for it yet; only change a prefix's registry while nothing is running in it.

## Resolution and fullscreen

Hadron never changes your display's mode. A game sees modes up to the size of each display's
desktop, and when it picks one, the change succeeds at once and the display stays as it was; the
game's window covers the display and its picture is scaled up to it. Native Mac games work the same
way. Old games that set brightness through gamma ramps get no effect from it, as under Proton.

Fullscreen is a borderless window over the display, not a separate full-screen Space.

::: warning Fullscreen at a resolution of another shape (Hadron 0.3.3)
When a game runs fullscreen at a resolution whose shape differs from the screen's, such as 16:9 on
a MacBook's taller panel, the picture should be scaled to fit with black bars, as on Windows. In
0.3.3 part of the picture is cut off or covered by a black bar instead. A fix ([issue #137](https://github.com/Broyojo/hadron/issues/137))
is planned for 0.4.0. Until then, a resolution with the screen's shape, or windowed mode, avoids
the problem.
:::

### External displays

With an external display, games open on the main display (the one with the menu bar), and moving a
running game to another display is unreliable. Other displays stay on while a game is fullscreen.

## MetalFX upscaling

For **Direct3D 10 and 11 games** (those that run on DXMT), Hadron can upscale with Apple's MetalFX.
The game renders at the resolution it is set to, and MetalFX scales the picture up by a factor you
choose, so a game set to 1512×982 with **2x** is shown at 3024×1964.

Turn it on from the game's **Properties → Compatibility** page with the **MetalFX Upscaling** menu
(Off, 1.5x, 1.72x, 2x, 3x), or with launch options:

```
DXMT_METALFX_SPATIAL_SWAPCHAIN=1 DXMT_CONFIG=d3d11.metalSpatialUpscaleFactor=2.0
```

Then lower the resolution in the game's own settings by the same factor. These settings are
DXMT's, so they do nothing for Direct3D 9, Direct3D 12, Vulkan or OpenGL games. If the Mac's GPU cannot run the MetalFX scaler,
the game runs without it.

## Game Mode

macOS Game Mode does not turn on for Hadron's games yet. Hadron declares each game to macOS as a
game, but that is not enough: macOS does not see the game's process as an app it started, and
Hadron's fullscreen is not a full-screen Space, which is likely what macOS looks for. This is an
open problem
([findings #44](https://github.com/Broyojo/hadron/blob/main/docs/findings.md#game-mode-never-sees-a-hadron-game-44-open)).
