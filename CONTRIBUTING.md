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
