# Windows programs outside Steam

Hadron also runs Windows programs that are not Steam games: an installer, a game from another
store's download, a tool. Steam is not needed for these.

## Ways to start a program

- **Open With:** right-click the `.exe` in Finder, choose **Open With**, then **Hadron**.
- **Drop** the `.exe` on Hadron's icon.
- **From Hadron's window:** choose **File → Open a Windows Program…** (⌘O), or the link **Open a
  Windows program that is not on Steam…**.
- **From a terminal:** `hadron run program.exe [arguments]` ([the `hadron` command](./install#the-hadron-command)).

Hadron does not make itself the app for `.exe` files. To open them with Hadron on a double-click,
select one in Finder, choose **Get Info**, pick Hadron under **Open with**, and click **Change
All…**.

A program started from the window or Finder keeps running when you quit Hadron. If it cannot start,
Hadron shows what went wrong.

## `hadron run`

`hadron run` starts the program from your terminal and waits for it. A console program's output
stays in the terminal, and the command returns the program's exit status. Everything after the
program's name goes to the program as its own arguments, including words that look like settings
(`FOO=bar` reaches the program). To give a program a setting, put it in front of the command:

```sh
HADRON_VSYNC=on hadron run ~/Downloads/game/game.exe
```

or give it an `exe:` line in your [games.conf](./settings#per-game-settings-games-conf). Ctrl-C
stops the program.

## The shared programs prefix

Unlike Steam games, which each get a prefix of their own, all programs outside Steam share one
prefix:

```
~/Library/Application Support/Hadron/programs/pfx
```

Its `C:` drive is the folder

```
~/Library/Application Support/Hadron/programs/pfx/drive_c
```

So a program that an installer puts there runs from there too: run the installer with Hadron, then
open the installed program's `.exe` from `drive_c` (for example under `drive_c/Program Files`)
with Hadron. A program installed in the prefix starts from its `C:` path, as its installer saw it.
Hadron does not yet keep a list of installed programs or add them to Steam; you find them in
`drive_c`.

`~/Library` is hidden in Finder. To get there, choose **Go → Go to Folder…** in Finder and paste
the path.

## How programs differ from Steam games

- A program starts by itself, not through Steam, so it does not see a running Steam client.
- The Steam bridge is not loaded.
- The memory watchdog guards it as it guards a game ([Troubleshooting](./troubleshooting#memory-warnings-and-stopped-games)).
- Logs: a program started from the window or Finder writes a log to `~/Library/Logs/Hadron`;
  `hadron run` keeps the output in the terminal unless you add `HADRON_LOG=1` in front. Hadron's
  own notes on each start go to `~/Library/Application Support/Hadron/programs/hadron-run.log`.
