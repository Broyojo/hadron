# D3D12 coverage: what vkd3d-proton can offer on KosmicKrisp

vkd3d-proton reports a D3D12 capability only when the Vulkan driver has the feature behind it.
This is the list of what is there and what is missing, so the driver work is a checklist rather
than something each new game discovers. Regenerate the first table with `tools/d3d12probe.c`; the
missing extensions come from comparing vkd3d-proton's `VK_EXTENSION` list with
`kk_physical_device.c`.

State on 2026-10-01 (M2 Pro, macOS 27.0.1, Mesa patches 0001-0030).

## What D3D12 reports

| Capability | Reported | Notes |
|---|---|---|
| Feature level | 12_0 | 12_1 needs conservative rasterization and ROVs |
| Shader model | 6.6 | 6.7 and 6.8 need `VK_KHR_shader_quad_control` |
| Resource binding tier | 3 | |
| Tiled resources tier | 2 | tier 3 needs sparse 3D textures |
| Wave ops, 64-bit integers, native 16-bit ops | yes | |
| 64-bit atomics (typed, descriptor heap, group shared) | yes | |
| Typed UAV load of additional formats, logic ops | yes | |
| Enhanced barriers, relaxed format casting | yes | |
| Sampler feedback | tier 0.9 | emulated by vkd3d-proton |
| 64-bit floats in shaders | no | |
| Barycentrics | no | |
| Mesh shaders | no | |
| Ray tracing | no | |
| Variable rate shading | no | |
| Conservative rasterization | no | |
| Rasterizer-ordered views | no | |
| Depth bounds test | no | reported on Apple GPU family 10 (M5) and later |
| View instancing, programmable sample positions | no | |

## Missing, in the order to do them

"Metal" is what the API is expected to offer. Unless a probe is named, it has not been measured.

### Needed by games that already reach this point

| Gap | Vulkan side | Metal | Seen in |
|---|---|---|---|
| ~~Reserved (sparse) textures with UAV usage~~ done in Mesa 0030 | sparse residency images with `STORAGE` usage | works on macOS 27.0.1 (`tools/metal-probes/sparse-write.m`) | Subnautica 2: `CreateReservedResource` 16384x768x2 `R32_UINT`, fatal |
| A compute shader Metal's compiler runs out of memory on | none: a driver bug, the generated MSL is too large | - | Subnautica 2: three compute pipelines fail to build |
| Wireframe fill mode | `fillModeNonSolid` | `setTriangleFillMode` has lines; points would need emulation | DXVK refuses the device for D3D9/10/11 without it |

### Optional D3D12 features that some games require

| Gap | Vulkan side | Metal |
|---|---|---|
| Shader model 6.7 and 6.8 | `VK_KHR_shader_quad_control` | quad-group functions exist; a small addition |
| Mesh shaders | `VK_EXT_mesh_shader` | object and mesh functions exist |
| Barycentrics | `VK_KHR_fragment_shader_barycentric` | `barycentric_coord` exists |
| Rasterizer-ordered views | `VK_EXT_fragment_shader_interlock` | raster order groups exist |
| 64-bit floats | `shaderFloat64` | none; Mesa can lower doubles to integer code |
| Multisampled UAVs | `shaderStorageImageMultisample` | unprobed |
| Tiled resources tier 3 | `sparseResidencyImage3D` | sparse 3D textures are unprobed |
| Views of 3D slices | `VK_EXT_image_sliced_view_of_3d` | unprobed |
| Ray tracing | acceleration structures, ray query, ray tracing pipelines | exists; the largest item here |

### No equivalent in Metal

| Gap | Vulkan side | Metal |
|---|---|---|
| Conservative rasterization (blocks feature level 12_1) | `VK_EXT_conservative_rasterization` | no API |
| Variable rate shading tier 2 | `VK_KHR_fragment_shading_rate` | rasterization rate maps only, which are per screen region |
| Depth bounds test below Apple GPU family 10 | `depthBounds` | not available |

### Quality and performance, not capabilities

`VK_EXT_descriptor_buffer`, `VK_EXT_graphics_pipeline_library` / `VK_KHR_pipeline_library`,
`VK_EXT_shader_module_identifier`, `VK_EXT_depth_bias_control`,
`VK_EXT_dynamic_rendering_unused_attachments`, `VK_EXT_memory_priority`,
`VK_EXT_pageable_device_local_memory`, `VK_EXT_zero_initialize_device_memory`,
`VK_EXT_image_compression_control`, the surface and present extensions
(`VK_KHR_surface_maintenance1`, `VK_EXT_present_timing`, `VK_KHR_present_mode_fifo_latest_ready`),
`VK_KHR_cooperative_matrix`, `VK_EXT_shader_float8`.

vkd3d-proton also lists vendor extensions (AMD, NVIDIA, Valve) and Windows-only ones
(`win32` external memory and semaphores); those don't apply.

## Known conformance gaps in what is reported

- `dEQP-VK.api.info.image_format_properties.*` fails for every format: with `sparseBinding`, the
  suite requires sparse binding on 1D and 3D images and on multisampled 2D images, and KosmicKrisp
  has sparse images for single-sampled 2D only.
- A shader write to an unmapped tile of a sparse texture reads back for the rest of the kernel
  (Metal keeps it until the command buffer ends), where `residencyNonResidentStrict` wants it
  discarded. Buffers have a guard for this (Mesa 0024); storage images do not yet.
- vkd3d-proton's own tiled resource tests: `test_update_tile_mappings` and its remap variants,
  `test_texture_feedback_instructions`, `test_sparse_default_mapping`, three checks of
  `test_execute_indirect_state`.
- A full Vulkan CTS run has not been done; only the groups for each feature and regression samples.
