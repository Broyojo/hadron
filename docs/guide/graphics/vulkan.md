# Vulkan

**Runs on KosmicKrisp, Mesa's Vulkan driver for Metal, on Metal 4.**

A Vulkan game needs no translation of its API: Wine passes its Vulkan calls to KosmicKrisp, the
Vulkan driver for Metal in [Mesa](https://mesa3d.org), with Hadron's patches. MoltenVK, the other
Vulkan layer for Macs, is not used; it cannot create a Vulkan instance under Wine.

KosmicKrisp is also what Direct3D 12 and OpenGL games run on, so most of the work on it serves
all three.

## Games on this path

None of the games tried so far is a Vulkan game; testing Vulkan titles is still to come. If you
try one, [report how it runs](https://hadrondb.com).

## How it is tested

KosmicKrisp is checked against the Vulkan conformance suite (Vulkan CTS), the official Khronos
tests. [The results](https://github.com/Broyojo/hadron/blob/main/docs/conformance.md) list what
passes and the gaps that are left, some on purpose until a game needs them.

## Good to know

- **Shaders Metal refuses** are saved in `~/Library/Logs/Hadron/failed-msl`, and
  [a report](../troubleshooting#save-a-report) includes the newest ones.
- MetalFX upscaling and `HADRON_VSYNC` do not apply here.
