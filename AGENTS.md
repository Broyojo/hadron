# Working on Hadron

Notes for anyone changing Hadron, written to be read by a person or by their coding agent. They
are what building it taught, in the order you are likely to need them. [README.md](README.md) has
the map of the stack, [CONTRIBUTING.md](CONTRIBUTING.md) the rules for patches and pull requests.

## What a fix is

- **General, never for one game.** A game that fails shows a bug in a shared layer: Wine, FEX, a
  graphics layer, the launcher, the Steam library. Find that bug and fix it there, so that the
  next game benefits. A change that only makes one title start is not a fix. `config/games.conf`
  holds settings for a game, as Proton's app defaults do; it is not a place for workarounds.
- **Nothing faked.** No forced feature levels, no relaxed requirement checks, no feature reported
  that the driver lacks. Such a hack is fine as a local, environment-gated experiment to find the
  next blocker; it is never what gets committed.
- **Correct first, then fast.** Between a correct, slow fix and a fast, approximate one, take the
  correct one and write its cost down where it can be profiled later.
- **Know which layer you are in.** Direct3D 9 is mtld3d and Direct3D 10/11 is DXMT, both straight
  to Metal. Only Direct3D 12 (vkd3d-proton), Vulkan and OpenGL (Zink) run on KosmicKrisp. Check
  the README's table before saying where a bug lives.

## Evidence

- **The conformance suites are the ground truth** for Vulkan and OpenGL: `scripts/build-cts.sh`,
  then `scripts/cts.sh` (it lists its suites). Check a driver change against the last results in
  [docs/conformance.md](docs/conformance.md) before installing it, and never edit the suites.
  They say nothing about Direct3D 9 to 11.
- **Before calling something a Metal bug**, or any platform's: read the contract in the SDK
  headers and the specification, run with validation on, vary the test until each of your own
  assumptions is ruled out, and have someone (or a fresh agent given the code, not your
  conclusion) look at it. Then name it for what it is: a bug, unspecified behaviour, or a
  documented limit. Several "Metal bugs" here turned out to be Hadron's.
- **Read the failure before explaining it.** Print the driver's own reason, and make sure the
  experiment's shader compiled. Two explanations here were built on shaders that had not.
- **A failure that comes and goes is a race until shown otherwise**, not flakiness. "It passed N
  times" proves little when the failure comes in bursts; inject a delay at the suspected point,
  or run the old and new build side by side at the same time.
- **Measure before optimising.** `sample <pid>` shows where the native side spends its time, and
  the player feels it: it stops every thread a thousand times a second, which made Subnautica
  (about 100 threads) unplayable for as long as it ran. Take a few seconds of it, say when, and do
  not count lag reported during it. `vmmap` stops a game for minutes. mtld3d has an instrumented
  build (`MTLD3D_PERF=1 scripts/build-mtld3d.sh --no-install`) that reports every two seconds into
  `<game folder>/mtld3d-logs/`. [docs/findings.md](docs/findings.md) #25 is a worked example.

## Running games without hurting the machine

- **Always through `scripts/play`** (Steam launches go through it too). It starts the watchdog,
  which logs memory and stops the game on memory pressure, swap growth, a memory cap or low
  disk. A bug in an upload path once filled unified memory fast enough to freeze the Mac.
- **Never leave a game running** when you are done. `scripts/stop` ends every Hadron process,
  which includes a game somebody else started from Steam, so look first.
- **Do not kill a Wine process you did not start.** One wineserver serves every process of its
  prefix; killing the wrong one hangs someone's game for good. Read the process's prefix with
  `ps eww <pid>` (`WINEPREFIX`, `STEAM_COMPAT_DATA_PATH`) before you act.
- **The runtime Steam uses is live.** In a checkout, Steam runs `scripts/` and `dist/` as they
  are on disk, on whatever branch is checked out. While a game runs, do not install into
  `dist/`, do not switch branches, and change a launch script only by writing a new file and
  renaming it over the old one: a running shell reads its script by offset.
- **An agent cannot see the game.** Ask the person for what is on screen, how it feels and how
  it sounds, and do not take screenshots of their screen. Everything else (launching, sampling,
  reading logs, stopping) an agent can do itself.

## Where things are

- **Logs.** Each launch from Steam: `steamapps/compatdata/<app id>/hadron-run.log` in the Steam
  library. Each game session: `build/logs/<app id>-*.log` and its `.mem.log` in a checkout,
  `~/Library/Logs/Hadron` for the installed app. `scripts/report` (Save a report in the app)
  packs the latest of them, with the user's name removed.
- **Settings** are environment variables, and the launch scripts start Wine with a clean
  environment: a variable reaches the game only if `scripts/play` passes it on. From Steam they
  are a game's launch options as plain `NAME=value` words. Steam on the Mac does not run launch
  options through a shell, so `NAME=value %command%` fails with an OS error.
- **Another runtime for one test.** Copy the runtime, change the copy, and point
  `~/Library/Application Support/Hadron/runtime` at it (CONTRIBUTING.md, "Working without an
  Apple Developer account"). A game's prefix keeps copies of some libraries from when it was
  made, and its saves: copy a changed library into it, do not delete it.
- **What is known.** [docs/findings.md](docs/findings.md) is the numbered log of what was
  measured and fixed, with the open problems marked; [docs/test-games.md](docs/test-games.md)
  says how each game fares; [docs/roadmap.md](docs/roadmap.md) is the order of work. Read the
  entry before starting on something, and add one when you learn something the next person
  would otherwise have to find again.

## Upstream rules that bind an agent

Mesa wants AI-developed commits labelled and their comments and messages in a person's own
words, and takes no AI work at all under `src/asahi` and `src/gallium/drivers/asahi`. FEX accepts
no AI-written code, so Hadron's FEX patches stay downstream. Wine asks that nobody who has seen
Microsoft's source for something work on it. CONTRIBUTING.md has the details.
