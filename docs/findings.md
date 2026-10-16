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

## Vulkan on Metal: KosmicKrisp through Wine, D3D12 through vkd3d-proton (#22)

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

Teardown through this stack (`HADRON_D3D12=vkd3d`, which installs vkd3d-proton and DXVK's dxgi.dll into
the prefix): with the missing-feature checks in vkd3d-proton and DXVK relaxed and feature level 12_0
forced, all as local diagnostics only, it creates its device and swapchain and renders sky, lighting,
water and HUD, but not the voxel world. On the way, KosmicKrisp asserted writing a sampled-image null
descriptor past the end of a mutable set: vkd3d-proton's null-descriptor template wrote the requested
null type into every mutable set, including sets whose type list can't hold it, and KosmicKrisp sizes
mutable descriptors by their list (16 bytes in the raw buffer set, 64 for a sampled image).
vkd3d-proton patch 0001 writes each set a null type it can hold. DXVK's dxgi skips the GPU unless it has
DXVK's D3D11 features (fillModeNonSolid, geometryShader), though vkd3d-proton only presents through it.

vkd3d-proton's own test suite (`tests/d3d12.exe`, built with `-Denable_tests=true`) runs on KosmicKrisp
through Wine and maps the remaining driver work. First findings: `SampleLevel` at LOD exactly 0.5/1.5
picks the lower mip (D3D rounds up; Vulkan allows either), one depth-compare sampling case is wrong, and
creating a pipeline statistics query pool asserted (KosmicKrisp supported only occlusion and timestamp
queries; D3D12 requires pipeline statistics; added in patch 0019, below).

Teardown's voxels are drawn GPU-driven: ExecuteIndirect with per-object root CBV, vertex and index
buffers. vkd3d-proton implements that with VK_EXT_device_generated_commands, which KosmicKrisp lacked,
so it dropped the per-draw state and the voxels vanished. Mesa patch 0001 adds the extension (behind
`MESA_KK_EXPERIMENTAL=dgc`, which `play` sets): at execute time a compute kernel (libkk `kk_dgc.cl`)
writes one copy of the root descriptor table per sequence with its push constants, sequence index and
vertex buffer bases patched in, and the draw or dispatch arguments (zeroed past the count buffer or
under failed predication); per-sequence index buffers are copied into the device heap by poly's unroll,
since Metal takes an index buffer only from the CPU; the CPU then encodes one indirect draw per sequence
with its own root bound. Preprocessing is a no-op because KosmicKrisp re-records command buffers on
resubmit, so the work has to happen at execute. vkd3d-proton patch 0002 stops requiring DGC support for
geometry/tessellation stages the device doesn't have. `tools/dgc-test/run.sh [2]` tests the three
signature shapes Teardown uses, natively against the driver. The bug that took longest: non-indexed
sequences were issued as indexed draws with whatever index buffer was bound (the draw path decides on
the index size), which drew Teardown's boxes with stale indices, as spikes and half faces.

With DGC, Teardown renders correctly on D3D12 (still with the diagnostics: relaxed transform-feedback and
texel-alignment checks, forced feature level 12_0). Remaining driver work for an honest device:
transform feedback, single-texel buffer alignment, sparse resources (feature level 12_0), pipeline
statistics queries, timestamp query pools above Metal's 4096-entry counter heaps, and geometry shaders.

D3D12 games launch from Steam like any other. DXGI isn't swapped per game any more: DXMT's dxgi.dll
(DXMT patch 0002) hands swapchain creation for devices that aren't its own, such as vkd3d-proton's
command queues, to DXVK's DXGI, which `play` installs in the prefix as dxgi_dxvk.dll. DXVK patch 0001
lets a DXGI-only DXVK build list the GPU without DXVK's D3D11 feature requirements, which don't apply to
a build that can't create D3D devices. `HADRON_D3D12=vkd3d` now only selects vkd3d-proton's d3d12.dll,
and `config/games.conf` sets it for Teardown. steam-run applies per-game settings from that file and
from the user's ~/Library/Application Support/Hadron/games.conf; launch options win over both.

Geometry shaders: Mesa patches 0004-0009 carry the pending upstream KosmicKrisp geometry shader
merge request (!44786, by its author, one conflict resolved against DGC), which implements them with
poly as compute passes before the draw. vkd3d-proton's geometry shader, layered rendering, topology
mismatch and PS layer tests pass (527 checks; test_primitive_id_read_tess_geom has one failure).
Patch 0010 lists the geometry stage for DGC: with geometryShader on, vkd3d-proton turns DGC off
unless every graphics stage is supported, which briefly made Teardown's voxels vanish again.

Vulkan CTS 1.4.6.2 (built in build/vk-gl-cts, run natively against KosmicKrisp): dEQP-VK.geometry.*
passes 181/181 supported. dEQP-VK.dgc.ext.* (execution sets aren't exposed, so most cases are NotSupported) had two KosmicKrisp
bugs with tessellation or a geometry shader:

- The geometry heap was reset at every render pass split, but DGC unrolls every sequence's indices into
  it before drawing; a later sequence's vertex shader outputs overwrote earlier-unrolled indices. The
  heap now lives for the whole draw command (Mesa patch 0012). Plain multi-draws that unroll had the same
  latent bug.
- Indirect tessellation dispatched its VS and TCS grids, written in threads, as threadgroup counts of 64,
  so surplus threads wrote other instances' vertex slots (an intermittent race) and past the allocation.
  They now use exact-thread dispatch (patch 0013); this affects all indirect tessellation draws.

dEQP-VK.dgc.ext, geometry and tessellation together: 1100 pass, 0 fail. Excluded: the DGC *_lib variants,
which build pipeline libraries on a device without VK_EXT_graphics_pipeline_library (a CTS bug; KK then
dereferences the missing input assembly state), and
dEQP-VK.tessellation.geometry_interaction.limits.output_required_max_geometry, which hangs the GPU on the
driver as it was before these changes too. Open.

Transform feedback (Mesa patch 0014, `MESA_KK_EXPERIMENTAL=xfb`, on by default in `scripts/play`) is
built on poly's geometry shader emulation, which already wrote transform feedback from the GS
rasterization pass; KosmicKrisp threw away the two helper shaders it needs. The pre-GS pass (one
thread per draw) clamps each stream's primitive count to the bound buffers and advances the offsets and
query counters; a GS whose output count varies also needs its count shader and a prefix sum over the
counts. A VS or TES that captures output gets a passthrough GS (poly's generator), inserted before
linking and dropped once the pipeline is built. Two linking details mattered: `nir_opt_varyings`
keeps an output for transform feedback only when the store intrinsic carries XFB info, which
KosmicKrisp's `nir_lower_io` never adds, so captured varyings that nothing else read were deleted; and
adjacency topologies can be set dynamically within a pipeline's topology class, so the passthrough is
built for the non-adjacency class and adjacency draws are unrolled first. Four streams, rasterization
stream select and the transform feedback stream query are exposed; vkCmdDrawIndirectByteCountEXT is
not yet. Vulkan CTS: transform_feedback.simple, fuzz and primitive_restart pass with no failures (the
rest need more than 4 streams or buffers, or transformFeedbackDraw), and vkd3d-proton's stream
output tests pass on an unmodified vkd3d-proton. With single-texel alignment and transform feedback,
D3D12CreateDevice no longer needs relaxed checks.

Sparse resources (Mesa patches 0015-0017, `MESA_KK_EXPERIMENTAL=sparse`, on in `scripts/play`) use
Metal 4's placement sparse resources: buffers and textures created without memory whose 64 KB pages
`MTL4CommandQueue updateBufferMappings`/`updateTextureMappings` map onto placement heaps, which is
vkQueueBindSparse almost one to one, and 64 KB is D3D12's tile size. Device memory heaps of 64 KB or
more are made sparse-compatible. Metal's 2D tile shapes match Vulkan's standard block shapes for every
format probed (BC included), so residencyStandard2DBlockShape is honest; 3D tiles are flat slices
(non-standard), so no sparseResidencyImage3D. Image binds map tile regions directly; opaque binds walk
a per-layer layout of each mip's tiles followed by Metal's per-slice mip tail. Shader residency maps to
MSL's sparse_sample/read/gather (a value plus a resident flag; NIR's extra residency-code component is
split off in nir_to_msl). Findings on the way, each checked with standalone Metal probes:
- textures with shader-write usage only get sparse tier 1, whose small mip levels and tails misreport
  residency or fault the GPU; KosmicKrisp adds shader-write for transfer destinations and atomic
  formats, so sparse images without Vulkan storage usage drop it (copies use blits), and sparse
  residency with storage usage is reported unsupported;
- a texel-buffer view of a sparse buffer works once its descriptor carries the sparse page size;
- mapping updates need a resource-state barrier before later work; a small command buffer after each
  sparse bind provides it (without it, reads saw pages after they were unmapped).
Two gaps took a residency map kept by the driver (patch 0024). MSL's texture_buffer has no sparse_read,
and Metal does not discard a shader write to an unmapped buffer page: the value reads back at that
address until the command buffer ends (`tools/metal-probes/strict-write.m`), where Vulkan's
residencyNonResidentStrict wants the write discarded. KosmicKrisp keeps one byte per 64 KB page of
every sparse-residency buffer in a device buffer, filled on the queue's timeline in the command buffer
that follows each sparse bind. A sparse fetch from a texel buffer reads its residency from the map
(the view's descriptor carries an index into a table of views, in bits its 16-byte-aligned offset
leaves free). A store to a storage buffer whose descriptor marks it sparse is skipped when its page is
unmapped, per component, so a vector straddling two pages writes only its bound part. A store through
a raw device address has no descriptor, so the address the driver hands out for a sparse-residency
buffer carries an index in bits 48-59; shaders strip it from every pointer access and look the buffer's
map up when a store's pointer has one (DGC strips it from the vertex and index buffer addresses in its
stream). A check on every store costs 65-100% in a tight store loop however it is written (branch or
select, `store-guard.m`), so shaders that store to buffers get two copies of their code and pick one
with a single read of "does a sparse-residency buffer exist on this device": programs with none run
the original code (`tools/storebench`: stores, pointer stores and pointer loads +0%, atomics +1%, a
load+store loop about +5% from the read itself). `tools/sparse-test` covers the cases the CTS does not
(a straddling store, a store through a device address).

