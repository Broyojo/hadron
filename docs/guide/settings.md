# Settings

Hadron has no settings window. Its settings are environment variables, which you give a game in
one of two ways:

- as the game's **launch options** in Steam, written as plain `NAME=value` words
  ([how](./steam-games#launch-options)), or
- in a **games.conf** file, which keeps them for a game or a program (below).

Hadron starts Wine with a clean environment, so a variable reaches the game only if Hadron passes
it on. The settings on this page are the ones it does.

## Per-game settings: games.conf

Settings you want to keep for a game, without typing them into Steam, go in your own games.conf:

```
~/Library/Application Support/Hadron/games.conf
```

Create the file if it does not exist. Each line names a game by its Steam **app ID**, then lists
settings:

```
# <app id> NAME=value ...
400 HADRON_VSYNC=on
1167630 VKD3D_SWAPCHAIN_MIN_LATENCY_FRAMES=1 MTL_HUD_ENABLED=1
```

The app ID is the number in the game's Steam store address: `store.steampowered.com/app/400` is
Portal. It is also the name of the game's folder in `steamapps/compatdata`.

A **program that is not on Steam** has no app ID, so its line starts with `exe:` and its file
name:

```
exe:setup.exe HADRON_LOG=1
exe:My Game.exe HADRON_VSYNC=off
```

The name may hold spaces; it ends at `.exe`. Case does not matter, as on Windows. A line applies to
that file wherever it is installed, and to programs it starts.

How settings are combined:

1. A game's Steam launch options win.
2. Then your games.conf.
3. Then the games.conf that ships with Hadron, for Hadron's own defaults for a game, as Proton's
   app defaults are. It has no entries at the moment.

A setting is only taken while it is not already set, so the first place that sets it wins.
Everything after a `#` on a line is a comment. A games.conf only sets variables; nothing in it is
run as a command.

## Settings worth knowing

### Display and frame pacing

| Setting | What it does |
|---|---|
| `HADRON_VSYNC=game` / `on` / `off` | Whether frames wait for a display refresh. `game`, the default, follows the game's own vsync choice, as a Windows driver does; `on` and `off` override it, for games without a vsync setting. Direct3D 9, 10 and 11 games. Vsync on never tears and adds some input latency, as on Windows. |
| `MTL_HUD_ENABLED=1` | Shows Apple's Metal performance overlay (the **Metal HUD** switch on the [Compatibility page](./steam-games#the-compatibility-page)). |
| `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` with `DXMT_CONFIG=d3d11.metalSpatialUpscaleFactor=2.0` | MetalFX upscaling for Direct3D 10 and 11 games, by the factor given. The Compatibility page's **MetalFX Upscaling** menu writes both. See [Performance and display](./performance#metalfx-upscaling). |
| `VKD3D_SWAPCHAIN_MIN_LATENCY_FRAMES=1` | Direct3D 12 only. Hadron lets a D3D12 game that waits for each frame run one frame ahead (the default is `2`), which keeps the GPU busy: Teardown went from 16 to about 60 fps. A game that already reaches the display's refresh rate gets back one frame of latency with `1`. |

### Input

| Setting | What it does |
|---|---|
| `HADRON_PRECISE_SCROLLING=1` | Sends fractions of a wheel notch, for smooth scrolling in programs that scale by the wheel's movement. By default Hadron sends whole notches, which every game handles. |

### Logs

| Setting | What it does |
|---|---|
| `HADRON_LOG=1` | Writes Wine's log (errors and the libraries loaded) to `~/Library/Logs/Hadron`. Every Steam launch and every program opened from Hadron's window already does this; it matters for `hadron run`, which otherwise keeps the output in the terminal. |
| `HADRON_LOG_MAX_MB=64` | Past this size (64 MB by default) a log keeps its start and its end and drops the middle. |

### Memory watchdog

While a game runs, Hadron's watchdog logs its memory use once a second and protects the Mac
([what it does](./troubleshooting#memory-warnings-and-stopped-games)). These make it stricter:

| Setting | What it does |
|---|---|
| `HADRON_MEM_LIMIT_GB=<n>` | Stop the game when its memory footprint passes *n* GB. |
| `HADRON_SWAP_LIMIT_MB=<n>` | Stop the game when swap has grown by more than *n* MB since it started. |
| `HADRON_DISK_MIN_GB=<n>` | Stop the game when free disk space falls below *n* GB (1 GB by default). |
| `HADRON_WATCHDOG=0` | Turns the watchdog off. Not recommended: a runaway game can then fill memory until the Mac stalls. |

Values are whole numbers. An invalid value stops the game from starting, so that it never runs
unguarded by mistake.

### Compatibility

| Setting | What it does |
|---|---|
| `HADRON_LARGE_ADDRESS_AWARE=0` | 32-bit games get the full 4 GB address space by default, as under Proton. This limits the ones not marked large address aware to 2 GB, as on Windows. |
| `HADRON_D3D9=wined3d` | Runs Direct3D 9 through Wine's own implementation (on OpenGL) instead of mtld3d. Meant for comparing the two when a D3D9 game draws wrongly. |

### The x86 translator (FEX)

FEX reads its options from `FEX_` variables, and Hadron passes all of them on. Two that help when
a game misbehaves:

| Setting | What it does |
|---|---|
| `FEX_DISKCACHE=0` | Turns off FEX's cache of translated code. The cache is on by default and kept in the game's prefix, up to 4 GiB per cache file, so later starts translate less. Turning it off rules the cache out. |
| `FEX_SMCCHECKS=full` | Checks code for changes before every run, for programs that rewrite their own code. Slow. The default is `mtrack`. |

## Settings that are not variables

**Retina mode** is a setting in each prefix's Windows registry, not an environment variable, so it
cannot go in launch options or games.conf. See
[Performance and display](./performance#retina-mode).

## For developers

`scripts/play` in the repository documents every variable the launch scripts read, including the
ones meant for testing Hadron itself. [AGENTS.md](https://github.com/Broyojo/hadron/blob/main/AGENTS.md)
explains how they are used when debugging.
