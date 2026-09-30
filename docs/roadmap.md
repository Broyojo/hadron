# Roadmap and open decisions

## Order of work (agreed 2026-09-29)

1. Ultimate Custom Night: done bar the frame-time dips in nights (docs/findings.md #18). Next small items: Portal's
   quit-time assert (worker threads terminated at exit) and a clean mtld3d failure when a 32-bit process runs out of
   address space.
2. Harden the MAP_JIT code buffers (Wine 0011, FEX 0005):
   - an off switch (e.g. `HADRON_FEX_MAP_JIT=0`) falling back to page flipping, for A/B testing;
   - a real nesting counter for write windows (thread-local storage crashed in FEX's Windows DLLs; use
     another per-thread slot, e.g. a TEB field);
   - a stress test in the regression suite hammering code writes, invalidation and relinking across threads.
3. Steam Play integration (decided 2026-09-30: inside the macOS Steam client, NotProton-style; never the
   Windows Steam client under Wine). **Prototype works:** Among Us installs and launches from the Mac Steam
   library through Hadron, and its online sign-in succeeds through the bridge (docs/findings.md #21). Next:
   - per-game `.app` wrappers so games get Game Mode and a Dock icon, and the overlay shim for Steam's overlay;
   - replace NotProton's "CrossOver options" panel with Hadron's options (vsync, D3D9 backend, HUD);
   - first-launch prefix creation takes minutes; seed new prefixes from a template instead;
   - Steam updates: detect a build the signatures don't cover and say so instead of failing silently.
4. Vulkan on Metal (KosmicKrisp in Mesa, or MoltenVK) wired into Wine, then vkd3d-proton on it for D3D12
   and Zink for OpenGL beyond Apple's 4.1. Teardown needs one or the other (docs/test-games.md).
5. Run the rest of the test game set (docs/test-games.md) to find the next general bugs.
6. Distribution: a Hadron.app that installs the runtime into ~/Library/Application Support/Hadron and the
   Steam integration into Steam.app (what scripts/steam-install does), notices when a Steam update undoes the
   injection and re-applies it, and offers a small GUI: repair, per-game settings, logs, and self-updates
   (Sparkle). Signing: a Developer ID Application certificate and a Developer Needs a Developer ID Application certificate and a Developer
   ID provisioning profile for com.broyojo.hadron.loader (only the team's Account Holder can create the
   certificate).

Later: external displays (games open on the main display; moving a running game across displays is
unreliable); lower vsync input latency (fewer queued drawables while display sync is on); mtld3d should fail an allocation cleanly when a 32-bit process runs out of address space (it
crashed on a null pointer in `LeaseCompletion::consume` with 79 MB left); hardware TSO limited to emulated code (a toggle costs ~0.27 us; enabling it on whole threads made
Portal unplayably slow); mtld3d/DXMT presenting through Wine's client surfaces (removes Wine 0012's special
case); D3D12 (vkd3d-proton on KosmicKrisp or DXMT's D3D12); the remaining ~50 ms steamclient retry cost should be gone for
Steam launches (re-measure).

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
- lsteamclient includes Steamworks-SDK-derived code (Valve's SDK licence) and NotProton overlays (GPLv3):
  fine to build locally; check both before redistributing binaries.
- Portal's Mac saves from January 2026 are in ~/Library/Application Support/Steam/steamapps/common/Portal
  and could be copied into the Windows version.