vkd3d-proton's own sparse tests are stricter than the CTS here and still fail, the same before and
after these patches: test_update_tile_mappings, test_texture_feedback_instructions_sm51/dxil and
test_sparse_default_mapping end without a result, test_update_tile_mappings_remap_vmem/smem fail about
980 of 7,711 checks, and test_execute_indirect_state fails 3 of 267. test_buffer_feedback_instructions
(CheckAccessFullyMapped on buffers) passes now that texel buffers report residency. Open.

D3D12 feature level 12_0 needs tiled resources tier 2, which needs min/max sampler filtering. Metal
takes a sampler reductionMode on every GPU but only honours it from Apple family 10 (M5): on M2 Pro
(family 8) and M4 (family 9, tested on another Mac) every mode returns the weighted average, through
both the Metal 3 and Metal 4 paths, and MSL has no shader-side reduction. Patch 0018 emulates it below
family 10: the sampler descriptor carries the reduction and filter modes, and each filtered sample
branches on them; the emulated path reads the 2x2 (2x2x2 for 3D) footprint at texel centres, where
linear filtering returns texels exactly and the sampler's address modes still apply, at one or two
mip levels, and takes the per-channel min or max of the texels with non-zero weight. Cube maps work in
face coordinates, with footprints crossing an edge clamped onto the neighbouring face's edge texels.
Vulkan CTS: sparse_resources passes (3,367, 0 failures; the rest need 3D or multisampled sparse images
or device groups), the sampler reduction and texture filtering groups pass (7,499, 0 failures). vkd3d-proton now
reports feature level 12_0 (tiled resources tier 2, resource binding tier 3), and Teardown runs with
no overrides.

An automated review of these patches (Codex, on the pull requests) made 17 distinct claims; each was
checked against the code, the specification and a test before changing anything. Eight were wrong
(the code already did it, an earlier NIR pass had lowered the case away, or the specification allows
the behaviour: totals in the first multiview query, primitive restart not affecting primitive counts).
Nine were real: DGC draws of strips with primitive restart into a geometry shader joined the strips
(`tools/dgc-test` case 3; patch 0020); the min/max emulation ignored the sampler's LOD clamp for
explicit LODs, skipped nearest filtering with linear mip filtering and sparse sampling, stayed off when
the extension was enabled without the Vulkan 1.2 feature bit, and computed a LOD for every sample
before looking at the sampler's mode (`tools/minmax-test`; patch 0022); sparse image queries offered
multisampling (0023); a statistics pipeline that failed to build was silently skipped (0021); and the
two sparse gaps above (0024). Its review of that fix made three more claims, all real: an
out-of-range texel index read past the residency map, a store straddling two pages was checked only at
its start, and stores through device addresses were not guarded.

Pipeline statistics queries (patch 0019) are counted by KosmicKrisp itself: Metal exposes only the
timestamp counter set on Apple GPUs (checked on M2 Pro and M4; statistic counter heaps are refused).
Direct draws and dispatches are counted on the CPU; indirect, predicated and DGC draws are summed by
one parallel kernel per render pass, run when the pass ends so no pass is split; tessellation and
geometry counts come from the compute emulation, which already knows how many patches, points and
primitives it made (a GS with dynamic output runs its count pass while a query is active). Fragment
invocations use a copy of the fragment shader with one atomic per SIMD-group, compiled when a pipeline
is made and turned into a Metal pipeline the first time a query counts fragments; it keeps early depth
testing unless the shader discards or writes depth, stencil, sample mask or memory. Driver-internal
clears, blits and copies are not counted. Clipping primitives equal clipping invocations (Metal does
not report what its clipper outputs; the spec allows this). Vulkan CTS: query_pool.statistics_query
passes (17,686 tests, the rest need a compute-only queue or device-address commands). Measured on M2
Pro at 1080p: no difference without a query against the previous driver; with every statistic
counting, 32 layers of overdraw cost 3% more, 5,000 direct or indirect draws 3-13% more GPU time, but
a shader that discards loses early depth rejection while fragments are counted (32 layers: 12 ms to
48 ms), because Metal shades every fragment of a shader with memory side effects.

Teardown slows down as destruction piles up. Logged over six minutes of play: memory stayed flat
(about 1.1 GB for the process, 3.4 GB of GPU memory), while CPU went from about 35% to 140% of a core
and the GPU from 80% to 95% busy. A sample of the main thread was about 83% busy, almost all in
FEX-translated x86 code (Teardown and the x86 vkd3d-proton), with about 1.5% in KosmicKrisp and 3%
in Apple's driver. That is more work per frame (debris physics on the CPU, more objects to draw),
not a leak; the CPU side is FEX overhead, for the performance work (an ARM64EC vkd3d-proton, FEX
tuning), and the GPU side wants a look at KosmicKrisp's render pass splits and per-sequence DGC draws.

The watchdog now limits swap growth since the game started rather than swap in use: macOS gives swap
back slowly, and swap left over from earlier runs stopped Teardown at launch.

## Unreal Engine 5: Subnautica 2's start-up checks and shader model 6.6 (#23)

Subnautica 2 (Unreal Engine 5, x86-64) stopped at four checks in a row. Each had a general cause.

**Visual C++ runtime.** The launcher asked for the Visual C++ 2015-2022 redistributable although Wine
provides it. It reads the file version of `vcruntime140_1.dll`, and that DLL had no version resource:
Wine's makedep registers resources per enabled architecture, and modules built only as ARM64EC are
output for aarch64 (disabled) and linked as arm64ec, so their resource list was empty. Wine 0019 takes
the resources of the architecture the module is linked for.

**SSE4.2.** FEX reads the host's ARM ID registers from the registry, where Wine publishes them from
the SMBIOS processor information. On macOS Wine's reader was a stub, so FEX assumed a baseline
ARMv8.0 CPU and hid SSE4.2, PCLMULQDQ, AES and SHA from x86 code (and skipped the faster code it has
for LSE atomics, RCPC and FlagM). macOS traps reads of the ID registers from user space, so Wine 0020
builds them from the `hw.optional.arm` features the kernel reports. Fields with no reported feature
stay zero.

