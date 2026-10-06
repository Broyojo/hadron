# Roadmap and open decisions

## Where it stands (2026-10-05)

Done, in the order it was built: Wine native on arm64 with FEX inside it; Direct3D 9 (mtld3d) and
10/11 (DXMT) straight to Metal; Steam Play inside the Mac Steam client; Vulkan on Metal
(KosmicKrisp) with Direct3D 12 (vkd3d-proton) and OpenGL 4.6 (Zink) on it, checked against the
Khronos conformance suites ([conformance.md](conformance.md)); `Hadron.app` and the `hadron`
command around a self-contained runtime; a signed, notarized 0.1.0.

## Next

1. **Macs that did not build it.** One outside Mac has run 0.1.0 from the disk image so far,
   starting from a Steam with Valve's signature, and that evening found six things, all fixed
   ([test-games.md](test-games.md), [findings.md](findings.md) #24 to #27). More Macs will find
   more: a launch on a Mac that never had Rosetta is still to be seen.
2. **A game's first launch.** Creating its Windows prefix and running Steam's install scripts is
   the first thing a new user waits for: measure it, and seed new prefixes from a template.
3. **Unreal Engine 5.** Subnautica 2 does not load: Metal's compiler gives up on its largest
   compute kernels, 8-13 MB of generated MSL each ([test-games.md](test-games.md)). The failing
   shaders are saved by the driver, so this can be worked on without the game.
4. **Speed.** Nothing has been optimised yet. Teardown is held back by CPU translation, Geometry
   Dash drops frames while one core is at 100%, and macOS ends GPU work that runs about 50 ms
   while other processes wait (seen with six test processes; to be checked for a game in front).
   Portal loses a third of its frame rate while a failed portal shot's effect is alive, to lock
   waits between the engine's threads ([findings #25](findings.md)).
5. **Game controllers.** Wine's controller service starts in the packaged runtime; no controller
   has been tried in a game.
6. **Updates.** The window says when a newer release exists. Installing it in place (Sparkle), and
   the official Homebrew cask in place of the project's own tap.
7. **Steam updates.** Notice when a Steam update removes the setup and say so, instead of
   Windows games quietly losing their Install button until Hadron is opened.
8. **Direct3D 9 to 11 against a ground truth.** The Khronos suites say nothing about mtld3d and
   DXMT; Wine's Direct3D tests would.
9. **macOS 26.** Only macOS 27 is supported.

## Hardening left from earlier

- The MAP_JIT code buffers (Wine 0011, FEX 0005): an off switch falling back to page flipping,
  for A/B testing; a real nesting counter for write windows; a stress test hammering code writes,
  invalidation and relinking across threads.
- Ultimate Custom Night's frame-time dips in nights (findings.md #18), Portal's quit-time assert,
  and a clean mtld3d failure when a 32-bit process runs out of address space (it crashed on a
  null pointer in `LeaseCompletion::consume` with 79 MB left).
- Steam's overlay; replacing NotProton's "CrossOver options" panel with Hadron's own options; a
  Steam build the hook signatures do not cover should be reported, not fail silently.

## Later

External displays (games open on the main display; moving a running game across displays is
unreliable); lower vsync input latency (fewer queued drawables while display sync is on);
hardware TSO limited to emulated code (a toggle costs ~0.27 us; enabling it on whole threads made
Portal unplayably slow); mtld3d/DXMT presenting through Wine's client surfaces (removes Wine
0012's special case); sparse 3D textures and the other gaps left on purpose in
[conformance.md](conformance.md), when a game needs them.

## Facts established by experiment

- With the cross-architecture entitlement, even in x86_64 compat mode, macOS still refuses RWX memory and
  MAP_JIT keeps strict per-thread write/execute switching. There is no shortcut; CrossOver faces the same
  constraints (its ARM64 preview source isn't published; only CrossOver 26.3.0, the Rosetta build, is).
- `com.apple.security.cs.disable-executable-page-protection` doesn't help: W^X is enforced for native arm64
  processes regardless of the hardened runtime. Don't ship it.
- `thread_set_x86_64_compat(1)` enables hardware TSO (litmus test: 212 reorderings per 5M rounds → 0) and is
  idempotent.

## Licensing notes

- FEX's contribution rules (src/fex/CLAUDE.md) forbid submitting AI-generated code. All Hadron FEX patches
  are AI-written: keep them downstream, never submit them to FEX. Check Wine's and mtld3d's policies before
  proposing any upstream contribution.
- Building mtld3d's i686 DLLs needs Microsoft's MSVC CRT and Windows SDK (downloaded with xwin into
  toolchains/xwin only when `MTLD3D_ACCEPT_MSVC_LICENSE=1`). A build agent accepted that licence on this
  machine without asking; the user should confirm they're fine with it, or we switch to llvm-mingw's
  runtime.
- NotProton (the Steam-side integration, src/notproton + patches/notproton) is GPLv3; only its authors can
  relicense it (worth asking them about LGPL or dual licensing). Hadron's own code keeps its licence: the GPL
  component only executes scripts/steam-run as a separate program.
- lsteamclient includes Steamworks-SDK-derived code (Valve's SDK licence) and NotProton overlays (GPLv3).
  The SDK licence grants building software that uses Steamworks and redistributing the SDK's own
  `redistributable_bin`; it does not plainly grant redistributing a built lsteamclient. Proton forks ship
  one all the same. 0.1.0 carries it, with the licence named in its notices: a known grey area.
- A release carries every component's licence files and `THIRD-PARTY-NOTICES.md`
  (`scripts/package-runtime.sh`, from `packaging/third-party.conf`).
- Portal's Mac saves from January 2026 are in ~/Library/Application Support/Steam/steamapps/common/Portal
  and could be copied into the Windows version.
