# Test games

Hadron is general-purpose: fixes go into shared layers (Wine, FEX, graphics, the Steam
bridge), never game-specific hacks. This set covers the main technology combinations so a fix
for one game is checked against the others. All are the **Windows** versions.

| Game | App ID | Arch | Graphics | Engine | Why |
|---|---|---|---|---|---|
| Portal | 400 | i386 | D3D9 | Source | 32-bit WoW64, D3D9, Steam bridge Seen once each, not reproduced since: an "Illegal termination of worker thread" assert on quit (threadtools.cpp:1667; a later quit was clean), and before the 4GB change an "Engine Error: failed to allocate minimum memory (48 MB)". |
| Five Nights at Freddy's | 319510 | i386 | D3D9 | Clickteam | simple D3D9 sanity check External displays stay on in fullscreen (Wine 0016). The office edges warp by design (the game's panorama effect, `perspective.mfx`), but mtld3d draws them differently from wined3d: a ~26-pixel black band at the right edge where wined3d draws image, and a smeared left edge in one capture. Likely the panorama pass's out-of-range samples (address mode or border colour); next step is logging its sampler states. Cosmetic. |
| Among Us | 945360 | x86_64 | D3D11 | Unity | most common indie setup |
| Subnautica | 264710 | x86_64 | D3D11 | Unity | larger Unity game with a native Intel Mac version to compare against |
| Just Cause 3 | 225540 | x86_64 | D3D11 | Apex | heavy AAA, performance |
| SpaceEngine | 314650 | x86_64 | OpenGL | custom | OpenGL path |
| Half-Life | 70 | i386 | OpenGL | GoldSrc | old 32-bit OpenGL |

| Teardown | 1167630 | x86_64 | D3D12 | custom | D3D12 through vkd3d-proton on KosmicKrisp |
| Geometry Dash | 322170 | x86_64 | OpenGL | Cocos2d-x | OpenGL on Zink |
| Subnautica 2 | 1962700 | x86_64 | D3D12 | Unreal Engine 5 | shader model 6.6, Nanite: what most Unreal Engine 5 games need |

Later: Vulkan titles.

Out of scope for now: games with kernel or EAC/BattlEye anti-cheat, and games that require
third-party launchers (EA app, Rockstar launcher).

## Running

```sh
toolchains/depotdownloader/DepotDownloader -app <id> -os windows -all-archs -qr -dir games/<name>
HADRON_LOG=1 scripts/play <id> games/<name>/<game>.exe [args]
```

Through Mac Steam (the normal way, with the Steam bridge): `scripts/build-steam-play.sh`, then
`scripts/steam-install` once, and install and play Windows games from the Steam library as usual.
Games with a Mac version need Properties -> Compatibility -> Hadron to get their Windows build. Each launch
logs to `steamapps/compatdata/<appid>/hadron-run.log` and `build/logs/<appid>-*.log`.

Manual launches (`scripts/play`) have no Steam client serving them, so run them with
`HADRON_DISABLE_LSTEAMCLIENT=1`; that also keeps games from loading the Windows `steamclient.dll`.

## Status

| Game | Result | Notes |
|---|---|---|
| Portal | **Playable and smooth** at 3024x1964 (full Retina), long sessions, no crashes; Bink intro videos play, then the menu | mtld3d (the default; `HADRON_D3D9=wined3d` for Wine's own), Steam bridge off until Steam Play integration. Earlier stutter/crash (wined3d buffer copies), lag spikes (W^X flips) and the black/crashing intro (hidden Metal view, steamclient address-space leak) fixed; see docs/findings.md. Seen once each, not reproduced since: an "Illegal termination of worker thread" assert on quit (threadtools.cpp:1667; a later quit was clean), and before the 4GB change an "Engine Error: failed to allocate minimum memory (48 MB)". |
| Five Nights at Freddy's | **Runs**, menu renders and plays; fullscreen centered | mtld3d. Fullscreen sets a real 1280x800 display mode (Wine captures the display; Cmd-Tab leaves it). The earlier offset/cropped fullscreen image (a mode set before the app was active wasn't reported, so the window was sized for the old desktop) is fixed by Wine 0013; see docs/findings.md #15. External displays stay on in fullscreen (Wine 0016). The office edges warp by design (the game's panorama effect, `perspective.mfx`), but mtld3d draws them differently from wined3d: a ~26-pixel black band at the right edge where wined3d draws image, and a smeared left edge in one capture. Likely the panorama pass's out-of-range samples (address mode or border colour); next step is logging its sampler states. Cosmetic. |
| Ultimate Custom Night | **Runs well**: 60 fps (the game's fixed rate) on the menu, nights mostly 60 | 32-bit D3D9 (Clickteam), mtld3d. Fixed: quarter-size frame (Wine 0014), crash on GO (4GB address space, Wine 0015), 12 -> 60 fps (mtld3d 0001 system-memory draws, 0002 `DebugSetMute`). Some 20-33 ms stretches in nights still to look at. See docs/findings.md #16-#19. |
| Among Us | **Runs**, menu and local lobby render and play smoothly | 64-bit Unity (IL2CPP), D3D11 through DXMT on the first try; ~1.5 GB. Its vsync setting works (DXMT 0001). Installed and launched from Mac Steam through Hadron's Steam Play integration, online sign-in works (docs/findings.md #21). |
| Subnautica | **Runs well** | Played by hand from Mac Steam on 2026-10-03: smoother than the game's own Intel macOS version under Rosetta. Not profiled yet. |
| Portal 2 | **Plays fully** from Mac Steam through Hadron | D3D9 (Source), mtld3d. With an external display, games open on the main (menu-bar) display, and moving a running game between displays is unreliable. |
| Teardown | **Runs** | D3D12 through vkd3d-proton on KosmicKrisp, at feature level 12_0 with no overrides (docs/findings.md #22). The first launch after a driver change is slow while pipelines compile. Frame rate is held back by CPU translation; open. |
| Geometry Dash | **Runs** | Ran on Apple's OpenGL until 2026-10-04, when OpenGL moved to Zink; not checked on Zink yet. Signing in to a Geometry Dash account crashed on 2026-09-30; on 2026-10-02 signing in and relaunching both work, cause not identified. |
| Subnautica 2 | **Being tested** | Unreal Engine 5 requires its SM6 tier: shader model 6.6 and 64-bit atomics on typed resources. Fixed on the way (docs/findings.md #23): the Visual C++ runtime check (Wine 0019), SSE4.2 reported to x86 code (Wine 0020), D3D12 through vkd3d-proton for every game, and the Vulkan features vkd3d-proton needs to report shader model 6.6 (Mesa 0026-0029). |