**D3D12 adapter.** Wine's own d3d12 asks the DXGI adapter for a Wine-private interface, and DXGI is
DXMT's: `D3D12CreateDevice` failed with `E_NOINTERFACE` for every game not listed in `games.conf`.
vkd3d-proton is now the D3D12 path for all games (`HADRON_D3D12=wine` selects Wine's).

**Shader model.** Unreal requires its SM6 tier: shader model 6.6, resource binding tier 3, wave
operations and 64-bit atomics on typed resources. vkd3d-proton reported 6.0 on KosmicKrisp and no
64-bit atomics, because the driver lacked four Vulkan features. Metal offers none of them directly;
`tools/metal-probes` has the measurements behind each implementation.

- *Compute shader derivatives* (Mesa 0026). Metal takes no derivatives in compute kernels, and
  implicit sampling there uses LOD 0. But threads 4n..4n+3 of a threadgroup form a quad for every
  local size tried, which is Vulkan's linear derivative group. Derivatives become differences of
  `quad_broadcast` values, implicit sampling passes them as gradients, and LOD queries are computed
  from them with the sampler's clamp and mip filter. Quad groups shuffle the local invocation ids.
- *Denormal modes for 32-bit floats* (Mesa 0027). Apple GPUs flush 32-bit denormals to zero as
  operands and as results, in every math mode; 16-bit denormals are kept. Flush-to-zero is therefore
  native, except for operations that only move bits (min, max, negate, abs), which get an explicit
  flush. Preserve is emulated for shaders that ask for it: each operation keeps the native result
  when no operand or result is denormal, and otherwise computes it in integers with correct
  rounding. vkd3d-proton only requests preserve when a DXIL shader does (`-denorm preserve`), so
  ordinary shaders are unaffected. `tools/denorm-test` compares 24 operations on 65,536 inputs bit
  for bit with the CPU.
- *64-bit atomics* on buffers, shared memory and images (Mesa 0028, 0029). Metal has 64-bit atomic
  `min` and `max` only, without a result, on buffers and on RG32Uint textures. Those stay native,
  which is what Nanite's visibility buffer uses. Every other operation is a read-modify-write under
  a lock from a device table of 4,096 32-bit atomics. What had to be found out
  (`tools/metal-probes/atomic64-lock.m`):
  - A plain spin lock deadlocks. The threads of a SIMD group run in lockstep, so the one that takes
    the lock is held until the others stop spinning, and they are waiting for it. The lock is a state
    machine instead: a lane that gets the lock runs its locked section in the next pass of the same
    loop, so it never waits for its group, and the loop's exit depends on the value the unlock
    returns, which the compiler can't predict, so the locked section stays inside the loop. Compute
    and fragment shaders first let lanes take turns (only the lane whose turn it is spins). That
    stalled in the memory model tests, with threads giving up on locks nobody held, whenever the
    kernel was dispatched three or more times in a command buffer with Metal 4 barriers between;
    any small change to the kernel hid it (`tools/metal-probes/turn-lock-stall.m`, Mesa 0079). The
    cause is not established.
  - Plain loads, stores and texture reads are not coherent across threads, even under the lock.
    They are with `atomic_thread_fence` at device scope before the read, between the read and the
    write, and after the write. Without the middle fence an exchange on a texture returned stale
    values once 16 threads shared each of 4,096 texels.
  - 32-bit texture atomics on RG32Uint touch only the first channel, so there is no 64-bit
    compare-and-swap to build on, and the per-texture `fence()` breaks the locked sequence.
  - Native `min`/`max` can't be mixed with locked operations on the same value. Once a shader that
    needs the lock is created, a device flag sends `min` and `max` through the lock too, after the
    queue has drained.
  - macOS aborts a command buffer that holds up the display for about 40 ms ("Impacting
    Interactivity"). Threads killed inside the lock leave it held, and later kernels would spin
    forever. The device is lost at that point anyway, but the driver clears the lock table when a
    command buffer fails and bounds the spin (about 8 seconds), so the GPU is not left busy. Metal 4
    reports the same abort as `MTL4CommandQueueErrorTimeout`, after about 50 ms on the GPU when
    several processes compete for it.

  R64 image views are RG32Uint views; the lock is keyed on texel coordinates so every view of an
  image agrees. `tools/atomic64-test` checks exact totals on a buffer and on an r64ui image, with up
  to 65,536 threads on 1, 64 or 4,096 values.

Vulkan CTS, 0 failures: compute shader derivatives 152, float controls 4,051, 64-bit atomics on
buffers, images and in the memory model tests 2,648, the R64 image formats 827, sampler and subgroup
regressions 8,287, and a 7,559-test sample of image, format, render pass and compute tests.
`tools/d3d12probe` now reports feature level 12_0, shader model 6.6 and 64-bit atomics on typed
resources, descriptor heap resources and group shared memory.

The first launch on this driver passed Unreal's check ("shader model 6.6 ... atomic64 supported",
"RHI D3D12 with Feature Level SM6 is supported and will be used") and then stopped on two things: a
reserved texture, and Metal's shader compiler dying.

**Reserved textures with UAV access.** `CreateReservedResource` failed for a 16384x768x2 `R32_UINT`
texture: sparse residency with storage usage, which Mesa 0016 had reported unsupported. Mesa 0030
and 0032 allow it. Writable sparse textures only get Metal's sparse tier 1, and on macOS 27.0.1 its
residency queries can't be used:

- A view's first mip level is ignored: `sparse_read` through a view of levels 1-2 reports the
  residency of levels 0-1 (`tools/metal-probes/sparse-residency-views.m`).
- For some texture sizes `sparse_read` ends the command buffer with a GPU address fault: 11x37,
  64x64, 129x129, 257x257 and 513x513 fault, 128x128, 200x200 and 512x512 don't
  (`sparse-residency-fault.m`). Plain reads and writes are fine, and so is everything on read-only
  (tier 2) textures.

The two Apple documents disagree on whether this is allowed at all: the Metal Shading Language
specification says sparse textures do not support `write` or `read_write` access, while the
`MTLTextureSparseTier1` header describes what writes to unbacked regions do. Writable sparse
images rely on the header's reading; another reason sparse stays off for the alpha.

So a writable sparse image gets a read-only twin of the same shape that exists only to answer
residency queries (`sparse-twin.m`). Every bind maps the twin's tile to the same heap page as the
real one, which Metal allows. A sparse read in a shader becomes a plain read of the real texture for
the value and a sparse read of the twin for the residency code; descriptors carry both. The two
textures start their mip tails at different levels (the writable one at the same level or one
later), so the tail Vulkan sees starts at the earlier level and covers the writable texture's extra
tiled level too. One more thing Metal gets wrong: mapping a tier 2 texture, then a tier 1 texture,
then the tier 2 one again with nothing committed in between crashes inside
`updateTextureMappings` (`sparse-mapping-order.m`), so each bind call maps the writable textures
first and the rest after.

**Metal's compiler running out of memory.** `MTLCompilerService` aborted with a failed allocation,
and every library being compiled at that moment failed with `XPC_ERROR_CONNECTION_INTERRUPTED`;
Unreal treats a failed pipeline as fatal. Two causes, both in the shape of the generated MSL, which
declares every temporary at the top of the function (Mesa 0031):

- The declarations were zero-initialised, which made LLVM's mem2reg and SROA see two stores per
  temporary. Dropping the initialiser took one shader from 25 s and 2.5 GB to 5 s and 0.6 GB.
- Clang's uninitialised-variable analysis keeps a bit per tracked variable per basic block. A
  shader with 97,000 temporaries and 19,000 selects needed 2.6 GB just to parse. Three
  `#pragma clang diagnostic ignored` lines turn the analysis off: 160 MB.

The driver now saves any MSL that Metal refuses to compile (Mesa 0033, `MESA_KK_FAILED_MSL_DIR`;
`scripts/play` points it at `build/logs/failed-msl`), which is how the second cause was found.

**Where it stands (2026-10-01, paused).** With those fixes a launch initialises the engine in about
two minutes, creates the reserved texture, compiles every pipeline without a failure and reaches
the first frames behind the loading screen. It then waits on pipelines that take about a minute
each: the largest compute shaders reach Metal as 12-17 MB of MSL (88,000 temporaries, 240,000
lines) and cost its compiler 40-75 s and about 2 GB apiece. Most of that size is added by the
driver. Instruction counts for one such shader, pass by pass:

| After | Instructions | Texture ops |
|---|---|---|
| SPIR-V to NIR | 6,600 | 55 |
| compute derivatives, texture lowering | 7,700 | 107 |
| sparse buffer store guard (Mesa 0024) | 16,100 | 214 |
| explicit I/O | 21,500 | 214 |
| sampler min/max emulation (Mesa 0018) | 48,800 | 1,558 |
| descriptor lowering | 93,400 | 1,350 |
| null descriptor checks | 101,600 | 1,350 |
| NIR optimisation, as emitted | 63,200 | 1,350 |

What to do about it, in order:

1. Sampler min/max emulation: it unrolls 8 samples (16 for 3D) at every sample site, behind a
   check of the sampler's mode. Make it one sample in a loop over the corners and the two levels.
   Not started.
2. Sparse buffer store guard: it clones the whole shader body and picks the guarded or the plain
   copy at run time. Guard in place above a size limit instead. Written, untested:
   `docs/wip/ssbo-guard-in-place.diff`.
3. What is left of the compile cost after that is in LLVM's mem2reg, whose rename pass copies a
   vector of all multi-store variables per basic block: about 4,400 registers from `if`/`else`
   around every checked load and every texture operation (null descriptor checks, robust buffer
   access). Fewer branches means fewer of both; null descriptors could select a dummy texture of
   the right type instead of branching.
4. Clang's code generator walks every live local variable on each call
   (`EHScopeStack::requiresLandingPad`), which is quadratic with all temporaries declared at the
   top. Declaring a temporary where it is assigned, when its uses allow, keeps that list short.
5. Compile one oversized library at a time, build the driver without NIR validation, and cache
   compiled pipelines on disk so the cost is paid once.

Vulkan CTS for the sparse changes: all 1,036 `sparse_resources.shader_intrinsics.*sparse_read*`
tests (600 pass, the rest not supported, 0 fail) and a 1-in-10 sample of the whole sparse group.
The no-initialiser change touches every shader; the broad regression sample for it has not been run
to the end.

`docs/d3d12-coverage.md` lists what vkd3d-proton can and cannot offer on KosmicKrisp, so the
remaining driver work is a checklist.

Open: `dEQP-VK.api.info.image_format_properties.*` fails for every format, because the suite
requires sparse binding on 1D, 3D and multisampled images and KosmicKrisp has single-sampled 2D only.

## Steam starts a game as x86_64 first: the launch needed Rosetta (#24)

Found by the first test on a Mac that had never had Hadron or Rosetta (2026-10-05, M5 Max,
macOS 27.0.1). Setup worked and a game's install script ran through Hadron, but every game
launch failed in Steam with "OS Error", and `hadron-run.log` had no entry for it: Steam failed
before the tool ran. Installing Rosetta made it work.

`steamclient.dylib` starts a game's process with `posix_spawn` and a binary preference
(`posix_spawnattr_setbinpref_np`) of x86_64, then arm64. A compatibility tool is run as
`/bin/sh -c '<tool>/run waitforexitandrun <exe>'`, and `/bin/sh` has both architectures, so the
x86_64 one is chosen. Without Rosetta it cannot start. With Rosetta it runs translated, and so
does everything it starts that has an x86_64 slice: `scripts/steam-run` and `scripts/play` ran
under Rosetta on every Mac so far, reporting `uname -m` as x86_64. Wine, FEX and the game were
native all along, because those binaries are arm64 only. The install script is started another
way (`fork` and `execvp`), with no preference, which is why it worked.

NotProton patch 0003 has the library's `posix_spawn` hook set the preference to arm64 when the
spawn is Hadron's tool, and leave every other spawn, a Mac game among them, as the client asked.
`make spawn-env` starts real children through the hook with the client's preference and reads
`sysctl.proc_translated` in them.

## Portal: the frame rate drops while a failed portal shot's effect is alive (open, #25)

Reported by the first outside tester (M5 Max) and reproduced here on 2026-10-05: in Portal, a
shot at a surface that cannot take a portal drops the frame rate from the 120 cap to about 80
for as long as the effect lasts, every time. Setting `r_threaded_particles 0` did not change it
on the tester's Mac.

What was measured:

- mtld3d's instrumented build (`MTLD3D_PERF=1`, its report is in
  `<game>/mtld3d-logs/<exe>-<pid>.log`): a frame goes from 8.3 ms to 12-13 ms, and all of the
  difference is outside Direct3D. Time inside D3D9 calls is 0.07 ms calm and 0.08 ms during the
  drop, and the GPU wait goes to 0.01 ms. The D3D9 layer and the GPU are not the cause.
