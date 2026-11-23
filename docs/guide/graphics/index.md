# What runs where

A Windows game draws through one of a few graphics APIs: Direct3D 9, 10, 11 or 12, Vulkan, or
OpenGL. Macs have none of them; they have Metal. Hadron translates each API to Metal, and a
different component does the work for each. Which one a game gets is decided by the API it uses,
not by a setting.

| The game uses | Translated by | Runs on |
|---|---|---|
| [Direct3D 9](./direct3d-9) | mtld3d | Metal, directly |
| [Direct3D 10 and 11](./direct3d-11) | DXMT | Metal, directly |
| [Direct3D 12](./direct3d-12) | vkd3d-proton, to Vulkan | KosmicKrisp, then Metal |
| [Vulkan](./vulkan) | (nothing to translate) | KosmicKrisp, then Metal |
| [OpenGL](./opengl) | Zink, to Vulkan | KosmicKrisp, then Metal |

**KosmicKrisp** is the Vulkan driver for Metal in [Mesa](https://mesa3d.org), the open-source
graphics driver project, with Hadron's patches. It runs on Metal 4. Three of the paths go through
it, so a fix there helps Direct3D 12, Vulkan and OpenGL games at once.

Direct3D 9 to 11 skip it: mtld3d and DXMT turn those calls into Metal themselves.

## What this means for you

- **Settings are per path.** MetalFX upscaling is DXMT's, so it is for Direct3D 10 and 11 games;
  the frame latency setting is vkd3d-proton's, for Direct3D 12. `HADRON_VSYNC` covers Direct3D 9
  to 11. Each page says what applies.
- **Bugs are per path.** A game that draws wrongly usually shows a bug in its path's translator or
  driver, and another game on the same path may share it. The
  [list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) names
  each game's API.
- **Testing differs.** KosmicKrisp and Zink are checked against the Khronos conformance suites,
  the official test suites for Vulkan and OpenGL
  ([results](https://github.com/Broyojo/hadron/blob/main/docs/conformance.md)). Those suites say
  nothing about Direct3D 9 to 11, which never reach KosmicKrisp.
- **Drivers report only what they implement.** Hadron does not claim features the driver lacks to
  get a game past its start-up checks.

## Under the graphics

Below the graphics, every game runs on the same two layers: [Wine](https://www.winehq.org), built
natively for arm64, provides the Windows API, and [FEX](https://github.com/FEX-Emu/FEX)
translates the game's x86 code (64-bit and 32-bit). The repository's
[architecture notes](https://github.com/Broyojo/hadron/blob/main/docs/architecture.md) go into
detail.
