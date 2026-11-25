# FAQ

## Which games work?

A handful play well, many do not start yet. The
[list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) has the
details: Portal and Portal 2 play fully; Five Nights at Freddy's, Ultimate Custom Night,
Subnautica, Geometry Dash and Astroneer run well, among others. [HadronDB](https://hadrondb.com)
is where reports from players are being collected: look a game up there, and report how it runs
for you.

## Is it free? Is it open source?

Yes. Hadron's own code is under the BSD 3-Clause license, and the projects it builds on keep their
own licenses ([LICENSE](https://github.com/Broyojo/hadron/blob/main/LICENSE)). The source is on
[GitHub](https://github.com/Broyojo/hadron).

## Is it made by Valve, or by CodeWeavers?

No. Hadron is not affiliated with or endorsed by Valve, Apple, CodeWeavers or any of the projects
it builds on. It is to the Mac what Proton is to Linux, but it is not Proton.

## Does it use Rosetta?

No. Wine runs natively on arm64, and FEX translates the game's x86 code inside it. A game's Intel
Mac version, if Steam installs that instead of the Windows one, is a different matter: it runs
through Rosetta, without Hadron.

## Does anti-cheat work?

Generally not. Games with kernel-level anti-cheat, which means most competitive multiplayer games,
are not expected to work, and games protected by Easy Anti-Cheat or BattlEye are out of scope for
now. Online games without such anti-cheat can work: Among Us runs, and its online sign-in works.

## Do games need a launcher like the EA app?

Games that need a third-party launcher, such as the EA app or the Rockstar launcher, are not
expected to work for now.

## Do my saves carry over? Does Steam Cloud work?

Steam Cloud works: the Mac Steam client syncs a game's cloud files, and Hadron lays out each
prefix the way Proton does, so the paths Steam Cloud uses match. Saves a game keeps outside Steam
Cloud stay in its prefix, `steamapps/compatdata/<app id>/pfx` in your Steam library.

## Do controllers work?

Not known yet. Wine's controller service starts, but no controller has been tried in a game.

## Does it change Steam?

Yes. Setting up adds a library to `/Applications/Steam.app` and signs that app again, which
replaces Valve's signature on it. **Remove from Steam** in Hadron's window (or `hadron uninstall`)
takes it out, and reinstalling Steam from Valve restores Valve's signature.

## Can I update Hadron while a game is running?

Yes. A running game keeps the version it started with, and the next game you start uses the new
one ([Updates](./install#updates)).

## Does Hadron send anything about me?

No. There is no telemetry. While its window is open, Hadron asks GitHub for the latest release's
update feed, naming its own version and your language. Reports are files you save and share
yourself.

## Can I run Windows programs that are not on Steam?

Yes: open the `.exe` with Hadron, or run `hadron run program.exe`
([Windows programs outside Steam](./windows-programs)).

## Which macOS versions are supported?

macOS 27 on Apple silicon only.

## Where do I ask a question?

In [GitHub Discussions](https://github.com/Broyojo/hadron/discussions) or on the
[Discord](https://discord.gg/ShNTcxWjKN). Bugs go to
[GitHub issues](https://github.com/Broyojo/hadron/issues), with a report
([how](./troubleshooting#save-a-report)).