- `sample` of the game process in 5 s windows: during the drop three unnamed threads of the game
  become about 20% busy each, in translated code (about 6% of it in FEX itself), and the main
  thread is blocked in `NtWaitForAlertByThreadId` for 43% of its samples (6 samples calm, 373
  during the drop, of about 860). That is `RtlWaitOnAddress` with a timeout, which is how this
  Wine waits on a contended critical section (`wait_semaphore` in dlls/ntdll/sync.c).
- wineserver stays at 3-4% CPU and about 5,500 context switches a second in both phases, so
  server round trips are not the bottleneck.

So the main thread loses about 4 ms a frame to lock waits against the engine's worker threads.
Which lock is not known: `sample` cannot unwind past Wine's syscall dispatcher into the PE side.
The next step is a temporary trace in `RtlEnterCriticalSection` that records, for each contended
wait, the section, the caller's return address and the time waited, summed and printed every few
seconds, with the addresses resolved through the `+loaddll` lines of the game's log. The process
heap's lock is the first suspect, since Windows avoids contention there with its
low-fragmentation heap.

A Steam launch option cannot carry an environment variable in front of `%command%` on the Mac:
Steam does not run the command through a shell and fails with "OS Error 260". To run a game on
another runtime, point `~/Library/Application Support/Hadron/runtime` at it for the test.

## Unreal's launcher runs the prerequisite installer when Wine's VC runtime version is old (#26)

Astroneer on the outside tester's Mac (2026-10-05): `Astro.exe`, Unreal's launcher, started
`UE4PrereqSetup_x64.exe`, whose .NET Framework 4 step then sat without progress (26 of its MSI
custom actions failed to load with error 193, the others ran; not investigated further).

The launcher reads `Major`, `Minor` and `Bld` from
`HKLM\Software\Microsoft\VisualStudio\14.0\VC\Runtimes\x64` and compares them with the
toolset the game was built with (in Subnautica 2's launcher: `cmp $0xe`, `cmp $0x2a`, then the
build number), and runs the game's redistributable installer when the registered version is
older. Wine's wine.inf registers 14.42.34433, from November 2024 when the key was added
upstream, so every game built with a later toolset starts its installer in a new prefix. The
runtime libraries themselves are there: a 64-bit test program loads all of msvcp140, its `_1`
and `_2`, vcruntime140 and `_1`, the 2010 to 2013 runtimes, XInput 1.3 and the DirectX audio
libraries.

Wine patch 0024 registers 14.51.36247, which is what the redistributable Steam installs today
writes. The entries are written without overwriting, as upstream has them, so a prefix made
earlier keeps 14.42; such a prefix has to be made again for a game that needs it.

That was not enough for Astroneer: its launcher still asked. It wants 14.24.28127.4 or newer
and then loads `msvcp140_2.dll` and `vcruntime140_1.dll`, and the tester's logs show it never
reached the two loads, so the version it read was too low. Steam's install script, which runs
before the game, had run an older redistributable, and under Wine that installs and writes its
own version over the registered one (reproduced: Steam's 2019 `VC_redist.x64.exe /quiet` in a
new prefix exits 0 and leaves 14.28.29334 where 14.51 was). On Windows it would not: the
installer is a WiX bundle, the installed redistributable is registered as one under
`Uninstall` in the 32-bit registry view, and an older bundle that finds a newer one stops with
`ERROR_PRODUCT_VERSION` (1638), which Steam's scripts accept.

Registering Wine's runtime as such a bundle makes the older installers stop the same way (tried:
with the keys a real 14.51 redistributable leaves, and a bundle path that exists, Steam's 2019
installer logs "Detected related bundle … operation: Downgrade" and exits 1638). It was not kept:
the redistributable also installs libraries Wine does not have, MFC among them, and a game that
needs those gets them from exactly that installer. Claiming the whole bundle is installed would
take them away.

So the installers run as before, and `hadron-steam.exe`, which starts every game, keeps the
registered version from being lower than the runtime that is there: it reads the file version
of `msvcp140.dll` in the system directory (Wine's stays in place through an older installer,
its version being higher) and raises `Major`, `Minor`, `Bld` and `Version` under
`VC\Runtimes\x64` and `x86`, in both registry views, when they are below it. In a new prefix:
14.51 after the first launch; 14.28.29334 after Steam's 2019 x64 and x86 installers, with their
twelve MFC files installed and Wine's `msvcp140.dll` untouched; 14.50.35719 after the next
launch. A prefix lowered earlier is repaired the same way. With this in the 0.1.0 build of
2026-10-05 the tester's Astroneer starts without the prompt and runs well, in the prefix that had
been lowered.

## Java: HotSpot's compiled code gives wrong results under FEX (open, #27)

Slay the Spire (Java 8, x86-64, started as `jre\bin\javaw.exe -jar desktop-1.0.jar`) shows "A
Java Exception has occurred" on the outside tester's Mac. The JVM loads `jvm.dll`, `java.dll`
and `zip.dll` and nothing after. Which exception it is has not been captured, so what follows
is a Java problem found while looking, not yet shown to be the game's: running the game's own
`jre\bin\java.exe -Xint -jar desktop-1.0.jar`, or reading its exception from a console, would
connect the two.

A stock Windows JRE (Temurin 8u504, x64) in a test prefix:

- `java.exe -version` works.
- `jjs.exe script.js` (Nashorn) fails differently in each mode: by default with
  `IllegalArgumentException: not primitive: class java.lang.Object`, with the C1 compiler alone
  (`-J-XX:TieredStopAtLevel=1`) with a `NoClassDefFoundError` for a class that is there, with C2
  alone (`-J-XX:-TieredCompilation`) with `ArrayIndexOutOfBoundsException: 1632382876`.
- With the interpreter alone (`-J-Xint`) system properties, file and network calls, eight
  threads and a 20-million-iteration loop all give the right results. A deep recursion then ends
  in `StackOverflowError` and a fatal `EXCEPTION_GUARD_PAGE`, which is a second problem.

So results go wrong once HotSpot runs code it compiled. HotSpot patches that code in place while
it runs (inline caches, class constants, entry points), which makes stale translations in FEX
the first suspect: its detection of self-modifying code (`SMCChecks`, `mtrack` by default) may
not see those writes here. Not verified. The next experiment is the same `jjs` test with
`FEX_SMCCHECKS=full`, and with `FEX_DISKCACHE=0`; `scripts/wine-run` passes only `FEX_DISKCACHE`
on, so run `dist/bin/wine` with its environment by hand.

## Wine: native host mappings stall thread creation on macOS (fixed, #28)

Astroneer froze for 2.7–2.9 seconds on mission completion on an Apple M5 Max with
48 GiB of RAM, macOS 27.0.1 (26A434). Repeating the mission after restarting the
game reproduced it with the FEX disk cache enabled. A present probe measured the
frame gap, and Wine allocation and lock probes located the same interval: the HTTP
manager created a worker thread whose thread data and emulation stack allocations
searched for addresses above 4 GiB while holding Wine's global `virtual_mutex`.
Other threads waited for that lock. Save-file and DXMT shader-cache metadata did
not change during the stall.

Wine's view tree records Wine allocations, but not all native mappings. The search
therefore saw a free gap containing a native, inaccessible guard mapping from
`0x1000000000` to `0x7000000000` (64–448 GiB). Each non-overwriting fixed-map
attempt correctly failed, but the search advanced by only 64 KiB. The two
allocations made 7,372,814 and 7,372,815 failed attempts. The stall was in Wine's
address search, not FEX compilation or a graphics driver.

Wine patch 0025 queries the overlapping native region after a mapping collision
and skips to its aligned boundary in the search direction. It descends through
Mach submaps at the candidate address and requests short region information, which
does not ask for resident-page statistics. The caller still enforces allocation
bounds, and every subsequent mapping attempt remains non-overwriting. If a query
fails or the conflicting mapping disappears, the search takes its original single
step. There are no game checks or fixed host addresses in the allocator change.

