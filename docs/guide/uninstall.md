# Uninstall

## 1. Take Hadron out of Steam

Do this first, while Hadron is still installed: it is Hadron that knows how to undo its setup.

- In Hadron's window, choose **Remove from Steam** (at the bottom), or
- run `hadron uninstall`.

This closes Steam, removes Hadron's library and its launch setting from `/Applications/Steam.app`,
signs Steam again, and removes the copies of Hadron's runtime that games ran from (except one a
game is still running from). Steam then starts without Steam Play. As with setup, macOS may ask you
to allow Hadron under **Privacy & Security → App Management**.

Steam stays signed by Hadron's ad-hoc signature, not Valve's. Reinstalling Steam from Valve
restores Valve's own signature.

## 2. Remove the app

- **Installed with Homebrew:** `brew uninstall --cask hadron`. This also removes the `hadron`
  command.
- **Installed from the disk image:** move Hadron from Applications to the Trash. If you had the
  window add the `hadron` command, remove that link too:

  ```sh
  sudo rm /usr/local/bin/hadron
  ```

## 3. Remove what Hadron kept (optional)

These folders stay after the app is gone:

| Folder | What is in it |
|---|---|
| `~/Library/Application Support/Hadron` | The [shared programs prefix](./windows-programs#the-shared-programs-prefix), with anything you installed in it, your [games.conf](./settings#per-game-settings-games-conf), and Valve's Windows Steam client files |
| `~/Library/Caches/Hadron` | Caches |
| `~/Library/Logs/Hadron` | Logs of game sessions and the memory logs |

With Homebrew, `brew uninstall --zap --cask hadron` removes all three.

Each Steam game's prefix, `steamapps/compatdata/<app id>` in your Steam library, belongs to
Steam's folders and is not removed by any of this. It holds the saves of games that do not keep
them in Steam Cloud, so move it somewhere safe before you delete it.
