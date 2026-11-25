# Direct3D 10 and 11

**Translated by [DXMT](https://github.com/3Shain/dxmt), straight to Metal.**

Direct3D 11 is what many Windows games use, Unity and Unreal Engine 4 games among them. Hadron runs
Direct3D 10 and 11 through DXMT, which turns them into Metal without
going through Vulkan.

## Games on this path

Among Us, Subnautica, Astroneer (Unreal Engine 4), Besiege and Bravely Default Flying Fairy HD
Remaster run. The
[list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) has the
details.

## Settings that apply

| Setting | Effect |
|---|---|
| MetalFX upscaling | `DXMT_METALFX_SPATIAL_SWAPCHAIN=1 DXMT_CONFIG=d3d11.metalSpatialUpscaleFactor=2.0`, or the **MetalFX Upscaling** menu on the game's Compatibility page. The game renders at its set resolution and MetalFX scales it up by the factor ([more](../performance#metalfx-upscaling)). |
| `HADRON_VSYNC=game` / `on` / `off` | Vsync. `game` follows the game, as on Windows: a windowed game is synced, as Windows' compositor does, unless it asks to tear ([Settings](../settings#display-and-frame-pacing)). |
| `MTL_HUD_ENABLED=1` | Apple's Metal performance overlay. |

Settings for DXMT go in `DXMT_CONFIG`, separated by `;`. Hadron adds its vsync setting to what
you give there.