`python3 -B tests/mac/test-wine-address-search.py` builds checks from the actual
Wine helper and runs native ARM64 and FEX-translated x64 Windows reproducers in
separate prefixes. A test-only injected library reserves native address gaps
without committing RAM, and the Windows test exhausts Wine's high reservation
before creating threads. Fourteen native checks cover both search directions,
64 KiB and 2 MiB alignment, partial overlap, preservation of existing mappings,
and an unmap between collision and query. Windows checks verify bounded success
and failure in both directions with 2 MiB alignment. Thread creation went from
about 0.82 seconds to 0.34–0.53 ms on ARM64, and from 1.68–1.71 seconds to
0.66–0.94 ms on x64 with the FEX cache enabled. Timings are reported, not used as
pass/fail thresholds.

All eight patch queues apply and match their source commits. Runtime smoke checks
passed for ARM64, x64 and i386 programs, Visual C++ runtime DLLs and registry
entries, and a D3D11 triangle. In the instrumented game retest the player reported
no lag; a 54-second capture around the mission had no present gap of 100 ms or
more. The longest observed address search took 0.297 ms, with 173 attempts. This
confirms the reproduced stall is fixed; other macOS releases, Intel-native builds
and a broader game set have not been tested. Diagnostic Wine and DXMT probes are
not part of the patch.

## Steam's overlay was loaded too late: no overlay, and the camera lagged (fixed, #29)

Subnautica's camera became laggy while the mouse moved, on an M2 Pro. Samples of the game showed
the process's Cocoa main thread asleep in `usleep`, called from `gameoverlayrenderer.dylib`'s hook
on `-[NSApplication nextEventMatchingMask:untilDate:inMode:dequeue:]`, for 24 to 45% of the time
while the camera moved and 0 to 4% while it was still. The game's main thread spent 8 to 15% of
its time in `NtUserGetCursorPos`, which winemac answers with a synchronous call to that thread.
Shift+Tab did nothing, and Steam never started its `gameoverlayui` helper for the game.

