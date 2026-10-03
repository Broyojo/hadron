# Conformance testing

The graphics layers Hadron adds, KosmicKrisp (Vulkan on Metal) and Zink (OpenGL on Vulkan), are
checked against the Khronos conformance suites. The suites are the ground truth: a bug found by a
test is found once, with a small reproduction, instead of in a game where it could be anything.

What this covers: Direct3D 12 games (vkd3d-proton runs on KosmicKrisp), Vulkan games, and OpenGL
when it goes through Zink. It does not cover Direct3D 9 (mtld3d) or Direct3D 10 and 11 (DXMT):
those translate straight to Metal and never reach KosmicKrisp. Nothing here says anything about
them; their ground truth would be Wine's Direct3D tests, which have not been run against them.

A pass here does not mean a game works. The suites do not cover optional features an
implementation does not claim, and they say nothing about shader size or compile time (see
findings 23 in [findings.md](findings.md)).

## Running

```
scripts/build-cts.sh            # the suites and deqp-runner, once
scripts/cts.sh gles3            # one suite; results in build/cts-results/<name>/
```

| `scripts/cts.sh` | Suite | Tests | Time (6 jobs, M2 Pro) |
|---|---|---|---|
| `vk`, `vk-full` | Vulkan CTS 1.4.6.2, on KosmicKrisp | 3,247,552 | 2 hours 40 minutes |
| `gles2`, `gles3`, `gles31` | dEQP OpenGL ES, on Zink | 14,371 / 42,804 / 37,653 | 2 / 5 / 10 minutes |
| `gl30` ... `gl33` | Khronos OpenGL, on Zink | 878 / 888 / 5,661 / 9,886 | under 5 minutes each |
| `gles2-khr`, `gles3-khr`, `gles31-khr` | Khronos OpenGL ES, on Zink | 473 / 6,498 / 4,101 | under 2 minutes each |

`results.csv` has one line per test, `failures.csv` everything that did not pass or skip, and each
failing test leaves its log next to them.

Rules that keep the results worth something:

- The suite source is never edited. `scripts/build-cts.sh` refuses a modified checkout.
- A driver change is tested before it is installed: `VK_DRIVER_FILES=<icd.json> scripts/cts.sh ...`
  runs the suite on another KosmicKrisp build, for instance an ICD file whose `library_path`
  points into `build/mesa`. Installing over a driver that a run is using mixes two drivers in one
  result.
- A failure that comes and goes is not written off as flaky. Run it with `MESA_SHADER_CACHE_DIR`
  set to an empty directory, and with `MESA_SHADER_CACHE_DISABLE=true`: the one unstable group so
  far was a cache key collision (0043 below).
- The Vulkan validation layer (`brew install vulkan-validationlayers`,
  `ZINK_DEBUG=validation VK_LAYER_PATH=/opt/homebrew/share/vulkan/explicit_layer.d`) tells whether
  Zink or the driver is at fault when Metal rejects something.

## Results

OpenGL and OpenGL ES on Zink on KosmicKrisp, 2026-10-02. "First" is the first full run, "now" is
with the first eight fixes below (0038-0045). Skips are tests for features the implementation does not claim.

| Suite | Tests | First: fail / crash | Now: pass | Now: fail / crash | Skipped |
|---|---|---|---|---|---|
| OpenGL ES 2 (dEQP) | 14,371 | 4 / 0 | 14,310 | 2 / 0 | 57 |
| OpenGL ES 3 (dEQP) | 42,804 | 350 / 19 | 42,459 | 16 / 19 | 305 |
| OpenGL ES 3.1 (dEQP) | 37,653 | 2,551 / 39 | 34,265 | 135 / 41 | 3,198 |
| OpenGL ES 2 (Khronos) | 473 | 3 / 324 | 468 | 0 / 0 | 5 |
| OpenGL ES 3 (Khronos) | 6,498 | 10 / 9 | 6,476 | 0 / 0 | 22 |
| OpenGL ES 3.1 (Khronos) | 4,101 | 20 / 0 | 3,938 | 16 / 0 | 146 |
| OpenGL 3.0 | 878 | 10 / 2 | 859 | 3 / 1 | 15 |
| OpenGL 3.1 | 888 | 10 / 0 | 864 | 3 / 0 | 21 |
| OpenGL 3.2 | 5,661 | 215 / 2 | 5,638 | 4 / 1 | 18 |
| OpenGL 3.3 | 9,886 | 218 / 10 | 9,577 | 7 / 1 | 299 |

