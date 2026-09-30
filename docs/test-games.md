# Test games

Hadron is general-purpose: fixes go into shared layers (Wine, FEX, graphics, the Steam
bridge), never game-specific hacks. This set covers the main technology combinations so a fix
for one game is checked against the others. All are the **Windows** versions.

| Game | App ID | Arch | Graphics | Engine | Why |
|---|---|---|---|---|---|
| Portal | 400 | i386 | D3D9 | Source | 32-bit WoW64, D3D9, Steam bridge |
| Five Nights at Freddy's | 319510 | i386 | D3D9 | Clickteam | simple D3D9 sanity check |
| Among Us | 945360 | x86_64 | D3D11 | Unity | most common indie setup |
| Just Cause 3 | 225540 | x86_64 | D3D11 | Apex | heavy AAA, performance |
| SpaceEngine | 314650 | x86_64 | OpenGL | custom | OpenGL path |
| Half-Life | 70 | i386 | OpenGL | GoldSrc | old 32-bit OpenGL |

Later: D3D12 (vkd3d-proton / DXMT D3D12) and Vulkan titles.

Out of scope for now: games with kernel or EAC/BattlEye anti-cheat, and games that require
third-party launchers (EA app, Rockstar launcher).

## Running

```sh
toolchains/depotdownloader/DepotDownloader -app <id> -os windows -all-archs -qr -dir games/<name>
HADRON_LOG=1 scripts/play <id> games/<name>/<game>.exe [args]
```

Until Steam Play integration exists, games aren't launched by Mac Steam, so its client never
maps them and the Steam bridge hangs; run with `HADRON_DISABLE_LSTEAMCLIENT=1` meanwhile. That
also keeps games from loading the Windows `steamclient.dll`, so they run without Steam.

## Status

| Game | Result | Notes |
|---|---|---|
| Portal | **Playable and smooth** at 3024x1964 (full Retina), long sessions, no crashes; Bink intro videos play, then the menu | mtld3d (`HADRON_D3D9=mtld3d`), Steam bridge off until Steam Play integration. Earlier stutter/crash (wined3d buffer copies), lag spikes (W^X flips) and the black/crashing intro (hidden Metal view, steamclient address-space leak) fixed; see docs/findings.md. |
| Five Nights at Freddy's | **Runs**, menu renders and plays | Image offset down/right in fullscreen. mtld3d log: fullscreen 1280x800 back buffer, no mode-set, window covers 1512x982, layer 1512x982pt @1x — sizes consistent, so suspect the Metal view/client surface keeps a stale origin after FNaF's window goes fullscreen (Portal's fullscreen is fine). Next: check client-surface/view frame updates in winemac for Wine patch 0007/0012 views on window resize. |
