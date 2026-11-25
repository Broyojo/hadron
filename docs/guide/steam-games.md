# Playing Steam games

Once [Steam is set up](./install#_2-set-up-steam), you play from Steam. Hadron does not need to be
open.

## Windows-only games

Games that only exist for Windows need nothing: they use Hadron on their own. Install the game in
your Steam library and choose **Play**.

The first start takes longer than the ones after it. Hadron makes the game its own Windows folder
structure (a *prefix*, in `steamapps/compatdata/<app id>` of your Steam library), and the game
compiles its shaders. The first start after a Hadron update compiles them again.

The Dock and the menu bar show the game under its Steam name, with the game's own icon.

## Games that also have a Mac version

A game that also has a Mac version is installed as that Mac version unless you say otherwise.
Many older Mac versions are 32-bit and no longer run at all; a 64-bit Intel one still runs,
through Rosetta.

To play the Windows version through Hadron instead:

1. Right-click the game in your library and choose **Properties**.
2. Go to **Compatibility**.
3. Tick **Force the use of a specific Steam Play compatibility tool**. Hadron is selected for you.

Steam then downloads the Windows build. This is the same setting Proton uses on Linux; Portal and
Portal 2 were played this way.

## Launch options

Hadron's settings are environment variables, and you give them to a game as its **launch
options**: right-click the game, **Properties**, **General**, **Launch Options**. Write each
setting as a plain `NAME=value` word, separated by spaces:

```
HADRON_VSYNC=on MTL_HUD_ENABLED=1
```

::: danger Do not use %command%
On Linux, Proton's launch options are often written as `NAME=value %command%`. That does not work
on the Mac: Steam for Mac does not run launch options through a shell, and the game fails to start
with an error such as "OS Error 260". Write only the `NAME=value` words.
:::

Hadron takes a word as a setting when its name starts with `HADRON_`, `WINE`, `DXMT_`, `MTLD3D_`,
`MTL_`, `DXVK_`, `VKD3D_`, `MESA_` or `FEX_` (and a few prefixes the Compatibility page writes).
Every other word is passed on to the game as an argument, so game options such as `-novid` can sit
next to settings:

```
HADRON_VSYNC=on -novid
```

[Settings](./settings) lists the settings worth knowing, and how to keep settings for a game in a
file instead.

## The Compatibility page

For a game that runs through Hadron, its **Properties → Compatibility** page also shows a
**Graphics** panel. Each control there writes launch options for you:

- **Metal HUD** shows Apple's Metal performance overlay over the game. It writes
  `MTL_HUD_ENABLED=1`.
- **MetalFX Upscaling** (Off, 1.5x, 1.72x, 2x, 3x) is for **Direct3D 10 and 11 games**. The game
  renders at the resolution it is set to, and MetalFX scales the picture up by the factor you
  choose. It writes `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` and
  `DXMT_CONFIG=d3d11.metalSpatialUpscaleFactor=<factor>`. See
  [Performance and display](./performance#metalfx-upscaling).

The panel comes from NotProton, whose Steam Play hook Hadron uses. Hadron shows only the options
its renderers actually read.

## What works from Steam

The Mac Steam client serves the game: you are signed in, friends and cloud saves work, and the
Steam overlay is drawn over the game. A game's Windows saves that are not in Steam Cloud stay in
its prefix (`steamapps/compatdata/<app id>/pfx`); do not delete that folder unless you mean to lose
them.

Steam's **Stop** button ends the game's whole Windows session, including anything its launcher
started.

## When a game does not start

See [Troubleshooting](./troubleshooting), and check [HadronDB](https://hadrondb.com) to see how a
game runs for others, or to report how it runs for you.