Two more tests in ES 2 and five in ES 3 pass with a warning (line interpolation). The first ES 3
run also lost 498 tests to a mistake in the harness (a test named twice in the lists), so its
numbers are from the second run, which already had four of the fixes. In ES 3.1, 13 tests fail
once and pass when run again, different ones each run and mostly `copy_image`; that is counted
as open, not as passing (see below).

Vulkan on KosmicKrisp, full must-pass list, 2026-10-02 (2 hours 40 minutes at 6 jobs, with all
the fixes below): 3,247,552 tests, 647,905 pass, 2,599,079 skipped, 348 fail, 109 crash, 1 timeout,
6 warnings, and 104 that failed once and passed on a second try. Results in
`build/cts-results/vk-full`; `scripts/cts.sh vk-full` reruns it list by list.

| Group | Count | What it is |
|---|---|---|
| `api.info.image_format_properties` | 173 fail | Sparse binding was limited to 2D single-sample colour images, while the driver reports `sparseBinding`, which requires it for every image type and sample count a format supports. Fixed by 0049 except for 3D (open item 7): 57 remain. |
| `texture.swizzle`, `texture.compressed` | 134 fail | A Metal bug: in a sparse texture of a block-compressed format, Metal's mip tail places two levels on the same memory for some sizes (39 of 1,225 sizes from 4x4 to 140x140 for BC1, BC7, ETC2 and EAC, mostly where a level is 17 or 33 blocks across; power-of-two sizes from 8x8 up are fine, 4x4 is not). Uncompressed formats are clean (0 of 2,209 sizes). Reproduced without the driver by `tools/metal-probes/sparse-bc-tail.m`. 0051 keeps compressed formats out of sparse images: these tests are now unsupported, 54 `image_format_properties` tests for compressed formats fail instead, and 1,486 sparse tests on compressed formats that passed are skipped. |
| `memory_model.message_passing`, `write_after_read` | 79 crash, most of the 104 retries | Lost devices: Metal ends the command buffer with a timeout (`MTL4CommandQueueErrorDomain` error 1). The tests run long shaders, and with six test processes sharing the GPU some exceed Metal's time limit. The same on the driver before and after this round's fixes; open item 8. |
| `glsl.440.linkage.varying.component.frag_out` | 22 crash | `nir_lower_blend` expects one store per colour output; outputs written per component broke it. Fixed by 0050. |
| `spirv_assembly...opfma.fp32...denorm_preserve` | 16 fail | The emulated `fma` for denormal operands rounded twice. Fixed by 0046. |
| `clipping.user_defined` through tessellation and geometry | 10 fail | Clip and cull distances read in the fragment shader after tessellation and geometry stages. |
| `dgc.ext` | 6 crash (SIGSEGV) | Device-generated commands, an experimental feature. |
| Stencil: `multisample.std_sample_locations...stencil`, `depth_stencil_write_conditions...d32sf_s8ui`, `transient_attachment_bit.stencil_load_store_op_test_local_bit` | 10 fail | Stencil kept across render passes or written with discards, on the combined depth/stencil format. |
| `glsl.builtin.precision.atan2.highp` | 4 fail | `atan(y, x)` with `x` far larger than `y` returns 0 where a tiny value is expected. |
| `spirv_assembly...float16...tessc` | 2 crash | 16-bit floats in a tessellation control shader. |
| `tessellation.geometry_interaction.limits.output_required_max_geometry` | 1 timeout | |

