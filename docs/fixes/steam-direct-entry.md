# Explicit Steam entry point selection

The player confirmed Raft and Cities: Skylines working on an M4/16 GB/macOS 27.0.1.
Cities was confirmed with a direct `Cities.exe` start, then through Steam with the explicit
personal setting `255710 HADRON_GAME_EXE=Cities.exe`. Steam authentication, guarded launch,
Stop tracking and the existing save prefix are retained. No repository app default is added.

The Paradox Launcher remains unresolved: its GPU/renderer crashes and displaced click targets
are not claimed fixed here. This feature implements the user's explicit request to choose the
direct installed program; it does not automate privacy acceptance or pretend the launcher works.

The resolver tests main entry selection, paths with spaces, unchanged default and installer
routes, nested helpers, missing files, traversal, backslashes, directories and symlinks.
Only a `waitforexitandrun` program directly in the install root is eligible by default.
For a nested launcher, `HADRON_LAUNCHER_EXE=Launcher/launcher.exe` opts in only the exact
original relative path. Absolute paths and dot components are rejected. Other helpers keep
their original entry point. A helper Steam
itself starts there with that same verb is indistinguishable and would also be redirected;
this limitation is why selection is opt-in. Launcher-specific arguments are retained and may
need removing. The file is validated before activation. Spaces work in the resolver, but the
existing games.conf format splits settings on whitespace; use Steam's argument UI for such names.
