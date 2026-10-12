# Native activation and fullscreen ordering

These are interactive desktop checks. Run them with no game/Wine session active,
and do not interact with the test windows. They capture native application/window
metadata, never screenshots. They do not establish gameplay or visual acceptance.

The Steam handoff controls live with the integration component:

```sh
make -C src/notproton activation-relay
```

The Wine ordering/lifecycle controls use a complete compatible development runtime,
fresh private prefix and support directory, `scripts/play`, and the watchdog:

```sh
python3 -B tests/mac/test-launch-focus.py --runtime /path/to/development/runtime \
  --llvm-bin /opt/homebrew/opt/llvm/bin
```

The runner needs Xcode's command-line tools, Python, LLVM and the fetched Wine
headers. It builds an x86-64 Win32 window sequence without the Windows SDK or CRT,
and a native foreground parent/helper. The runtime is read only. The test does not
load the Steam bridge, change a game installation, or use an existing game prefix.
It rejects an existing Wine session before launching; do not launch games alongside
it. Cleanup targets only the new private prefix's wineserver. Own native windows
exit on bounded timers.

Six cases check the actual loaded driver: an inactive topmost fullscreen window
stays at normal level behind the foreground control without activating; a windowed topmost window
retains its floating level; a cooperatively activated fullscreen window is elevated;
switching during delayed creation keeps the fullscreen window behind the chosen
helper; resizing an inactive topmost window to fullscreen places it behind that
helper; and a non-topmost inactive window stays normal and behind. Callback/state observations,
Win32 sequence completion, unexpected foreground changes, window order, mapped
driver identity and hashes determine the result. Missing evidence fails.

The first case reproduces baseline's bug: Wine's inactive fullscreen policy computes
a normal level, but its later floating floor overrides it and produces level 21.
The patch preserves the policy before applying that floor. Run baseline and patched
components in identical configurations; the runner does not swap components itself.
Logs and receipts are retained under `build/launch-focus/run-*`.

The observer uses `kCGWindowListOptionOnScreenOnly`, which Apple's SDK explicitly
documents as front-to-back. `kCGWindowListOptionAll` provides metadata for on/off
screen windows but does not guarantee their array order, so it cannot establish
relative window placement. The observer never reads pixels or takes screenshots.

Also run the existing [native ordering and Win32 minimization checks](minimization.md).
Those checks do not prove immediate restore before native minimization starts, nor
correct fullscreen resolution changes. Real acceptance uses owner-led ordinary
Steam launches, deliberately switching during startup and returning manually,
then repeats without temporary diagnostics.
