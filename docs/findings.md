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
    the lock is held until the others stop spinning, and they are waiting for it. Lanes take turns
    instead, so only one lane of a group ever waits. Stages without SIMD-group built-ins use a state
    machine whose exit the compiler can't predict, which keeps the locked section inside the loop.
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
    command buffer fails and bounds the spin, so the GPU is not left busy.

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
