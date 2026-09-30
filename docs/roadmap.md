# Roadmap and open decisions

## Order of work (agreed 2026-09-29)

1. FNaF fullscreen image offset (in progress; see docs/test-games.md).
2. Harden the MAP_JIT code buffers (Wine 0011, FEX 0005):
   - an off switch (e.g. `HADRON_FEX_MAP_JIT=0`) falling back to page flipping, for A/B testing;
   - a real nesting counter for write windows (thread-local storage crashed in FEX's Windows DLLs; use
     another per-thread slot, e.g. a TEB field);
   - a stress test in the regression suite hammering code writes, invalidation and relinking across threads.
3. Steam Play integration: games must be launched by Mac Steam for the lsteamclient bridge to work
   ("Timed out waiting for game mapping"). **Decision pending:** patch Mac Steam NotProton-style (re-sign
   Steam.app, inject a dylib, hook CCompatManager; fragile across Steam updates, gray area under the Steam
   Subscriber Agreement) versus Hadron shipping its own launcher. Needs the user's call.
4. Run the rest of the test game set (docs/test-games.md) to find the next general bugs.
5. Packaging: signed, notarized Hadron.app. Needs a Developer ID Application certificate and a Developer
   ID provisioning profile for com.broyojo.hadron.loader (only the team's Account Holder can create the
   certificate).

Later: hardware TSO limited to emulated code (a toggle costs ~0.27 us; enabling it on whole threads made
Portal unplayably slow); mtld3d/DXMT presenting through Wine's client surfaces (removes Wine 0012's special
case); D3D12 (vkd3d-proton on KosmicKrisp or DXMT's D3D12); the remaining ~50 ms steamclient retry cost
disappears with Steam Play integration.

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
- lsteamclient includes Steamworks-SDK-derived code (Valve's SDK licence) and NotProton overlays (GPLv3):
  fine to build locally; check both before redistributing binaries.
- Portal's Mac saves from January 2026 are in ~/Library/Application Support/Steam/steamapps/common/Portal
  and could be copied into the Windows version.
