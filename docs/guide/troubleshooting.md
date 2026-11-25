# Troubleshooting

Hadron is early, and many games do not start yet. Before you dig in, look the game up on
[HadronDB](https://hadrondb.com) and in the
[list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md): it may be a
known problem.

## Windows games have no Install button, or games stopped starting

A Steam update can remove part of Hadron's setup. Open Hadron: if its window says **Steam needs
repair**, choose **Repair** (or run `hadron repair`). Repair closes Steam while it works. The same
happens after some Hadron updates; the window then says *"Hadron was updated. Click Repair to
finish."*

`hadron status` tells you from a terminal whether Steam is set up.

## Setup fails with "Operation not permitted"

macOS did not let Hadron change Steam. Allow Hadron under **System Settings → Privacy & Security →
App Management**, then try again ([Install](./install#the-app-management-prompt)).

If setup says *"Steam already loads another library"*, another tool that hooks into Steam is set
up. Hadron leaves it alone; remove that tool from Steam first.

## A game installs as its Mac version

That is Steam's default for a game that has one. Force Hadron in the game's **Properties →
Compatibility** ([how](./steam-games#games-that-also-have-a-mac-version)).

## A game does not start

- **Give the first launch time.** It makes the game's prefix and compiles shaders; Direct3D 12
  games are especially slow the first time.
- **Check your launch options.** They must be plain `NAME=value` words without `%command%`
  ([Launch options](./steam-games#launch-options)).
- **Anti-cheat and launchers.** Games with kernel-level anti-cheat, Easy Anti-Cheat or BattlEye
  are not expected to work, and neither are games that need a third-party launcher such as the EA
  app or the Rockstar launcher.
- **A prefix from another tool.** If the game's prefix was made by another runner (CrossOver
  through NotProton, or an x86-64 Wine), Hadron moves it aside as `pfx.other-runner-<date>`, saves and all, and makes
  a new one. Your old files are in that folder.
- Then [save a report](#save-a-report) and send it.

## Black screen or wrong picture

- On a first launch a game can sit on a black or loading screen for a long time while its shaders
  compile. Wait a little before deciding it hangs.
- If Metal's shader compiler refuses a shader (Direct3D 12, Vulkan and OpenGL games), the driver
  saves it in `~/Library/Logs/Hadron/failed-msl`. Each one is a driver bug; a report includes the
  newest.
- A fullscreen picture cut off or covered by a black bar at a resolution of another shape than the
  screen's is a known problem in 0.3.3 ([Performance and display](./performance#resolution-and-fullscreen)).
- For a Direct3D 9 game, starting it once with `HADRON_D3D9=wined3d` shows whether the problem is
  in mtld3d or in the game's path through Wine. Say which in your report.

## The game crashes

Save a report right after the crash, before starting the game again: it holds the logs of the last
few launches.

## Memory warnings and stopped games

While a game runs, Hadron's watchdog checks the Mac's memory once a second.

- **A warning.** When the game's memory footprint passes half of your Mac's memory, swap grows by
  2 to 4 GB (depending on your Mac's memory), or memory pressure rises, macOS shows a notification,
  *"Hadron memory warning"*, once. The game keeps running.
- **Stopping the game.** Hadron stops the game only at an emergency: when macOS reports critical
  memory pressure, or when less than 1 GB of disk space is left. You then see *"Hadron stopped the
  game"* with the reason.

This protects the Mac: a game that fills unified memory fast enough can otherwise freeze it. You
can make the limits stricter with [settings](./settings#memory-watchdog). A memory log of each
session is kept next to its log, as `~/Library/Logs/Hadron/<app id>-<date>.mem.log`.

Keep some disk space free. FEX's cache of translated code lives in each game's prefix and can grow
to several GB for a large game.

## Save a report

A report is one zip file that holds what is needed to look at a problem:

- the Wine logs and memory logs of the last three launches,
- the end of the launch log of the two games played last, and of programs outside Steam,
- the newest shaders Metal refused, if any,
- your Hadron version, macOS version and Mac model.

Your home folder's path, your user name and your Steam account name and number are taken out. That
is done by pattern, so look through the file before you share it. Nothing is ever sent
automatically.

To save one:

- in Hadron's window, choose **Save a report…**, or
- run `hadron report` in a terminal. It saves `Hadron-report-<date>.zip` on your Desktop and prints
  its path; `hadron report <file.zip>` saves it elsewhere.

### Where to send it

[Open an issue on GitHub](https://github.com/Broyojo/hadron/issues/new) with:

- the game and its Steam app ID,
- what happened: does not start, crashes, looks wrong, runs slowly,
- your Mac and macOS version,
- the report file.

Questions, and games that work for you, fit better in
[GitHub Discussions](https://github.com/Broyojo/hadron/discussions) or on the
[Discord](https://discord.gg/ShNTcxWjKN). To tell others how a game runs, use
[HadronDB](https://hadrondb.com).

## Where the logs are

| What | Where |
|---|---|
| Each launch from Steam: what Hadron did before the game started | `steamapps/compatdata/<app id>/hadron-run.log` in your Steam library (by default `~/Library/Application Support/Steam/steamapps`) |
| Each game session: Wine's log and the memory log | `~/Library/Logs/Hadron/<app id>-<date>.log` and `.mem.log` |
| Programs outside Steam | `~/Library/Application Support/Hadron/programs/hadron-run.log`, and session logs in `~/Library/Logs/Hadron` |
| Shaders Metal refused | `~/Library/Logs/Hadron/failed-msl` |

A session log past 64 MB keeps its start and its end.
