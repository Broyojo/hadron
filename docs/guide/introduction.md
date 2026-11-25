# Introduction

Hadron lets the native Mac Steam client install and play Windows games on an Apple silicon Mac.
It is to the Mac what Proton is to Linux, and an open-source alternative to CrossOver for Steam
games. Once it is set up, you do not need to open Hadron to play: Windows games appear in your
Steam library and start from there like Mac games.

It can also run Windows programs that are not on Steam: see
[Windows programs outside Steam](./windows-programs).

## How it works, in one paragraph

Nothing runs under Rosetta. [Wine](https://www.winehq.org), which provides the Windows API, is
built natively for arm64. [FEX](https://github.com/FEX-Emu/FEX) runs inside Wine and translates
the game's x86 code (64-bit and 32-bit) to Arm. Direct3D, Vulkan and OpenGL are translated to
Metal; [What runs where](./graphics/) says which layer handles which API. The Mac Steam client
serves the game: sign-in, friends and cloud saves.

## What you need

- A Mac with Apple silicon.
- macOS 27. That is the only version Hadron supports.
- [Steam for Mac](https://store.steampowered.com/about/), installed in `/Applications`.

You do not need an Apple Developer account, Xcode or Homebrew to play. Homebrew is one of the two
ways to [install](./install) Hadron.

## Before you try it

- **It is unofficial.** Hadron is not affiliated with or endorsed by Valve, Apple, CodeWeavers or
  any of the projects it builds on.
- **It changes Steam.** Setting up adds a library to `/Applications/Steam.app` and signs that app
  again, which replaces Valve's signature on it. That is how Steam for Mac learns to offer Windows
  games. Removing Hadron from Steam takes it out, and reinstalling Steam from Valve restores
  Valve's signature. A Steam update can undo the setup; Hadron's **Repair** puts it back. Valve
  has said nothing about tools like this on the Mac either way, so use it at your own risk.
- **It is early.** A handful of games play well, and many do not start yet. The
  [list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) says how
  each one fares, and [HadronDB](https://hadrondb.com) is where reports of how games run are being
  collected.
- **First launches are slow.** A game's first start sets up its Windows environment and compiles
  its shaders, and the first start after a Hadron update compiles them again.
- **Nothing about you is sent anywhere.** There is no telemetry. The one request Hadron makes on
  its own is to GitHub, while its window is open, for the latest release's update feed. It names
  the version of Hadron that is asking and, as a browser would, your language; nothing else about
  you or your Mac. If something breaks, Hadron can save a report file for you to attach to an
  issue yourself.

## What Hadron is not

- **Not a way to run Mac games.** A game that has a Mac version is installed as that Mac version
  unless you tell Steam otherwise ([how](./steam-games#games-that-also-have-a-mac-version)).
- **Not for every game.** Games with kernel-level anti-cheat, which means most competitive
  multiplayer games, are not expected to work, and neither are games that need a third-party
  launcher such as the EA app or the Rockstar launcher. See [the FAQ](./faq#does-anti-cheat-work).
- **Not a virtual machine.** There is no copy of Windows inside. Wine reimplements the Windows API,
  and each game gets its own Windows folder structure (a *prefix*) that Hadron makes on its first
  launch.

## Next

[Install Hadron](./install), then read [Playing Steam games](./steam-games).
