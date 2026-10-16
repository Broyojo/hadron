# Cities II, Ready or Not and R.E.P.O.: test results

2026-10-16, M4/16 GB/macOS 27.0.1. Cities II (949230), build 25127643;
Ready or Not (1144200), build 25784851. Both downloads were complete.

## Test conditions

These are user-confirmed gameplay results in a separate investigation runtime,
not acceptance of an unmodified release. The runtime combines local Wine
foreign-window/client-layer and executable-ASLR experiments, the revised
partial-decommit correction proposed in PR #79, DXMT foreign-window presentation
and the device-initialization correction proposed in PR #80, and the footprint
watchdog proposed in PR #77. Unrelated experiments have not yet been eliminated
in a clean integration run. Do not infer each change is necessary for either game.
Games ran through scripts/play in copied Steam prefixes; critical-pressure
protection stayed enabled. No repository game defaults were added.

## Cities: Skylines II

Direct Cities2.exe launch uses D3D11/DXMT. With USER=steamuser, the 14:55:58 run
reached its menu after about 75 seconds. The player selected Low/VeryLow graphics,
changed from a diagnostic 1280x720 window to 2560x1440 fullscreen and loaded
Archipelago. SceneFlow records loading completed at 15:03:38; the player confirmed
it works. Peak footprint was 11,417 MB with elevated (level 2), not critical,
pressure. User settings were preserved in the existing Steam prefix with a backup.

A personal explicit entry selection chooses Cities2.exe instead of Launcher/dowser.exe;
the generic nested-launcher selection is in PR #78. Normal Steam Play acceptance
with that preference remains pending. The Paradox Launcher is not claimed fixed.
Earlier runs include a correct watchdog stop at critical pressure on Medium
settings and an unexplained invalid-stack-frame exception. A first run with the
incorrect host USER produced invalid app-data paths and is excluded from acceptance.
Startup speed and intermittent failures remain open.

## Ready or Not

The default D3D12 path initialized vkd3d-proton/KosmicKrisp. A two-second native
sample showed a background worker compiling MSL in Metal while the game thread
waited. The run continued creating pipelines and updating its cache for about ten
minutes, then was intentionally stopped for comparison. It did not self-crash;
lag during sampling is excluded.

The supported -dx11 option uses DXMT and reached the menu earlier. The player
confirmed in-game play works. Initial comparisons also passed -windowed -ResX=1280
-ResY=720; these diagnostic arguments were removed for the following run. No fake
feature levels or repository per-title renderer default were introduced. A missing
Wine GStreamer media-handler warning did not prevent menu/gameplay acceptance;
it is not established as a blocker.

Open: the native cursor reappears after touching the bottom screen edge, even
when the Dock stays hidden; tabbing out and back hides it again. Cursor diagnostics
are ongoing; this report does not claim a cursor fix. Normal Steam Play acceptance
and a clean integration-runtime run remain pending.

## R.E.P.O.

The player separately reports R.E.P.O. (3241660) works without problems. No
independent agent run, runtime qualification or performance measurement was made.
