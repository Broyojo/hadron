# Direct3D 9

**Translated by [mtld3d](https://github.com/athei/mtld3d), straight to Metal.**

Direct3D 9 is the API of many older games, often 32-bit ones. Hadron runs it through mtld3d, which
turns Direct3D 9 calls into Metal without going through Vulkan or OpenGL.

## Games on this path

Portal and Portal 2 play fully; Five Nights at Freddy's and Ultimate Custom Night run well. The
[list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) has the
details.

## Why mtld3d

Wine has its own Direct3D 9, which draws with OpenGL. For a 32-bit game it copied every mapped
buffer into 32-bit memory, which made Portal stutter and then crash out of memory. mtld3d avoids
that by drawing with Metal directly.

## Settings that apply

| Setting | Effect |
|---|---|
| `HADRON_VSYNC=game` / `on` / `off` | Vsync. `game` follows the game's own setting, as on Windows ([Settings](../settings#display-and-frame-pacing)). |
| `HADRON_LARGE_ADDRESS_AWARE=0` | 32-bit games get the full 4 GB address space by default; this limits those not marked large address aware to 2 GB, as on Windows. |
| `HADRON_D3D9=wined3d` | Uses Wine's own Direct3D 9 instead, to compare when a game draws wrongly. |

MetalFX upscaling is not available on this path.

## Good to know

Each game's prefix holds its own copy of mtld3d. Hadron brings it up to date on every launch, so a
game always runs the mtld3d of the Hadron version you have.
