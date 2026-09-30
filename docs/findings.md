# Findings

Root causes found while bringing up Hadron, and what fixed them. Most affect every game.

| # | Symptom | Root cause | Fix |
|---|---|---|---|
| 1 | Wine loader SIGKILLed at exec | arm64 macOS requires a 4GB `__PAGEZERO` | Wine patch 0001 |
| 2 | No memory below 4GB (`KUSER_SHARED_DATA`, 32-bit apps) | Native arm64 processes can't map low memory | `cross-architecture-support` entitlement; the loader claims the soft pagezero (Wine 0008) |
| 3 | RWX mappings fail | Apple silicon W^X | Flip exec+write pages RW/RX on fault (Wine 0002, 0006) |
| 4 | Random crashes after context switches | Darwin zeroes x18 (the TEB register) | `os_set_custom_x18_abi_enabled` per thread (Wine 0004) |
| 5 | Child processes fail with EFAULT | Thread stacks in the top page of the address space; XNU `copyinstr` checks the full maximum length | Keep allocations 16MB below the top (Wine 0005) |
| 6 | Steam updater thread dies; JIT overflows undetected | 4K guard pages share 16K host pages and end up writable | 64K guard regions in FEX (FEX 0002, 0003) |
| 7 | D3D11 has no Metal view | Upstream winemac lacks the interface DXMT expects | Export `macdrv_functions` (Wine 0007) |
| 8 | New prefixes can't run 32-bit programs | wineboot's 32-bit step runs before FEX is registered | Install FEX as `xtajit.dll`/`xtajit64.dll`, Wine's default emulator names |
| 9 | Portal stutters, then crashes out of memory | Wine's WoW64 OpenGL shadow-copies every mapped buffer into 32-bit memory (profiled: ~50% of wined3d's render thread in `memmove`) | D3D9 via mtld3d (Metal) |
| 10 | Portal freezes on mtld3d | mtld3d's crash reporter calls `dladdr()` on every SIGSEGV; under FEX faults are routine | `MTLD3D_NO_CRASH_HANDLER=1` |
| 11 | Portal unplayably slow with hardware TSO | TSO applies to all code on the thread, including native graphics drivers | Opt-in only; limit it to emulated code later (a toggle costs ~0.27us) |
| 12 | Steam bridge hangs ("Timed out waiting for game mapping") | Mac Steam only serves games it launched | Needs Steam Play integration |
| 13 | Portal black screen on mtld3d without `-novid`, later a crash | (a) A GDI paint of the game window before the intro hides the client view that mtld3d/DXMT present into, for good; (b) with the Steam bridge off, every retry of `SteamAPI_Init` loads and unloads Windows `steamclient.dll`, leaking a 32 MB reservation, and Source retries every frame during the intro | Keep external Metal views visible (Wine 0012); `scripts/play` disables Windows `steamclient.dll` when the bridge is off |
| 14 | Portal lag spike every few seconds | W^X flips on FEX's JIT code (two faults and two `mprotect`s per write/run alternation on a 16K page), triggered in bulk whenever FEX loads or links lots of code | MAP_JIT code buffers that FEX toggles itself (Wine 0011, FEX 0005) |

## Portal black screen and crash with the Bink intro (resolved, #13)

Two independent bugs, both hit only on the intro-video path.

**Black window.** mtld3d renders every frame and presents it (the F12 frame dump of the menu
is identical to a `-novid` run, occlusion-query counts included), yet the window shows black
from the first intro frame to the menu. Just before the intro, the game paints its window through
GDI once (full client rect, `(0,0)-(1280,720)`). winemac's window-surface flush then hides the
window's client view (`macdrv_surface_flush`: "the window may have been previously drawn with
client_surface ... hide the client_view"). GL and Vulkan show it again on every present through
`client_surface_present`; mtld3d and DXMT attach their Metal view through the `macdrv_functions`
table (Wine 0007) and present to the `CAMetalLayer` directly, so the view stays hidden. Evidence:
a temporary trace in that branch fired once, right after device creation, in the intro run and
never in a `-novid` run. Fix (Wine 0012): views created through `macdrv_functions` are counted
on their content view, and a flush leaves the client view alone while one exists. On Windows the
next `Present` covers the GDI paint anyway. This also covers DXMT (D3D11) windows.

