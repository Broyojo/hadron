# Contributing to Hadron

## Reporting a game that does not work

Open an issue with the game and its Steam app ID, what happened (does not start, crashes, looks
wrong, runs slowly), your Mac and macOS version, and a report file: run `hadron report`, or choose
**Save a report** in Hadron. The report holds the game's logs and any shader Metal refused, with
your home folder's path, your user name and your Steam account taken out. That is done by
pattern, so look through the file before you attach it. Nothing is ever sent automatically.

[docs/test-games.md](docs/test-games.md) lists the games that have been tried.

## Where a change goes

Hadron is a set of upstream projects with patches, plus a small amount of its own code
(the [README](README.md#how-it-works) has the map):

- `scripts/` and `launcher/` are Hadron's own.
- `patches/<component>/` holds Hadron's changes to Wine, FEX, Mesa and the others, as
  `git format-patch` queues that `scripts/fetch.sh` applies to the revisions pinned in
  `sources.conf`.

Two rules shape every fix:

- **Fix the shared layer, not the game.** A game that fails shows a bug in Wine, a driver or the
  launcher; the fix goes there, where the next game benefits. There are no per-game hacks.
- **Drivers report only what they implement.** No forced feature levels and no relaxed checks to
  get a game past its startup test.

## Changing an upstream component

1. Make the change in `src/<component>` and commit it there.
2. Export the commits into the queue: `git format-patch` into `patches/<component>/`, numbered
   after the last patch.
3. Run `scripts/check-patches.sh`: every queue has to apply cleanly to its pinned revision and
   match the source tree. The checks on a pull request run the same thing.

A change to a graphics driver comes with the conformance result that shows it: `scripts/cts.sh`
runs the Khronos suites, and [docs/conformance.md](docs/conformance.md) records what passes.

## Working without an Apple Developer account

Playing needs no account: the release is signed. Building is different in one place. 32-bit
Windows programs need memory below 4GB, and macOS gives that only to a loader signed with an
entitlement that comes from a developer account
([docs/apple-developer-setup.md](docs/apple-developer-setup.md)). Everything else you can build
and change yourself, and run with the loader from a release:

1. Install a release of Hadron and set Steam up with it.
2. Copy its runtime. On APFS this takes no extra space:
   `cp -Rc /Applications/Hadron.app/Contents/SharedSupport/runtime ~/hadron-runtime`
3. Check out the release's tag, so that what you build matches the rest, and build the part you
   are changing with its script. `scripts/package-loader.sh` is the one step to skip.
4. Copy what you built into the copy, at the path the release has it:
   - Most scripts install into `dist/`. Copy from there to `~/hadron-runtime/dist`, but never
     onto the loader or its two links, `dist/bin/wine` and `dist/lib/wine/aarch64-unix/wine`: a
     plain `cp` writes through a link into the signed loader and breaks its signature. After
     building Wine, this copies everything else:
     `rsync -a --exclude=/bin/wine --exclude=/lib/wine/aarch64-unix/wine
     --exclude=/lib/wine/aarch64-unix/wine.app dist/ ~/hadron-runtime/dist/`
   - The Steam library is built to `build/notproton/notproton.dylib` and belongs at
     `~/hadron-runtime/steam/notproton.dylib`.
   - `scripts/` and `config/` go to the same names in the copy.
5. Run `~/hadron-runtime/scripts/steam-install`. Steam then runs games from the copy;
   `hadron repair` points it back at the app.

The signed loader accepts libraries it was not signed with, which is what makes this work.

A game's prefix (`steamapps/compatdata/<app id>` in the Steam library) keeps its own copies of
some libraries from the day it was made, mtld3d's `d3d9.dll` among them. After replacing one of
those, copy the file into the prefix as well (`scripts/mtld3d-prefix enable <prefix>` does it for
mtld3d). Do not delete a prefix to refresh it without moving it aside first: it also holds the
saves and settings of every game that does not keep them in Steam Cloud.

Without a release at hand, `scripts/build-wine.sh --dev` and `scripts/build-fex.sh --dev` build
a variant that needs no entitlement and runs 64-bit programs only
([README](README.md#running-and-debugging)).

## Using a coding agent

Hadron was built with one ([README](README.md#how-it-was-made)), and fixing an issue with one is
welcome; it is how most fixes here were found. What a game's report and `docs/findings.md` hold is
usually enough for an agent to reproduce a failure and look for its cause. It is your choice
either way, and a change is judged the same however it was written: by what was tested.
[AGENTS.md](AGENTS.md) holds what building Hadron taught about working on it safely; agents
read it on their own, and it is worth a person's ten minutes too. Two things to keep to:

- Say in the commit that an agent wrote it, with a `Co-Authored-By` trailer or the trailer the
  upstream asks for.
- The rules of the next section still apply. They rule out agent-written changes to some code.

## The upstreams' own rules

Patches are written so they could go upstream, so their projects' rules apply here:

- **Wine**: do not work on Wine code if you have seen Microsoft's source code for the same thing.
- **Mesa**: a commit developed with an AI tool carries the trailer `Generated-by: LLM` or
  `Assisted-by: LLM`, and its comments and commit message are a person's own words. Nothing
  under `src/asahi` or `src/gallium/drivers/asahi` is touched with AI tools at all.
- **FEX** accepts no AI-written code, so Hadron's FEX patches stay downstream.

## Pull requests

Keep one topic per pull request, describe what you tested and on which Mac, and say plainly what
you did not test. The licence of a contribution is the licence of what it changes: BSD 3-Clause
for Hadron's own code, the upstream's licence for a patch ([LICENSE](LICENSE)).
