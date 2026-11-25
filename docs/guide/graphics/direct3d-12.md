# Direct3D 12

**Translated by [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) to Vulkan, which
runs on KosmicKrisp, on Metal.**

Direct3D 12 is the API of many recent games, Unreal Engine 5 ones among them. Hadron runs it
through vkd3d-proton, which turns it into Vulkan. The Vulkan driver is KosmicKrisp,
Mesa's Vulkan driver for Metal, with Hadron's patches.

## Games on this path

Teardown runs, at about 60 fps in a level on an M2 Pro (render scale 100%, quality Low), with its
frame rate held back by translating its x86 code. Subnautica 2 (Unreal Engine 5) does not load
yet: Metal's shader compiler gives up on its largest compute shaders. The
[list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) has the
details.

## What Direct3D 12 offers here

vkd3d-proton reports a Direct3D 12 feature only when the Vulkan driver really has what it needs.
As last measured, that is feature level 12_0 and shader model 6.6. Ray tracing, mesh shaders and
variable rate shading are not there, so games that require them do not run.
[D3D12 coverage](https://github.com/Broyojo/hadron/blob/main/docs/d3d12-coverage.md) has the full
list, with its date.

## Good to know

- **The first launch is slow**, and so is the first launch after an update that changes the
  driver, while the game's pipelines compile. vkd3d-proton keeps them in the game's prefix
  (`pfx/shadercache`) for later launches.
- **One frame ahead.** A game that waits for each frame to be shown before it starts the next gets
  to run one frame ahead, which keeps the GPU from idling at its lowest clock. Teardown went from
  16 to about 60 fps with it. `VKD3D_SWAPCHAIN_MIN_LATENCY_FRAMES=1` gives a game exactly the
  latency it asks for, one frame less, which suits a game that already reaches the display's
  refresh rate.
- **Shaders Metal refuses** are saved in `~/Library/Logs/Hadron/failed-msl`. Each is a driver bug;
  [a report](../troubleshooting#save-a-report) includes the newest ones.
- MetalFX upscaling and `HADRON_VSYNC` do not apply here.

Because Direct3D 12 runs on KosmicKrisp, the Vulkan
[conformance results](https://github.com/Broyojo/hadron/blob/main/docs/conformance.md) apply to it.
