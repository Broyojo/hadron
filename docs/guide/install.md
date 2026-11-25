# Install

You need an Apple silicon Mac with macOS 27 and [Steam for Mac](https://store.steampowered.com/about/)
in `/Applications`.

## 1. Get the app

**With [Homebrew](https://brew.sh):**

```sh
brew install --cask broyojo/hadron/hadron
```

This installs Hadron.app and the `hadron` command.

**Or with the disk image:** download it from the
[latest release](https://github.com/Broyojo/hadron/releases/latest), open it, and drag Hadron to
Applications.

## 2. Set up Steam

Open Hadron. Its window says how the Steam setup stands and offers what to do next:

- **Set up Steam Play** with a **Set up Steam** button: Hadron is not in Steam yet.
- **Steam is needed** with a **Get Steam** button: Steam is not installed in `/Applications`.
  Install it, then choose **Check again**.

Choose **Set up Steam**. Setup closes Steam while it works, then adds Hadron to it as a Steam Play
tool. On a first setup it also downloads Valve's Windows Steam client files from Valve's servers,
which games need to talk to Steam (Hadron does not ship them).

### The App Management prompt

The first attempt is usually stopped by macOS, because Hadron is changing another app. Hadron's
window then says *"macOS did not let Hadron change Steam"* and offers **Open Privacy &
Security**. Allow Hadron under **System Settings → Privacy & Security → App Management**, then
choose **Set up Steam** again.

When the window says **Ready to play**, you are done. Hadron does not need to stay open: open
Steam, and install and play Windows games from your library as usual. The setup shows in Steam
under **Settings → Compatibility** as Steam Play.

From a terminal, `hadron setup` does the same as the button.

## The `hadron` command

Everything the window does is also a command:

| Command | What it does |
|---|---|
| `hadron setup` | Add Hadron to Steam as a Steam Play tool (closes Steam while it works) |
| `hadron repair` | The same: put the setup back after a Steam update or after moving Hadron |
| `hadron uninstall` | Take Hadron out of Steam again |
| `hadron status` | Say whether Steam is set up |
| `hadron report [file.zip]` | Save a report file for a GitHub issue ([Troubleshooting](./troubleshooting#save-a-report)) |
| `hadron run <program.exe> [arguments]` | Run a Windows program that is not a Steam game ([more](./windows-programs)) |
| `hadron update` | Install the latest version of Hadron, if this is not it |
| `hadron version` | Print Hadron's version |
| `hadron help` | List the commands |

Without a command, `hadron` opens the window.

**Homebrew** installs the command for you. **With the disk image**, nothing runs when you drag the
app to Applications, so Hadron's window shows **Add the hadron command for Terminal…** instead. It
puts a link named `hadron` into `/usr/local/bin`, which belongs to the system, so macOS asks for
your password. The link is only offered while there is no `hadron` command yet.

## Updates

Hadron updates itself. When you open its window and a newer version exists, it shows what is new
and asks before installing. To look at once, choose **Hadron → Check for Updates…** in the menu
bar, or run `hadron update` in a terminal. The download is checked against a signature before
anything is installed, and nothing is installed unasked.

With Homebrew, `brew upgrade --cask hadron` also works. A plain `brew upgrade` leaves Hadron to
update itself.

Updating is safe while a game is running. Games run from a copy of Hadron's runtime kept in your
Library, so the running game is left as it is, and the next game you start uses the new version.

### Repair

Hadron's window asks for **Repair** when the setup inside Steam needs to change:

- *"Hadron was updated. Click Repair to finish. This closes Steam."* An update changed the part of
  Hadron that sits inside Steam. Most updates do not.
- *"Part of the setup is missing, which a Steam update can cause."* A Steam update replaced part
  of the setup. Until you repair, Windows games can lose their Install button.
- *"Steam is set up for another version of Hadron"* or *"…for another copy of Hadron"*: Steam
  was set up by a different version or copy of Hadron, or you moved the app.

**Repair** (or `hadron repair`) closes Steam and puts the setup back. The button is in the window
when everything is fine, too.

## Next

[Play a Steam game](./steam-games). To remove Hadron later, see [Uninstall](./uninstall).