**Crash.** With `HADRON_DISABLE_LSTEAMCLIENT=1` the game loads the Windows `steamclient.dll`,
which cannot reach a Steam client and is unloaded again. Each load leaves one 32 MB
`MEM_RESERVE`/`PAGE_NOACCESS` view reserved from code in Steam's `tier0_s.dll` (a temporary dump
of the address space at the first failure showed 110 such views and 120 MB free below 4 GB; the
guest return addresses were in `tier0_s.dll`). Source retries `SteamAPI_Init` every frame while
an intro video plays (every ~70 ms, the time a reload takes; after the menu it is every 5 s), so
the 4 GB of a large-address-aware 32-bit process is gone ~10 s into the intro. Then
`allocate_virtual_memory` fails ~1400 times for 32 MB, and the game crashes into Steam's crash
reporter. The earlier "execute fault at 4420D0F6" is plausibly the same thing: that address lies
in one of these leaked reservations (not verified). Fix: when the bridge is off, `scripts/play`
sets `steamclient,steamclient64=d`, so `SteamAPI_Init` fails at once ("Failed to load module")
and the game runs without Steam. That also ends the steamclient reload every 5 s that #14
describes.

Testing note: mtld3d skips presents while the window is fully occluded, so `scripts/shot` of a
covered game window shows a stale or black frame. Raise the window first (System Events: set the
process frontmost and `AXRaise` its window).

## Portal lag spikes (resolved, #14)

Profile: every few seconds a burst of 100-400 samples/s in `_sigtramp` → `segv_handler` →
`virtual_handle_fault` → `mprotect` on FEX JIT pages; quiet in between.

Trigger (`+timestamp,+loaddll`/`+virtual` traces): with no Steam to talk to (lsteamclient disabled),
the game's main thread loads the 21 MB Windows `steamclient.dll`, fails, and unloads it every 5 s,
for about 10 minutes. The unmap invalidates FEX's code for that range, so each reload runs its
init code again: FEX copies thousands of blocks back in from its disk cache (`LoadCachedCode`)
and relinks them (`ExitFunctionLink`). Each write followed by execution on the same 16K page
costs two faults and two `mprotect`s with TLB shootdowns: ~40k flips per reload. It also refills
the code buffer (~5 MB per reload), so after it has grown to 128 MB (in the first minute) FEX
clears its code cache every ~2 minutes. The spikes stop when the retries do. Code buffer growth
itself (16 → 128 MB in the first minute) is not the periodic trigger.

Fix: FEX's code buffers are MAP_JIT memory (per-thread W^X). FEX asks Wine for it with a
Wine-specific allocation attribute, `MEM_EXTENDED_PARAMETER_WINE_UNIX_JIT`, and switches the
thread to write mode (`pthread_jit_write_protect_np` via its unixlib) around every code write:
compile copy, disk-cache load, linking, delinking, backpatching. Wine places the MAP_JIT mapping
with an address hint into the view's range (no `MAP_FIXED` with `MAP_JIT`), never `mprotect`s it
(that fails on MAP_JIT memory), replaces pages that stop being exec+write with ordinary mappings,
and reports a fault in MAP_JIT memory loudly (`wine: write fault at ... in MAP_JIT memory`) rather
than flipping, since a toggle made in the signal handler is undone on sigreturn. Other exec+write
memory (x86 guest RWX pages, the dispatcher) keeps page flipping.

| | Fault-handler samples per second (15 × 1 s `sample`) | Main-thread stall per `steamclient.dll` reload |
|---|---|---|
| Before | bursts of 38-427 `_sigtramp` samples in 7 of 15 seconds, 0-4 in the rest | ~250 ms |
| After | 0 in all 15 seconds (0-3 `_sigtramp` from ordinary signals) | ~52 ms (steamclient's own init) |

Trade-offs: a write window costs two unix calls (~0.1-0.2 us each), paid per compile, cache load,
link or invalidation batch rather than per fault. A code write that misses a window is a crash,
so every write to a code buffer must be inside one. Left: the remaining ~50 ms stall per retry
disappears with working Steam; FEX never reclaims space from invalidated blocks, so code that is
repeatedly unmapped and reloaded still fills the buffer and forces full cache clears.