The skipped tests are features the driver does not report: shader objects and graphics pipeline
libraries, ray tracing, variable rate shading, mesh shaders, the primitives generated query,
custom border colours (off by default), depth bounds, several formats, and many things that do not
apply on a Mac (other platforms' memory sharing, separate queue families, video).

## Bugs found and fixed

All are Mesa patches in `patches/mesa`.

| Patch | Bug | Tests it fixed |
|---|---|---|
| 0038 | KosmicKrisp converted the value a fragment shader writes when its type differs from the render target's (float written to an integer target became `uint(value)`). Vulkan leaves this undefined, hardware drivers keep the bits, and Zink's copy shaders depend on that: every upload, read back or copy between a signed and an unsigned integer format gave zeros. | 204 per OpenGL 3.2+ run (`packed_pixels`), 2,417 in ES 3.1 (`copy_image`) |
| 0039 | Reading `gl_ClipDistance` or `gl_CullDistance` as a whole array in a later stage asserted: only writes were split into scalars before the pass that requires it. | 1 to 9 crashes per run |
| 0040 | A 3D image gets a 2D array twin for 2D views. With depth larger than width and height, the twin was asked for more mip levels than a 2D texture of that size can have, and Metal aborted. | 324 crashes (`texture_3d`), 5 failures |
| 0041 | Zink told the Vulkan driver that infinities and NaNs never occur in any OpenGL shader. Drivers for hardware ignore the hint; KosmicKrisp turns it into Metal fast math, where `isinf(1e2048)` is false. OpenGL ES 3 requires infinities to work. | `number_parsing` (2); the `aggressive_optimizations` cosine tests (6) pass with it as well |
| 0042 | Draws that had to be unrolled into an index list for another reason (8-bit indices, a restart index Metal does not have) also had their vertices reordered for the OpenGL provoking vertex, even when no input is flat shaded. Reordered lines start from the other end and no longer match the same lines drawn without indices. | `primitive_restart` (72), `buffer.map` and `buffer.write` index arrays (36) |
| 0043 | The shader cache key had "is a point list" but not "lines or triangles", while the transform feedback stage is compiled for one of the two. A program that captured lines and then triangles got the lines variant back from the cache, in the same process or from disk on the next launch. | half of `transform_feedback` (about 250), unstable from run to run |
| 0044 | Render targets whose channels are stored in another order in Metal (RGBA4 and the other 16-bit formats) swizzle the fragment output. A shader writing fewer than four components was swizzled from components it does not have. | `fragment_out` (20) |
| 0045 | `vkCmdDrawIndirectByteCountEXT` was not implemented (`transformFeedbackDraw` was false), so `glDrawTransformFeedback` drew nothing. A small kernel now turns the captured byte count into an indirect draw. | `transform_feedback.draw_xfb_*` (3 per desktop run); 30 Vulkan suite tests that were skipped now run and pass |
| 0046 | The emulated `fma` used when an operand is denormal multiplied and added with two roundings; an FMA rounds once. Found by the review of #13 and by the Vulkan suite. | 16 `opfma` tests |
| 0047 | Switching a shader class to locked 64-bit atomics waited only for re-recorded command buffers, not ordinary ones, and let new submissions in meanwhile, so native and locked atomics could run at once. Device teardown also freed its buffers before waiting for the GPU. Both now wait for all queue work. | Found by review; the teardown order by the Vulkan suite once completion callbacks read device memory |
| 0048 | 64-bit atomic loads and stores on shared memory and images were not serialised with the locked read-modify-write operations; image locks hashed an undefined coordinate for 1D images; a lock timeout entered the critical section without the lock. A timeout now loses the device instead of corrupting memory. | Found by the review of #13 |
| 0049 | Sparse binding on 1D, multisampled and depth/stencil images (see the table above). | 173 `image_format_properties` tests minus the 57 3D ones |
| 0050 | Colour outputs written per component are gathered into one store before blend lowering. | 22 crashes |
| 0051 | Block-compressed formats no longer take part in sparse images, because of the Metal mip tail bug above. Direct3D 12 games that use tiled BC textures see them reported as unsupported; a workaround would have to avoid Metal's tail layout for those formats. | 134 texture tests: unsupported instead of wrong |

All but 0041 are in the Vulkan driver, not in Zink, so they are not specific to OpenGL: they can
equally be hit by a Direct3D 12 game through vkd3d-proton or by a Vulkan game.

## Open failures, by cause

Ordered by how much they matter.

1. **Varyings that mix types or interpolation in one location.** Vulkan lets two variables share
   a location in different components with different types (a `float` in `.xy` and a `uint` in
   `.z`) and different interpolation. KosmicKrisp emits one Metal struct member per location with
   one type and one interpolation, the last one seen. Zink packs transform feedback varyings this
   way; vkd3d-proton may emit it for Direct3D 12 signatures that pack a float and an integer into
   one register (not checked). Symptoms: `Fragment input(s) user(vary_N) mismatching vertex shader output type(s)`
   from Metal and a failed pipeline, an assertion in `poly_nir_lower_gs.c` (`collect_output_types`)
   when the stage goes through the geometry path, wrong values for the flat component. Fix: one
   struct member per component for user varyings in the Metal emitter, both stages by the same
   rule. It touches every shader, so it waits for the Vulkan baseline. Covers the ES 3.1
   `tessellation.user_defined_io` group (36 crashes, 27 failures) and the remaining ES 3
   `transform_feedback` failures.
2. **Provoking vertex by reordering.** Metal always takes flat inputs from the first vertex.
   KosmicKrisp emulates OpenGL's last-vertex convention by unrolling the draw with rotated
   vertices, and only when the fragment shader has flat inputs. Rotated triangles rasterize to
   depths that differ in the last bit, so a program with a flat input and one without do not
   produce identical depth for the same geometry: a depth pre-pass followed by `GL_EQUAL` shows
   speckles. Mesa's linker turns every varying that is constant across a primitive into a flat
   one, so this is more common than it sounds. Only OpenGL is affected, Direct3D 12 uses the first
   vertex. `shaders.invariance.*.loop_2` (2 per ES suite) is this. Options: reorder every draw
   under the last-vertex convention (consistent, costs a compute pass or the vertex cache on every
   OpenGL draw), or fetch the flat inputs of the provoking vertex in the vertex stage without
   reordering. Needs a decision.
3. **Custom border colours.** KosmicKrisp has `VK_EXT_custom_border_color` behind
   `MESA_KK_EXPERIMENTAL=custom_border`. With it 100 of the 102 ES 3.1 `texture.border_clamp`
   failures pass (the two left are depth-compare gathers). It adds a border check to every texture
   sample in every shader, which is the shader size problem of findings 23 again. Without it
   a layer on Vulkan only has the three fixed border colours; Zink warns about that at start, and
   what vkd3d-proton does for a Direct3D 12 game with another border colour has not been checked.
4. **OpenGL above 3.3.** Zink stops at 3.3 because KosmicKrisp has no texel buffers in the
   three-component 32-bit formats (`ARB_texture_buffer_object_rgb32`). Tests that use compute
   shaders through an extension in a 3.3 context assume GLSL 4.20 and fail to compile
   (`cull_distance.coverage`, `CommonBug_ParenthesisInLayoutQualifierIntegerValue`,
   `pipeline_statistics_query_tests_ARB.functional_compute_shader_invocations`,
   `transform_feedback.draw_xfb_stream_*`). The 4.x lists (19,714 tests for 4.6) cannot run until
   then.
5. **Zink assertion in `zink_query.c`** (`q->started_in_rp == ctx->in_rp`) in
   `transform_feedback2.transform_feedback2_states`: a query ended in a different render pass
   state than it began. Not looked into.
6. **Mediump matrix inverse in a vertex shader** (`shaders.matrix.inverse.dynamic.{lowp,mediump}_mat3_float_vertex`):
   wrong by far more than 16-bit precision explains, the same shader as a fragment shader passes.
   Not found yet.
7. **Sparse binding on 3D images.** Vulkan lets an application bind the mip tail of a sparse image
   page by page, from different memory. Metal maps the tail of a 3D texture in units of several
   pages (4, 4 and 2 for a 1024x128x8 RGBA8 texture), so page-sized mappings overlap and corrupt
   it (`tools/metal-probes/sparse-3d-tail.m`). Sparse binding stays off for 3D images until the
   unit sizes can be derived; the 57 `image_format_properties.3d` failures stay with it.
8. **Failures that pass on a second try, and lost devices under load.** 13 per ES 3.1 run, 2 in
   the last OpenGL 3.3 run (`pixelstoragemodes.compressedteximage3d`), and the Vulkan memory model
   tests, whose command buffers Metal ends with a timeout when six test processes share the GPU.
   With one test process the Vulkan memory model tests have 2 crashes and 8 retries instead of
   133 to 163 crashes with six: load, not the driver, apart from those two. The stencil failures
   of the full run (`depth_stencil_write_conditions...d32sf_s8ui` and others) pass when run alone. Six test processes share the GPU during a run, so this may be a race
   or memory pressure in the driver. Not looked into.
9. **Clip and cull distances through tessellation and geometry together** (10 Vulkan tests,
   mostly dynamically indexed arrays): the same counts pass through vertex, vertex+tessellation
   and vertex+geometry.
10. Small ones: `texture_lod_bias` (one combination of sampler and shader bias out of many),
   `pipeline_statistics_query` vertices submitted (a restart index is counted as a vertex), ES 3.1
   `shaders.builtin_constants` and `layout_binding.sampler` crashes, `fbo.no_attachments` timeout,
   geometry shader primitive counters, `gpu_shader5` gather with offsets.
11. Warnings from Zink at start: no `VK_EXT_custom_border_color` (3 above) and no `rectangularLines`
   (wide lines are drawn as parallelograms).

## What the first day showed

Eight bugs in about 123,000 tests, none of which a game had shown yet. The pattern in most of
them is the same: Vulkan
leaves something undefined or optional, hardware drivers all do the same obvious thing, Zink and
vkd3d-proton were written against those drivers, and KosmicKrisp does something else that is
equally legal. The Vulkan suite cannot catch that class, because it only tests what the
specification defines. The OpenGL suites on Zink do, which makes them worth running even for the
Direct3D 12 path.