Steam for Mac gives a game `STEAM_DYLD_INSERT_LIBRARIES`, naming `steamloader.dylib` and
`gameoverlayrenderer.dylib`, and a Mac game has them in its process from the start. `scripts/play`
starts Wine with a clean environment and passed the variable on only under that name, so nothing
loaded them at start; lsteamclient's Unix side opened them by hand when the game loaded it
(`load_overlay_libraries`, NotProton's fallback for a loader whose inserted libraries dyld drops).
The overlay's log (`STEAM_OVERLAY_LOGGING=1`, in `/tmp/gameoverlayrenderer.<pid>.log`) then held
the attach header and nothing else: no "One-time initialization", no Metal hooks. Loaded that
late it only got in the way of the event loop.

`scripts/play` now also sets `DYLD_INSERT_LIBRARIES` to the same libraries, so every process of
the game has them from the start. The overlay then hooks `presentDrawable:` and its variants on
`_MTLCommandBuffer` and `commit` on the driver's command buffer, Steam starts `gameoverlayui`,
and the hot-key opens the overlay. In eighteen samples of Subnautica taken after the change the
Cocoa main thread never slept in the overlay, and the player reported no lag.

The overlay hooks only Metal 3's present calls. DXMT and mtld3d present with those. KosmicKrisp
presents through a Metal 4 queue (`waitForDrawable:`, `signalDrawable:`, `[drawable present]`),
so behind Zink, vkd3d-proton or a Vulkan game the overlay hooked and then never saw a frame:
`tests/win/gltri.c`, a legacy OpenGL triangle, shows it. NotProton's overlay helper
(`overlay-shim/overlay_shim.m`, built by `scripts/build-steam-play.sh`) is loaded in front of
the overlay for that. It presents such a drawable from a Metal 3 command buffer that waits on an
event the Metal 4 queue signals, so the overlay's hooks run, and it builds the overlay's pipelines
for surface formats the overlay has none for. With it the same test reports a screen size in the
overlay's log and `gameoverlayui` connects.

Checked by the player with the helper loaded: Subnautica (Direct3D 11, DXMT), Portal 2
(Direct3D 9, mtld3d), Teardown (Direct3D 12, vkd3d-proton) and Geometry Dash (OpenGL, Zink) show
the overlay on Shift+Tab, and each game's overlay log has the screen size and the hot-key.

The launch script is what gives Steam's libraries their environment, and four variables matter
to the overlay. The test copies of the runtime passed all four; the launch script merged with the
first build of 0.2.0 passed none, which a comparison of the copy the player had tested with that
build showed. `SteamOverlayGameId` is where the overlay takes the game's id from: without it Steam
started `gameoverlayui` with game id 0 (`/tmp/gameoverlay_ui.txt`, "Got gameid on commandline"),
though the overlay still opened, and `d3d11tri` through `scripts/play` gives id 0 without the
variable and the right id with it. `SteamClientLaunch` and `SteamEnv` are read by Valve's
`steamclient.dylib`; what they change was not looked into, they are passed because Steam sets
them and the tested runs had them. `NOTPROTON_OVERLAY_SHIM` names the helper for lsteamclient,
which then finds it already inserted and does not open the overlay's libraries a second time.

The overlay opened, but a game that does not react to it kept the mouse and the keyboard. Portal 2
pauses and lets go of the cursor when Steam tells it the overlay is open; Subnautica and Geometry
Dash carried on, with the cursor locked in the middle of the window, and the overlay could only
be used after pausing the game. lsteamclient sets the event
`__wine_steamclient_GameOverlayActivated` while the overlay is open, and nothing in Hadron's Wine
read it. Wine patch 0026 makes winemac.drv act on it. A thread in a game started by Steam waits
for the event, so nothing is polled while the overlay is closed. While it is set, key presses,
mouse buttons, movement and scrolling are not sent to the game; key releases are, and held mouse
buttons are released, so nothing stays down. The game's clipping rectangle is lifted and
remembered, `ClipCursor` and `SetCursorPos` succeed without moving the cursor, and `GetCursorPos`
leaves the position Wine last had, so a game that re-centres the cursor every frame sees no
movement. The rectangle is applied again when the overlay closes. Proton's X11 driver drops the
same input on the same event and leaves the cursor to the overlay, which grabs the pointer there.
The player confirmed a free cursor on Shift+Tab without pausing.

Not done, not measured:

- The helper's cost. It changes how KosmicKrisp's frames reach the screen and hooks
  `setRenderPipelineState:` on every path. The triangle tests run at the same rate with and
  without it; no game has been measured.
- A game that uses Vulkan directly, which takes the same path as the OpenGL test, and whether
  Steam leaves the variable out when a game's overlay is switched off in Steam.
- `tests/win/d3d9tri.c` cannot show the overlay on mtld3d: its presenter thread stays idle in
  that test, so only a game shows it.

`sample` is not free here. It stops every thread of the target a thousand times a second, and
on Subnautica, with about a hundred threads, the player saw lag exactly while it ran: for five
seconds at a time with a 4-second sample every 13 seconds, and all the time with samples back to
back. Lag reported while sampling does not count.

## Wine: window reordering cancels fullscreen minimization (#30)

Bravely Default Flying Fairy HD Remaster sometimes stayed stuck after Cmd+Tab
out of fullscreen and back: the window returned with unusable contents and did
not accept focus. This reproduced on Hadron 0.1.1 and 0.2.0 on an Apple M5
MacBook Pro with 32 GB of RAM, macOS 27.0.1. The game uses Unity 6 and D3D11
through DXMT.

Wine requested native minimization and set `WS_MINIMIZE`, but neither
`windowWillMiniaturize` nor `windowDidMiniaturize` followed. The native window
later became visible, unminimized and key while Wine still considered it
minimized, with a zero-sized client, and rejected its focus event. Between the
request and native initiation, `adjustWindowLevels:` changed the window's level
and explicitly ordered it front or back. Those ordering calls can cancel the
pending minimization.

A native AppKit test isolated this conflict without Wine or a game: immediate
`orderFront:` and `orderBack:` prevented initiation even with style and level
held constant; matched cases without ordering completed. Calling `orderFront:`
from `windowWillMiniaturize` allowed completion. In the game, suppressing only
the automatic ordering while inactive and awaiting initiation completed all 36
requests. Restoring the unchanged driver reproduced the failure on its fifth
request.

winemac.drv now tracks a native minimize request until initiation and omits the
automatic front/back ordering during inactive level adjustment while that request
is outstanding. It still applies the level change. Tracking starts when the
native request is issued, including requests deferred until a hidden window is
shown, and clears on native initiation, completion or restoration, or an explicit
Win32 restore. Explicit activation, show and restore paths retain their behavior.

Tested without diagnostic instrumentation in an isolated Hadron 0.2.0 runtime:
Bravely Default completed 18 minimizations and 17 restorations; Portal 2 (Source,
D3D9 through mtld3d) completed nine and eight. Both sessions ended minimized.
The player confirmed working Dock transitions in Bravely Default and normal
gameplay through ordinary and rapid switching in Portal 2. Loaded driver
identities were verified, and neither session rejected a focus event. Eleven
Win32 checks also passed for hidden/minimized creation, show, restore,
hide/reshow after minimization, and cancellation before showing.

The original experiments are preserved as one twelve-case native reproducer and
eleven Win32 lifecycle checks under `tests/`; see
[the build/run instructions](../tests/mac/minimization.md). Both passed on the
same Mac and macOS; the Win32 checks used the runtime rebuilt from main. This
adds automated evidence, not a new gameplay or visual acceptance run.

A local trace-only experiment restored immediately after ordinary and deferred
native requests. The traces established that the request had returned, no
`windowWillMiniaturize` had entered, and at restore entry the marker remained set
with `pendingMinimize` false. Restore arrived with `minimized_valid` true and cleared
the marker before any native callback. That validity flag compares `WS_MINIMIZE`
with Wine's cached requested state, independently of native miniaturization.
Thus the tested restores did not skip the guard or leave a stale marker.

Immediate restore also exposed a separate native/Win32 state mismatch, observed
both with #47 and in the older trace-only 0.2.0 source/runtime control. The restore
correctly cleared the marker, but native minimization subsequently completed
while Win32 reported restored. This case remains unresolved and warrants separate
investigation; #47 addresses automatic ordering that cancels minimization, not
cancellation of an already-issued native request. Successful Win32-only checks
do not establish that this mismatch is resolved. Subsequent completed
minimize/restore controls worked, but do not establish recovery from the earlier
immediate restore or correctness across all timing sequences.
Diagnostic instrumentation remains local; production behavior is unchanged.

## Games ran from inside the app, so an update could change a running game (fixed, #31)

Hadron.app carries the runtime, and Steam's tool started games straight from it:
`~/Library/Application Support/Hadron/runtime` was a link to
`Hadron.app/Contents/SharedSupport/runtime`. Whatever replaces the app replaces the runtime under
any game that is running: `brew upgrade`, a new app dragged over the old one, and an updater
inside the app. The game keeps the libraries it has loaded and gets the new build's for
everything it loads afterwards.

It showed while adding an updater (Sparkle). A check for a running game before the update is not
enough, and there is no place for a second one: started from a terminal, with the app's window
closed, Sparkle goes from unpacking to installing without asking again. With a test program
started just after `hadron update`, the app was replaced while the program ran. A guard in the
updater also does nothing for the other two ways an app is replaced.

Now a game does not run from the app. Each build of the runtime has a name
(`BUILD-ID`, written by `scripts/package-runtime.sh`; two builds of one version differ), and is
copied once to `~/Library/Application Support/Hadron/runtimes/<build id>/`. On one APFS volume
the copy is a clone: 0.32 s and no space of its own for the 2,037 files and 1.1 GB of 0.2.0's
runtime. The link Steam's tool starts names the copy new games use. `scripts/steam-run` takes
its real path, so a running game is told nothing through the link, and before anything else
decides which copy the launch runs from (`scripts/runtime-versions.sh`):

- a launch that is alive for the game's prefix, or a Wine session running in it, keeps its
  copy, and what starts there joins it. A launch marks its copy with a file named after the copy
  and its process ID, holding the prefix; Steam's tool lives as long as its game. A session
  whose launch is gone is found by its wineserver, which works in
  `/tmp/.wine-<uid>/server-<device>-<inode>` of the prefix, and `lsof` names the runtime its
  program is in;
- otherwise the build in the installed app, whose runtime folder `scripts/steam-install` wrote
  to `source`. If no copy of it exists yet, this is the first launch since the app was updated:
  the copy is made, the link is renamed over the old one, and the copy's own `steam-run` takes
  over. Comparing the two names costs about 4 ms a launch;
- an app that cannot be read changes nothing.

A copy is deleted by a later launch, when it is not the one the link names, no launch that
marked it is alive and no process has files of it mapped. When the processes cannot be looked
at, a copy counts as in use: kept too long it costs space, deleted too soon it costs a game.
Only a folder in `runtimes/` that carries a `BUILD-ID` is ever deleted, never through a link,
and the link itself is only ever replaced by a link. One launch at a time chooses, copies and
deletes, under the system's own file lock (`lockf` on an open file), which cannot outlive its
holder. The memory watchdog and `scripts/stop` count the processes of every copy.

So a running game never changes, and the next game started runs on the new build, with both
running side by side if need be. Setting Steam up again is asked for only when a build changes
Steam's own part: `steam/HOOK-ID` is made from the sources of the library in Steam, the hooking
library it links and the hook signatures, and `scripts/steam-status` compares it with what
`scripts/steam-install` recorded. The library's bytes cannot serve: a clean rebuild of the same
source gave another file, and every signature changes them again.

A setup made by 0.2.0 or older, where the link names the app, moves over at the first launch
after the app is updated: a launch that begins in a folder that is not a copy takes that folder
as the installed app. Such a setup has no recorded `HOOK-ID`, only the checksum of the library it
put into Steam. `packaging/hook-history` lists those of 0.1.0, 0.1.1 and 0.2.0, read from the
published disk images; all three were built from the NotProton revision, patches and Dobby commit
that the current `HOOK-ID` stands for, so none of them is asked to set Steam up again.

`tests/mac/test-runtime-versions.sh` checks the rules on throwaway folders (52 checks, among
them: a link, a file, a folder without a `BUILD-ID`, a copy a process runs from and a copy a
live launch marked are kept; with `lsof` or `ps` failing nothing is deleted; the app a link names
is untouched; a second launch for a prefix stays with the first; the lock is free the moment its
holder is killed). With the real runtime and a Direct3D 11 test program, 25 checks: a game ran
from the first build's copy, the app was replaced at the same path, a second game started from
the new build's copy while the first went on from its own, a second program in the first game's
prefix ran from the first copy, a clean-up while the first game ran kept its copy, and one after
it had ended deleted it.

Through Steam, on a setup made by the 0.2.0 release: the app was replaced by a build of this
change, and the next game (Subnautica) started normally, from a copy it made, with the link moved
and the app's 2,040 files untouched. The window asked for nothing. Then, with Portal running, the
app was replaced again; Portal's session stayed on its copy, and Geometry Dash, started beside
it, ran from the copy of the newer build. Repair, once macOS allowed the rebuilt app to change
Steam, recorded the `HOOK-ID` and left the link on the current copy. Those runs were on the
first form of the rules (a ten-minute rule and a lock folder). On the final one, with the app
replaced once more, Geometry Dash started from the new build's copy, marked it for its prefix,
and the two older copies left in the Library were deleted.

A runtime without a `BUILD-ID`, a checkout or a copy somebody is changing, is run in place as
before. Not covered: the app and the Library on different volumes (the copy is then a real
one, 1.1 GB each time); two different builds started in one prefix at the same instant; and the
old copy's space, which is held until a launch after its last game has ended.

## OpenGL windows let their alpha show through (fixed, #32)

Half-Life's intro video was black, with its sound playing. The engine decodes
`valve/media/valve.webm` itself (a libvpx built into `hw.dll`), uploads each frame with
`glTexSubImage2D` to a rectangle texture and draws it as one quad to the window. A trace
(`WINEDEBUG=+opengl`) showed all 149 frames uploaded and drawn, with blending off, so what the
video's alpha channel holds lands in the window's back buffer. The frames' bytes were not read;
that the picture was fully black and plays once the alpha is ignored says it holds 0.

On Windows that does not matter: a window is opaque whatever its pixel format's alpha holds.
Here it did. Zink creates its swapchain with pre-multiplied alpha when the EGL config has alpha
bits and the surface was not created with `EGL_PRESENT_OPAQUE_EXT`, the Metal layer is then
composited with the frame's alpha, and a frame with alpha 0 came out black. Mesa offered the
extension only on Wayland, and Wine's Mac driver did not ask for it.

Mesa patch 0084 offers `EGL_EXT_present_opaque` for window surfaces on a Metal layer, and Wine
patch 0028 creates every OpenGL window surface with it, as Wine's Wayland driver does. The
swapchain is then opaque, and KosmicKrisp drops the alpha when it copies the frame to the
drawable. What the application reads back is unchanged: the alpha stays in its framebuffer.

`tests/win/glalpha.c` draws a frame whose left half has alpha 0 and prints what it reads back
(`0 255 0 0` on the left before and after). With the two patches the video plays, seen by the
player on 2026-10-11. This applies to every OpenGL game: what a game left in its back buffer's
alpha decided how the frame was composited, which no Windows game expects. Not measured: how
macOS composites a frame whose colour exceeds its alpha, which is what such a frame is to it.
Not covered: a window that is meant to be see-through (a layered window with per-pixel alpha),
which the Mac driver's old OpenGL path supported and this one never did.

## D3D12: a game that waits for each frame keeps the GPU at its lowest clock (fixed, #33)

Teardown ran at 16 fps on an M2 Pro (1512x982, render scale 100%, quality Low), which the first
look had put down to CPU translation (#22). It was not that. Measured in one level, standing
still:

| | render scale 100% | 50% |
| --- | --- | --- |
| frames per second (Metal HUD) | 15.9 | 27.4 |
| GPU time per frame | 43.3 ms | 19.6 ms |
| frame interval | 62.9 ms | 36.5 ms |

- The frame interval is the GPU time plus 17 to 20 ms, not the larger of two overlapping
  times: the CPU and the GPU take turns. A Metal System Trace (`xctrace record --template
  "Metal System Trace" --attach <pid>`, then `xctrace export`) shows one block of GPU work per
  frame and the GPU idle for the rest.
- `sudo powermetrics --samplers gpu_power` showed the GPU at 444 MHz, the lowest of its eight
  states (the highest is 1398 MHz), for 100% of the time, and idle for 27 to 43% of it. The
  trace says the same ("Minimum GPU Performance state"). The Mac was on mains power, not in Low
  Power Mode, and its thermal state was nominal. The device tree gives the GPU's controller a
  target utilisation of 85% (`gpu-perf-tgt-utilization`): it raises the clock when the GPU is
  busier than that, and a GPU that waits for the CPU for a part of every frame never is.
- The main thread used 7% of a core on the game's first screen and waited for the rest, in
  `NtWaitForSingleObject`. `sample` had shown 56% of its samples in translated code in the
  level, which says where the stack was and not that the thread ran; the CPU times of `ps -M`
  are the measure.
- The cause of the turns is the game's own pacing. Its swapchain has a latency handle
  (`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`) at the default latency of 1, and it
  waits on the handle before each frame. vkd3d-proton releases the handle when
  `vkWaitForPresentKHR` returns, which KosmicKrisp's window system code does from the drawable's
  presented handler: when the frame is on the display. On the game's first screen, where a frame
  takes 2 ms to make (450 fps with the game's vertical sync off), that gave 21 to 40 fps on a
  120 Hz display with vertical sync on.

So two things multiply. A frame is reported as shown only several refresh cycles after it was
queued, and the game waits for that; and because the GPU idles during the wait and while the
game prepares the next frame, it stays at a third of its speed. At full clock the same GPU work
would take 14 ms (43.3 x 444 / 1398), inside a 60 fps frame. That the same pacing costs little
on Windows, where a GPU with work does not stay at a third of its clock, is reasoning: it was
not measured.

vkd3d-proton patch 0003 adds `VKD3D_SWAPCHAIN_MIN_LATENCY_FRAMES`: the fewest frames a swapchain
with a latency handle lets the application have in flight, whatever latency it asks for. The
latency the application reads back stays the one it set. `scripts/play` sets it to 2. With that,
in the same place at render scale 100%: about 60 fps, the GPU at 1398 MHz for 97 to 100% of the
time and idle for 0 to 3% (measured with an experimental build that set the latency the same
way; the committed patch was measured on the first screen only). On the first screen: 69 fps
against 36 (the table below). A swapchain without a latency handle is not affected;
vkd3d-proton already lets those run three frames ahead, and notes the same "GPU goes falsely
idle" effect on other hardware.

This is a deviation from what the game asks for, in timing only: it gets one frame more between
input and picture than it requested. At 60 fps that is 33 ms where one frame at 16 fps was
63 ms. A game that reaches the display's rate at latency 1 pays a refresh cycle for nothing;
`VKD3D_SWAPCHAIN_MIN_LATENCY_FRAMES=1` in its launch options gives it exactly what it asks for.

Game Mode was tried as the other thing that might raise the clock. It is off for Hadron's games
(the HUD says so: the bundles games run from are not declared as games), and `xcrun gamepolicyctl
game-mode set on` forces it on. On the game's first screen at render scale 100%, 80 seconds a
run, the GPU's state read from a Metal System Trace:

| latency | Game Mode | frames per second (median) | GPU performance state |
| --- | --- | --- | --- |
| the game's (1) | automatic: off | 35.9 | minimum, 99% of the time |
| the game's (1) | forced on | 39.4 | minimum, 99% |
| 2 | forced on | 69.9 | minimum, 99% |
| 2 | automatic: off | 69.0 | minimum, 100% |

No difference beyond the noise of that screen, which loads the game in the background. This does
not settle it: the screen is light enough that the GPU stays at its minimum even at latency 2, and
forcing the mode on does not show that macOS took the process for the game. Not done: the same
comparison in a level, with the bundle declared as a game. Not known either: what CrossOver does
here.

Ruled out on the way: staggered submissions (`VKD3D_CONFIG=no_staggered_submit` changed nothing),
and the D3D12 layer's own CPU cost (2% of the main thread's samples). Left for later, each a cost
per frame that is now worth measuring again:

- KosmicKrisp runs the preprocessing of device-generated commands as a compute pass inside the
  render pass, which ends the pass, stores its attachments and loads them again: 10 to 14 times
  a frame in Teardown. vkd3d-proton already asks for the preprocessing ahead of the pass
  (`vkCmdPreprocessGeneratedCommandsEXT`), which KosmicKrisp ignores.
- vkd3d-proton has a mode for tile-based GPUs (render passes kept open across command lists),
  which it turns on by driver; KosmicKrisp is not on its list, Asahi's driver for the same GPUs
  is.
- The window system code copies every frame to the drawable and leaves the layer non-opaque, a
  workaround for a Metal 4 crash; the HUD reports the game as composited, not direct.
- A frame is reported as shown three to six refresh cycles after it was queued (the first
  screen's 21 to 40 fps for a 2 ms frame). The default of 2 covers for that: as long as it holds,
  no game that waits for each frame reaches the display's rate at latency 1, however light its
  frames. When the report comes on time, measure in a level again whether latency 1 keeps the
  GPU busy enough, and set the default back to 1 if it does.

## Steam beta's Compatibility UI moved outside the filename filter (#34)

Steam beta 1791856936 puts its Compatibility UI in `steamui/sp.js`, while stable
1788652215 uses `chunk~*.js`. The pinned NotProton filter admitted only the latter,
so the existing beta transformer never received the new bundle.

NotProton patch 0004 backports the filename filter and path tests from upstream
[`27a119f`](https://github.com/NotProtonNot/NotProton/commit/27a119f01495bdd77139c997e6eb769ad8b1f150).
It admits JavaScript under `steamui/`, excluding localization and library bundles.
The transformer is unchanged: compatibility edits still require every gate to match,
and transformed contents are served through an anonymous file without changing Steam's
files. Broader upstream resolver and logging changes are not included.

Tested on an Apple M5 MacBook Pro, 32 GB, macOS 27.0.1 (26A434):
the 14 path cases and three stable/beta fixtures pass, including refusal of truncated,
duplicated, and already-patched input. A reader using the built library receives
transformed stable-fixture and actual beta-bundle contents; excluded paths and disk
contents remain unchanged. The patch queue reapplies to the pinned revision and matches
the tested source. CI runs the path and fixture checks.

Live checks on both Steam versions passed native Mac defaults, a Windows/Hadron override,
Mac revert, restart persistence, Windows-only launches, and channel changes without
Repair. Rise of the Third Power supplied the platform-switch control; Bravely Default
supplied the Windows-only control. A locally built app also passed fresh setup on both
channels, with game logs confirming its managed runtime. The tested library matches the
installed copy. Other Macs, startup cost, a fresh full engine packaging build, and
notarized distribution were not tested.

Switching tests used existing prefixes; first-run prefix initialization was not validated.
## Game launch activation and inactive fullscreen ordering (#35)

Steam launches could assign Win32 focus without activating the native macOS game
window. Subnautica stayed behind other apps; Bravely Default and Portal 2 drew in
front but required manual focus. Wine detaches the game process during launch,
so the game cannot rely on inheriting Steam's activation context.

NotProton patch 0005 provides a cooperative activation handoff. `steam-run` calls
a native requester on its existing game route, before `scripts/play` starts Wine.
Path queries and outside-install helpers do not request activation. The requester
identifies its Steam ancestor and asks the foreground Steam frontend to yield to
the game's bundle ID. It waits for acknowledgement before continuing; inactive
frontends decline, expired requests are rejected, and an absent reply times out
after two seconds. Shell argument handling and arm64 spawn selection are unchanged.

Inactive fullscreen windows also had an ordering defect: `minimumLevelForActive:`
returned normal level under Wine's inactive-floating policy, but
`adjustWindowLevels:` then raised floating windows above the Dock. Wine patch 0029
applies that floor only when the computed minimum exceeds normal. An inactive game
stays at level 0; active fullscreen and permitted windowed topmost windows retain
their elevation.

The [native controls](../tests/mac/launch-focus.md) check activation from either
Steam frontend, delayed windows, blocked callbacks, absent receivers, deliberate
switching, and inactive fullscreen ordering. Relay cases wait for their private
processes to terminate and fail on missing launch/completion evidence. Routing
checks cover quoted direct and shell-wrapped launches, launchers, app-ID selection,
queries and helpers. Timeout and cleanup checks preserve unrelated processes.
The activation and routing controls pass on an M5 with 32 GB, macOS 27.0.1
(26A434). Native requester execution measured 48–157 ms, including process and
AppKit startup; this is not a game-launch benchmark.

On 2026-10-12, the owner reported two successful Steam test runs each for Bravely
Default, Portal 2 and Subnautica on the same M5. The tested candidate included all
main changes through `142140a`. The agreed checks covered launch focus and input,
switching away during startup and returning, ordinary and rapid switching,
minimize/restore, and brief rendering, sound and gameplay across the games'
available fullscreen, borderless and windowed modes. First-run initialization,
other macOS versions and multiple displays remain unverified.
Immediate restore before native minimization starts and fullscreen resolution
changes remain separate issues (#30).

**Known visual regression:** fullscreen minimization on Cmd+Tab animates behind
the newly focused app. Lowering the inactive window from level 21 to 0 changes
which app is drawn above the animation. The observed traces do not establish an
additional minimization delay.

Install the Steam integration through Hadron's usual setup and restart Steam to
load it. Replacing the runtime library alone does not update the copy in Steam.

## D3D12: a structured buffer view over 2 GB ended the process (fixed, #36)

Jurassic World Evolution 2 died while starting (issue #56), with Metal's failed assertion
`MTLTextureDescriptor has width (532676608) greater than the maximum allowed size of 268435456`
right after vkd3d-proton's `Application is attempting to use more elements in a typed buffer
(532676608) than supported by device (268435456)`. The game makes a structured shader resource
view over a 2 GB buffer. vkd3d-proton writes two descriptors for it: a storage buffer, which
shaders use, and a typed texel buffer of 32-bit words beside it. KosmicKrisp reports Metal's
limit for a texture buffer, 2^28 texels, as `maxTexelBufferElements`, and that is the real limit.
vkd3d-proton noticed that the typed view was larger, and created it anyway, which breaks a valid
usage rule of `vkCreateBufferView`; KosmicKrisp passed it on, and Metal's assertion ends the
process.

vkd3d-proton patch 0004 clamps the typed view to the device's limit when it is the companion
of a raw or structured view's storage buffer descriptor: shaders read that one, and it still
covers the whole buffer. A typed view proper that is too large is left as before (vkd3d-proton
reports that games make them and that AMD's and NVIDIA's drivers read past their stated limit);
on KosmicKrisp it would still fail, and that needs larger texel buffers in the driver.
`tests/win/d3d12bigview.c` makes the same view over a 2 GB upload buffer and reads two words past
element 2^28 in a compute shader: before, the process ended with the same assertion; after, both
words read back as written, and vkd3d-proton logs the clamp. Not tried: the game itself, which is not installed here.
## Display modes: emulated, never switched (fixed, #39)

Teardown in fullscreen on an external monitor (a BenQ RD280UG, 3840x2560 pixels at "looks like"
1920x1280, above a MacBook Pro's built-in display) showed only the top-left part of the game. Its
saved settings asked for resolution 5 of that monitor's list, 3008x2005. Three things were wrong:

- Wine's Mac driver changed the mode of the main display only. For any other display it logged
  `Changing non-primary adapter settings is currently unsupported` and still returned success:
  the game sized its window to 3008x2005 on a monitor that stayed 1920x1280.
- With that fixed (Wine 0030), macOS switched the BenQ to 3008x2005, a Retina mode with a
  6016x4010 backing at 100 Hz, and moved the larger display from above the built-in one to its
  right (-204,-1280 to 1512,-1280). The window stayed behind. Wine 0031 moves windows placed at a
  display's top-left corner with it, and back when the mode is restored.
- Even then the game showed only a sliver, and every switch to another app restored the mode and
  every return set it again. Real mode changes on macOS capture the display, rearrange displays,
  give Retina displays scaled modes and offer modes larger than the desktop (the BenQ's list ran
  to 3840x2560 while its desktop is 1920x1280).

So Hadron no longer changes display modes at all (Wine 0033): win32u's emulated mode changes,
which upstream offers as the `EmulateModeset` option, are on by default on macOS. A game sees
modes up to each monitor's desktop size, a mode change succeeds at once and leaves the display
alone, and the game's window covers the monitor with its picture scaled and its aspect ratio kept.
That is what native Mac games do: they render at a size of their choice and let the system scale
it. `EmulateModeset=n` in `HKCU\Software\Wine\X11 Driver` turns it off; 0030 and 0031 then make
real mode changes work on every display.

Measured with test programs (`fsmode.exe`: a mode change and a popup window covering the
monitor): with emulation, a 1280x800 "mode" on each display leaves both displays as they were,
Win32 sees a 1280x800 monitor, and the window covers the whole display on screen (1512x982 on the
built-in display, 1920x1280 on the BenQ). Direct3D 9 to 11 and Vulkan present into a layer that
Core Animation scales; OpenGL goes through Wine's framebuffer surface (logged as experimental):
`gltri` ran at 102 fps against 106 without, and `glalpha` read back the same pixels. Cost: old
games that set brightness with gamma ramps get no effect, as under Proton. Rendering at a Retina
display's full pixel size is #71; fullscreen in its own Space is #72.

The built-in display of an Apple silicon Mac has no EDID Wine can read, and the one Wine
generates for it was named "Wine Monitor"; games now list it as "Built-in" (Wine 0032).
## .NET: mscoree was switched off for every game (fixed, #37)

Fable Anniversary exited at once (issue #57): `Library mscoree.dll (which is needed by ... Fable
Anniversary.exe) not found`, status c0000135. Its executable imports `mscoree.dll`, the .NET
runtime's loader. Wine has that library, but `scripts/play` started every game with
`WINEDLLOVERRIDES="mscoree,mshtml=;..."`, and `scripts/steam-run` made and updated prefixes the
same way, so that wineboot would not stop at the Wine Mono and Gecko install dialogs. A program
that links against mscoree then cannot load at all, and nothing that needs .NET can run.

The runtime now carries Wine Mono 11.3.0, the version this Wine asks for, as Proton does, in
`dist/share/wine/mono/wine-mono-11.3.0` (`scripts/fetch-wine-mono.sh`: the project's arm64 release,
which also holds the x86 and x86-64 runtimes, checked against a pinned checksum). Wine finds a
shared install there by itself, and when a prefix is made, mscoree installs the small support
package from it without a dialog. Only `mshtml` is still switched off. Measured with a new prefix:
wineboot took 23 s instead of 18 s, once, and the prefix grew by 7 MB; the runtime grows by
240 MB on disk, about 43 MB compressed.

`tests/win/mscoree-import.c` imports mscoree as Fable does: with mscoree off it fails with Fable's
error; now the 32-bit and the 64-bit builds load and print the runtime version. C# programs
compiled with Wine Mono's own `mcs.exe` (itself a .NET program) run as 32-bit and 64-bit, from a
checkout and from a packaged runtime. In a prefix made before this change, .NET programs run too;
mscoree logs that a registry key is missing until Wine next updates the prefix and installs the
support package. A new prefix records .NET 4.8.1 as installed, so Steam's install scripts skip
the .NET redistributable. Not tried: Fable itself, which is not installed here, and any game in
it; whether Fable's mixed native and .NET code runs on Wine Mono is the next question.
## Media Foundation could not play anything: no GStreamer (fixed, #38)

A game that plays a video or sound file through Media Foundation, the usual path on Windows for
Unity's VideoPlayer and Unreal's media framework, got nothing (issue #69; likely part of #59).
`tests/win/mfplay.c` reads a file through a source reader and counts what it decodes. On 0.3.0 an
MP4 (H.264 and AAC) failed at once: `class {317df618-...} not registered`,
`MFCreateSourceReaderFromURL failed: 0xc00d36c4`. Two parts were missing:

- Wine's MP4, AVI, ASF, WAV and MP3 sources hand a file to its GStreamer handler unless a registry
  value says otherwise, even when FFmpeg (winedmo) could read it, and Hadron had no GStreamer.
- With that value set, the file opened through FFmpeg, and then no decoder existed: in this Wine
  the H.264 and AAC decoders (and the others) are winegstreamer's, and winedmo only demuxes.
  Upstream Wine's winedmo is still demux-only.

So Hadron now ships GStreamer, as Proton does: `scripts/build-gstreamer.sh` builds 1.28.8's core,
the base plugins winegstreamer uses (decodebin, conversion, resampling), demuxers for MP4,
Matroska, AVI, WAV and ASF, the stream parsers, and gst-libav, which decodes with Hadron's own
FFmpeg (now built with libavfilter, without filters, because gst-libav links it). All LGPL; GLib
comes from Homebrew and is bundled. Wine is configured with it and builds winegstreamer.
`scripts/play` gives the game only the runtime's plugins and a registry for each runtime build in
`~/Library/Caches/Hadron`. It adds 17 MB of GStreamer and a few MB of GLib to the runtime.

Measured with `mfplay.exe`: an MP4 decodes all 60 frames and 88 audio buffers, 64-bit and 32-bit;
a WMV with WMA audio decodes too. From a packaged runtime, in a fresh prefix, the same, and the
registry lists only the runtime's 20 plugins. A prefix made before this is updated by Wine on the
next start (13 s once) and then plays. WebM does not open (`0xc00d36bb`), with VP9 and Opus or
Vorbis; Windows' own Media Foundation needs an add-on for WebM too. winegstreamer logs
`gst_video_info_from_caps: assertion 'gst_caps_is_fixed (caps)' failed` once per stream, which
did not affect decoding. Not tried: DirectShow playback (quartz, which now also has GStreamer to
use), and a game.

## Raft and Cities: Skylines acceptance, launcher still open (#40)

On an M4 with 16 GB RAM and macOS 27.0.1, the player confirmed Raft and Cities: Skylines
working on 2026-10-16. Raft's preceding world-load log completed at 10878 MB physical
footprint and 2021 MB additional swap, with warning rather than critical pressure.
The watchdog policy/accounting work is proposed separately in PR #77; this entry does not
attribute acceptance to the alternate-entry feature or claim faster startup.

Cities works through Steam with the player's explicit `HADRON_GAME_EXE=Cities.exe` preference.
The normal Paradox Launcher remains unresolved: GPU/renderer failures and visibly displaced
click targets were observed. The direct route is a requested opt-in workaround, not evidence
that the launcher is fixed. No title-specific setting is shipped in Hadron's defaults.
Steam integration and the existing save prefix/watchdog remain active.
See [entry-point evidence and limitations](fixes/steam-direct-entry.md).
The accepted local runtime also contained diagnostic Wine/DXMT changes; these reports do not
establish acceptance of a packaged current-main release or isolate each component's contribution.

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
