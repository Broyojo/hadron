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
| 15 | FNaF fullscreen image shifted down/right and cropped | A display mode set while the app is still inactive (a game launched from a terminal) is deferred to activation, but win32u re-reads the old mode and monitor rect right after the call succeeds, so the fullscreen window is sized for the old desktop while the back buffer has the new size | Report the pending mode at once (Wine 0013) |

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

## FNaF fullscreen offset (resolved, #15)

Symptom: fullscreen FNaF showed its menu shifted down/right with black bands at the top and
left, cropped at the right and bottom. The Metal view/client-surface frame was not the cause:
traces (`trace+macdrv,trace+system,trace+display`) show the view covering the window throughout.

What happens: FNaF asks for a 1280x800 fullscreen back buffer. mtld3d calls
`ChangeDisplaySettings(1280x800, CDS_FULLSCREEN)`, which succeeds, then sizes the window to the
monitor rect. But the Wine app is not active yet (it was launched from a terminal, and activation
is asynchronous), and in that case winemac's `-[WineApplicationController setMode:forDisplay:]`
only records the mode in `latentDisplayModes` and sets it on the next activation. win32u re-reads
the displays right after the call and gets the old mode back (`add_modes current 1512x982`,
monitor `(0,0)-(1512,982)`), so mtld3d sized the window to 1512x982 ("honoring the requested
1280x800 back buffer without a mode-set; the window covers the monitor (1512x982)"). The Clickteam
runtime then centered its 1280x720 frame child for a 1512x982 client, at `(116,131)`, and drew
at that offset into the 1280x800 back buffer, which present scaled to the window: the offset
seen on screen (136x161 pt = 116x131 scaled by 1512/1280 and 982/800) and the crop. When the app
was activated a moment later the display really switched to 1280x800, but nothing resized the
window again.

Fix (Wine 0013): while a mode is pending, winemac reports it as the display's current mode and
sizes the monitor rect to it, as on Windows, where the mode is in effect when
`ChangeDisplaySettings` returns. `screen_covered_by_rect` also treats a window covering a screen
in its pending mode as fullscreen, so Cocoa does not push the 1280x800 window below the menu bar
before the mode is set. Activation then only carries out the change, and the window, back buffer
and display agree: FNaF's 1280x720 frame is centered with 40-pixel bands above and below, like
on Windows. Any game that changes the display mode at startup benefits, whatever its renderer.

Note: a real mode change captures the display (Wine's normal behavior): other apps are hidden
until the game quits or Cmd-Tab releases the display. Testing: `scripts/shot` skips windows
above the normal window level, and captured-display game windows are raised above it.

## Ultimate Custom Night drawn in the top-left quarter (resolved, #16)

The menu filled only the top-left quarter of its window on mtld3d. The back buffer was full size
(Reset to 1512x982) and the drawable follows the layer, but the Metal view stayed at 756x491 pt,
the window's size when the device was created; the game grows its window afterwards. Wine 0007
created the client surface behind the view with `macdrv_CreateClientSurface` and never registered
it with win32u, so `update_client_surfaces` never resized it on window moves (and it leaked).

Fix (Wine 0014): winemac takes the surface through `get_unused_client_surface` and
`use_window_client_surface`, as win32u's Vulkan surfaces do (both now exported to drivers), keeps
it in the window data and releases it on `DestroyWindow`. win32u then keeps the view in step with
the client area and disposes of it with the window; a later device on the same window presents
the kept surface again. DXMT gets the same fix. Known limit: a window that already has a GL or
Vulkan view keeps using that one. What remains of the cleanup below is the per-present
notification, which would retire 0012 and remove that limit.

## Ultimate Custom Night crash on GO (resolved, #17)

Pressing GO crashed in mtld3d (`LeaseCompletion::consume`, a null read) with 79 MB of address
space left. The exe is 32-bit and not large address aware, so it had 2 GB; mtld3d's staging and
retention alone reached several hundred MB. Wine 0015 adds `WINE_LARGE_ADDRESS_AWARE=1`, like
Proton's default `PROTON_FORCE_LARGE_ADDRESS_AWARE`, and `scripts/play` sets it
(`HADRON_LARGE_ADDRESS_AWARE=0` to opt out). mtld3d should still fail cleanly rather than crash.

## Ultimate Custom Night lag (resolved, #18)

The menu ran at 12 fps. Two causes, both general:

**System-memory vertex buffers (mtld3d 0001).** Clickteam's runtime keeps its sprites in one
384 KiB `D3DPOOL_SYSTEMMEM` vertex buffer and, for each of ~1000 sprites a frame, locks the whole
buffer (offset 0, size 0, `NOOVERWRITE`), writes one quad and draws it. mtld3d uploaded all
384 KiB at every Unlock (65 ms of an 83 ms frame, and the copies filled the retention cap). A
Windows runtime processes system-memory buffers in software: each draw copies the vertices it
names. mtld3d now records such draws as `Draw*PrimitiveUP` with only those vertices (and rebased
indices) copied into the frame scratch; the buffer is uploaded only if a draw reads it on the GPU.

Two earlier attempts at this failed and are worth remembering. Uploading each draw's range into
the device buffer made every upload overlap the range the previous indexed draw is assumed to
read (from its base vertex to the end of the buffer), so each one renamed the buffer with a
whole-buffer GPU copy: ~18 GB/s of copies, which starved unified memory and froze the whole Mac
(it had to be force-restarted). Passing the draw's vertex window to the encoder fixed that, but
each upload still cost a page-aligned staging allocation (16 KiB for ~100 bytes), and in a night
those filled the 32-bit address space (`page boxes 2416 MiB`) until mtld3d panicked. Watch
`vbib_gpu_copy_bytes_total` and the address-space warnings after any change to upload paths.

**D3DX's `DebugSetMute` lookup (mtld3d 0002).** The statically linked D3DX library looks up
`DebugSetMute` in `d3d9d.dll`/`d3d9.dll` before its calls and caches only a found pointer.
mtld3d didn't export it, so every D3DX call did `GetModuleHandle` twice, `LoadLibrary` and
`GetProcAddress`: ~40% of the game thread, found by mapping samples through FEX's block map
(`FEX_BLOCKJITNAMING=1 FEX_DISKCACHE=0`, then `tools/fexprof.py`). A lookup cache in Wine's
loader helped less than the export and was dropped (`build/logs/wine-loader-cache.diff`).

Result: the menu holds 60 fps, the game's own fixed rate (Clickteam titles run at the frame rate
their developer set, whatever the display), with no staging uploads or retention; nights mostly
hold 60 with some 20-33 ms stretches. Every Clickteam title and anything with static D3DX gains.

`scripts/play` now starts `scripts/watchdog`, which logs memory once a second to
`build/logs/*.mem.log` and stops the game on critical memory pressure, swap, a memory cap or low
disk, so a runaway can't take the Mac down again.

## Fullscreen blacked out other displays (resolved, #19)

A game that changes the display mode (FNaF, UCN) blacked out an external monitor: winemac
captured every display with `CGCaptureAllDisplays`. Windows changes one monitor's mode and leaves
the others alone. Wine 0016 captures only the display being changed and releases it when its
mode is restored. Using a window on the other screen still needs Cmd-Tab, much as clicking another
monitor minimizes an exclusive-fullscreen game on Windows.

## Screen tearing: vsync the way Windows does it (resolved, #20)

Every game could tear: mtld3d and DXMT both set Metal display sync off (`displaySyncEnabled =
false`) and treated a vsync request as a frame-rate throttle only. Both now follow Windows:

- A fullscreen device follows the game: D3D9's presentation interval, DXGI's sync interval.
  Vsync on waits for a display refresh and never tears; vsync off presents at once.
- A windowed device is composited by DWM on Windows, at a refresh, whatever it asks for, so it
  syncs here too. Source ignores its own vsync setting in a window for that reason. DXGI's one
  exception is honored: a windowed game that presents with sync interval 0 and
  `DXGI_PRESENT_ALLOW_TEARING` (Unity does, with vsync off) tears, as on Windows.
- `HADRON_VSYNC=game|on|off` (default `game`) overrides the game for titles without a vsync
  setting; `scripts/play` maps it to mtld3d's `present.vsync` and DXMT's `dxgi.vsync`.

Checked by eye: FNaF (asks for immediate, fullscreen) stops tearing with `HADRON_VSYNC=on`;
Portal's "Wait for vertical sync" re-paces the layer live in fullscreen; Among Us tears with its
vsync off and not with it on. Vsync adds some input latency, as on Windows; fewer queued
drawables while display sync is on would reduce it (next step).

## Steam Play in Mac Steam (prototype working, #21)

Hadron runs as a compatibility tool inside the macOS Steam client, like Proton on Linux:

- **Steam side: NotProton** (GPLv3, src/notproton, one patch in patches/notproton). A library loaded into
  `steam_osx` through Steam.app's LSEnvironment (`scripts/steam-install`) hooks `CCompatManager` and
  friends by string anchors plus byte patterns: Steam Play is force-enabled for every title, Windows-only
  games default to our tool, and Steam downloads their Windows depots. The tool is registered in
  `compatibilitytools.d/notproton` (its directory name must contain "proton" or Steam skips AutoCloud's
  Windows path mapping; it displays as "Hadron"). Steam build 1788652215: 17/17 signatures resolve, 8/8
  steamclient and 2/2 steamui hooks install.
- **Launch side: Hadron's own** `scripts/steam-run`, which the tool's `run` stub execs from
  ~/Library/Application Support/Hadron/runtime. Per game it creates `compatdata/<appid>/pfx` (with mtld3d,
  and with Mono/Gecko installs suppressed so wineboot doesn't wait on a hidden dialog), stages Valve's
  Windows client DLLs (fetched from Valve's CDN by `scripts/build-steam-play.sh`, never redistributed),
  runs installer helpers directly and the game through `scripts/play`, and turns Steam's Stop into
  `wineserver -k`.

Result: Among Us, Windows-only, shows Install/Play in the Mac library, installs its Windows depot, runs
its install script through the tool, launches, and signs in online (the manual-launch
`SteamworksAuthFail` is gone): launched by Steam, the lsteamclient bridge gets its game mapping.

Steam Cloud: Steam maps Cloud paths into `users/steamuser`, and still names XP-era folders ("Local
Settings/Application Data"). Steam launches run Wine as `USER=steamuser` (Wine names the profile after
$USER, as Proton's patch does), `steam-run` aliases the XP paths onto `AppData/Local`, `AppData/Roaming` and
`Documents` (merging anything Steam wrote there first), folds prefixes made under the Mac user's name into
`steamuser`, and makes the profile's Documents, Desktop and so on real folders inside the prefix, so each
game's profile stays there as under Proton. Wine links a shell folder to the Mac user's only when it
doesn't exist yet (shell32 `_SHGetUserProfilePath`), so real folders stay put: the layout runs once per
prefix (marker `.hadron-profile-v1`). A first version redid it on every launch, and Wine re-linked the
folders each time.

A black screen and slow launches while testing this turned out to be VS Code: its search followed the
prefixes' `dosdevices/z:` link to `/` and crawled the whole disk with 36 ripgrep processes (load average
113), which took every Wine start from 2 s to about 20 s. `.vscode/settings.json` now keeps search and
the file watcher out of prefix/, build/, dist/, src/, games/ and toolchains/, and off symlinks.

Bugs found on the way: a path with a space in `STEAM_DYLD_INSERT_LIBRARIES` split `play`'s `env` call;
the watchdog missed processes started through the symlinked runtime (lsof reports real paths).

## Vulkan on Metal: KosmicKrisp through Wine, vkd3d-proton blocked on two driver features (#22)

KosmicKrisp (Mesa main, `scripts/build-vulkan.sh mesa`) reports Vulkan 1.4 on the M2 Pro, and Wine's
winevulkan reaches it through Homebrew's Khronos loader with `VK_DRIVER_FILES` pointing at its ICD JSON:
an x86_64 program under FEX (`tools/vkprobe.c`) sees the GPU with 142 device extensions. With the
default MoltenVK the same program gets `VK_ERROR_INCOMPATIBLE_DRIVER` from `vkCreateInstance`.

vkd3d-proton 3.0.1 (`scripts/build-vulkan.sh vkd3d-proton`, x86_64 PE for now; llvm-mingw's libc++
needs `<new>` and `<exception>` force-included) first crashed in `vkGetPhysicalDeviceProperties2`.
That was Wine's generated thunk: `VkPhysicalDeviceLayeredApiVulkanPropertiesKHR` (maintenance7) holds
a `VkPhysicalDeviceProperties2`, and make_vulkan skipped its input conversion because the member is
returnedonly, so its pNext was uninitialized and the output conversion followed it. Wine patch 0017
converts extensible returnedonly members on input; upstream master has the same bug. MoltenVK
doesn't expose maintenance7, so nothing hit it before.

With that, `D3D12CreateDevice` (`tools/d3d12probe.c`) fails on vkd3d-proton's hard requirements that
KosmicKrisp doesn't meet yet:
- transform feedback (`VK_EXT_transform_feedback`, D3D stream output; Metal has none, so the driver has
  to emulate it, as Asahi's Honeykrisp does);
- single-texel alignment for texel buffer views (KosmicKrisp requires 16 bytes; D3D12 typed buffer
  views may start at any element, which a driver can support by folding the remainder into the shader's
  index, again as Honeykrisp does).

Everything else vkd3d-proton requires is there (Vulkan 1.3, 1M update-after-bind descriptors,
robustness2 with nullDescriptor, push descriptors, mutable descriptors, maintenance5/6, zero instance
divisors, samplerMirrorClampToEdge, shaderDrawParameters). Also missing, but not hard requirements:
geometry shaders (Mesa MR !44786 pending), sparse resources (caps D3D12 at feature level 11_1 without
an override), `VK_EXT_dynamic_rendering_unused_attachments`. vkd3d-proton also needs DXVK's dxgi.dll
for swapchains, which conflicts with DXMT's; DXGI will have to be picked per API.

## Future cleanup: Metal renderers and Wine's client surfaces

Wine patches 0007 and 0012 attach mtld3d/DXMT to a window through the CrossOver-style
`macdrv_functions` table: 0007 creates a client surface so a view exists, and the renderers then
draw into their own Metal layer without telling Wine when they present. 0012 compensates by keeping
that view visible while an external Metal view exists. Upstream Wine's GL and Vulkan paths instead
notify Wine on every present through its client-surface interface, so visibility is always right.
The principled fix is to have mtld3d and DXMT present through Wine's client surfaces (a small
per-present notification), removing 0012's special case and its edge cases (GDI drawing after a
device is destroyed while the view survives; games mixing GDI and 3D in one window). Needs
coordination with the mtld3d and DXMT developers.
