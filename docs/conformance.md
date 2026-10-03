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
| `api.info.image_format_properties` | 173 fail | Sparse binding was limited to 2D single-sample colour images, while the driver reports `sparseBinding`, which requires it for every image type and sample count a format supports. Fixed by 0049 except for 3D (open item 1): 57 remain. |
| `texture.swizzle`, `texture.compressed` | 134 fail | A Metal bug: in a sparse texture of a block-compressed format, Metal's mip tail places two levels on the same memory for some sizes (39 of 1,225 sizes from 4x4 to 140x140 for BC1, BC7, ETC2 and EAC, mostly where a level is 17 or 33 blocks across; power-of-two sizes from 8x8 up are fine, 4x4 is not). Uncompressed formats are clean (0 of 2,209 sizes). Reproduced without the driver by `tools/metal-probes/sparse-bc-tail.m`. 0051 keeps compressed formats out of sparse images: these tests are now unsupported, 54 `image_format_properties` tests for compressed formats fail instead, and 1,486 sparse tests on compressed formats that passed are skipped. |
| `memory_model.message_passing`, `write_after_read` | 79 crash, most of the 104 retries | Lost devices: Metal ends the command buffer with a timeout (`MTL4CommandQueueErrorDomain` error 1). The tests run long shaders, and with six test processes sharing the GPU some exceed Metal's time limit. The same on the driver before and after this round's fixes; open item 5. |
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
| 0052 | Zink, when the driver has no `VK_EXT_primitives_generated_query`, emulates `GL_PRIMITIVES_GENERATED` with two Vulkan queries per GL query. Copying their results counted each start of the GL query once per Vulkan query, so after the first restart the later starts were never copied and their primitives were lost. The overflow predicate over four streams had the same fault. | `transform_feedback2_states` (3 primitives instead of 5) |
| 0053 | The same emulation restarts its queries when transform feedback or a geometry shader is switched on or off between draws, and did that inside a render pass even for queries begun outside it, which Vulkan forbids (an assertion in Zink). It now ends the render pass first when such a query is active, and a query start without draws takes the new state without a restart. | `transform_feedback2_states` (crash) |
| 0054 | Texel buffers in the three-component 32-bit formats (`R32G32B32_SFLOAT`, `_UINT`, `_SINT`). Metal has no such texture format, so the shader reads the three words from the buffer's address, with bounds checks. This is what Zink needed for `ARB_texture_buffer_object_rgb32`, the last requirement for OpenGL 4.0: Zink now reports OpenGL 4.6 core and compatibility instead of 3.3. | the OpenGL 4.x suites run at all |
| 0055 | Zink counts a line loop's vertices twice (it draws it as lines) and halves the count when it reads a query back, but not when it writes the result into a buffer object (`GL_QUERY_BUFFER`). | `pipeline_statistics_query` vertices with line loops |
| 0056 | Pipeline statistics counted a primitive restart index as a vertex and computed primitives as if the indices had no restarts. With restart enabled and a statistics query active, a kernel now walks the index buffer and counts each run between restarts. | `pipeline_statistics_query` primitives and vertices with restart (832 mismatches) |
| 0057 | Varyings that share a location with different types or interpolation (a `float` in `.xy` and a flat `uint` in `.z`, which Vulkan allows and Zink and vkd3d-proton emit) became one Metal struct member per location, with the type and interpolation of whichever store came last. Every user varying is now one member per component, each with its own type and interpolation, in the vertex output and the fragment input alike, so the two stages agree without seeing each other. Metal interpolates a scalar member to the same bits as a vector component (`tools/metal-probes/varyings.m`), and its limit is in components, not members, so nothing is lost. Outputs that go to memory (a vertex shader before tessellation or a geometry shader, an evaluation shader before a geometry shader) keep their stores. | OpenGL 4.6 `draw_elements_base_vertex` (5 crashes), ES 3.1 `tessellation.user_defined_io` (36 crashes and 27 failures down to 9 failures) |
| 0058 | The geometry shader emulation asserted when one output location had components of different types. It now keeps such a location as raw 32-bit words and forwards each component with its own type. | the same crashes, through a geometry shader |
| 0059 | The driver reported 128 fragment input components. Metal's limit is 124 user components (built-ins do not count); with 32 `float4` varyings Metal's shader compiler service crashes. It reports 124 now. Vertex, evaluation and geometry outputs stay at 128: Vulkan counts `gl_Position` in those. | `pipeline.*.max_varyings`: 3 tests that were skipped now run and pass |
| 0060 | A 2D view of a 3D image (for rendering into one slice) was served by a 2D array texture aliasing the 3D texture's memory. Rendering now goes to the 3D texture itself, with the slice as Metal's depth plane, so it does not depend on Metal laying out both alike. | OpenGL 4.6 `copy_image` into 3D textures (45, all packed 32-bit formats, which Zink converts by drawing) |
| 0061 | Image copies that change format go through a buffer. Between a 3D image and a layered one they passed the 3D texture an array slice index and sized the buffer without the depth. Found by reading the code: no test in the suites reaches this path with more than one slice. | none (the 16,868 cross-format 3D copy tests pass before and after) |
| 0062 | Zink exposed `ARB_sparse_texture` when the driver had sparse 2D images only, but the extension includes 3D textures. It now requires sparse 3D images too, and `ARB_sparse_texture2` and `_clamp` require the base extension. | 120 `sparse_texture_tests` per OpenGL 4.3+ suite: unsupported instead of failing |
| 0063 | Custom border colours mixed all five components of a sparse sample, the residency code included, which the Metal emitter cannot express. | 144 Vulkan `texture_functions...clamp_to_border.sparse_*` crashes once 0064 was on |
| 0064 | `VK_EXT_custom_border_color` is on by default (it was behind `MESA_KK_EXPERIMENTAL=custom_border`). The emulation costs one branch per sample, and a second sample only for samplers that have a custom colour. | 100 of 102 ES 3.1 `texture.border_clamp`, OpenGL 4.6 `texture_border_clamp` (11), ES 3.1 Khronos (11) |
| 0065 | Dead code left after leaving SSA reached the Metal emitter, whose type inference had no type for it (`UNTYPED!` in the source). | ES 3.1 `shaders.builtin_constants.core.max_compute_work_group_{count,size}` (2 crashes) |
| 0066 | OpenGL's last-vertex provoking convention rotated the vertices of every primitive when the fragment shader had flat inputs, so a program with flat inputs rasterized to slightly different depth than one without (`shaders.invariance.*.loop_2`, speckles in a depth pre-pass with `GL_EQUAL`). Such draws are now unrolled without reordering, and the vertex shader runs a second time for the provoking vertex of its primitive, keeping only the flat outputs. Only draws with flat inputs under the last-vertex convention pay for it, as before. | `shaders.invariance` (2 per ES suite) |

0041, 0052, 0053, 0055 and 0062 are in Zink and 0058 in the geometry shader emulation shared with other Mesa drivers. The rest are in the Vulkan driver, so they are not specific
to OpenGL: they can equally be hit by a Direct3D 12 game through vkd3d-proton or by a Vulkan game.

## Open failures, by cause

Ordered by how much they matter.

1. **Sparse 3D images.** Vulkan lets an application bind the mip tail of a sparse image page by
   page, from different memory. Metal maps the tail of a 3D texture in units of several pages
   that its API does not describe (4, 4 and 2 pages for a 1024x128x8 RGBA8 texture, one unit of
   64 pages for 256x256x256). Mapping the whole tail in one operation from consecutive pages reads
   back right, but for 6 of 30 shapes and formats tried it writes past the size `tailSizeInBytes`
   reports, into whatever memory follows: RGBA8 1024x128x8 (10 pages reported, page 10 written),
   2048x64x4 (37, page 40), 1000x10x3 (4, page 4); R8 1024x128x8 (20, page 22), 2048x64x4 (10,
   page 10); RGBA32 1000x10x3 (14, page 15). All are textures with few slices
   (`tools/metal-probes/sparse-3d-units.m`). A Metal bug like the block-compressed tail overlap;
   with it no tail size from the API is safe, and a tail bound in pieces from scattered memory
   could not be mapped exactly anyway. Sparse binding stays off for 3D images; the
   57 `image_format_properties.3d` failures stay with it. Zink no longer exposes
   `ARB_sparse_texture` (or `_texture2`, `_clamp`) without sparse 3D images (0062), so OpenGL loses
   sparse textures until this is solved.
2. **Doubles in geometry and tessellation evaluation shaders.** `gpu_shader_fp64.fp64.varyings`
   fails for every set of double varyings passed through a geometry shader (through vertex and
   tessellation stages they pass), and `fp64.max_uniform_components` gets wrong transform feedback
   from a tessellation evaluation shader with large double uniform arrays and then never finishes:
   the last command buffer runs for minutes with no error from Metal.
3. **Tessellation evaluation outputs before a geometry shader, stored one component at a time.**
   Splitting those stores (same addresses, same bytes as the packed stores) changes the rendered
   result deterministically: ES 3.1 `tessellation_geometry_interaction.render.passthrough` (2) and
   `.limits` (2) fail through Zink, which already splits them, and the Vulkan passthrough tests
   failed the same way until 0057 left memory outputs packed. Not understood.
4. **Mediump matrix inverse in a vertex shader** (`shaders.matrix.inverse.dynamic.{lowp,mediump}_mat3_float_vertex`):
   wrong by far more than 16-bit precision explains, the same shader as a fragment shader passes.
5. **Failures that pass on a second try, and lost devices under load.** About a dozen per ES 3.1
   run, and the Vulkan memory model tests, whose command buffers Metal ends with a timeout when
   six test processes share the GPU. With one test process the memory model tests have 2 crashes
   and 8 retries instead of 133 to 163 crashes with six. Several ES 3.1 crashes
   (`layout_binding.sampler`, a `copy_image` cube map case) and tessellation tests pass when run
   alone.
6. **Clip and cull distances through tessellation and geometry together** (10 Vulkan tests,
   mostly dynamically indexed arrays): the same counts pass through vertex, vertex+tessellation
   and vertex+geometry.
7. **Fragment inputs: 124 components, OpenGL 4.6 asks for 128.** Metal takes 124 user varying
   components into a fragment shader (`tools/metal-probes/varyings.m`), so the driver reports 124
   (0059) and Zink passes that on as `GL_MAX_FRAGMENT_INPUT_COMPONENTS`, under OpenGL 4.6's
   minimum of 128.
8. **Geometry shader output points with primitive restart** (`geometry_shader.primitive_counter.*_to_points_rp`,
   2-3): with restart and a custom restart index, transform feedback of a geometry shader that
   emits points misses values; strips pass, and points without restart pass.
9. **Side effects in geometry shaders that also need a count pass.** The geometry shader
   emulation runs the shader twice when its output counts are not known statically: a count pass
   with all side effects, then the main pass with stores and unused atomics stripped. An atomic
   whose result the shader uses stays in both and happens twice
   (`shader_atomic_counters.basic-usage-gs`, `geometry_shader.api.max_shader_storage_blocks` in
   OpenGL 4.6 and ES 3.1). The main pass needs the count pass's atomic results instead, through a
   buffer.
10. **Per-patch output block arrays** between tessellation control and evaluation (ES 3.1
   `tessellation.user_defined_io.per_patch_block_array`, 9): the evaluation shader reads the wrong
   value for the first element.
11. **Cube map arrays sampled outside fragment shaders** (`texture_cube_map_array.sampling`):
   `textureLod`, `textureGrad` and `textureGather` in vertex, tessellation, geometry and compute
   shaders, on a 3x3 grid of coordinates per face that includes face edges and corners.
12. Small ones: `shader_ballot` (3, not narrowed to a stage yet), `texture_lod_bias` (one combination of sampler and shader bias, in a vertex shader, a few
   units off: Apple GPUs blend mip levels with 6-bit weights, `tools/metal-probes/filter-precision.m`;
   meeting the test would mean filtering between levels in the shader), `gpu_shader5` gather with offsets (2),
   `clear_tex_image` on a 16-bit depth texture level 4, `framebuffers_texture_layer_attachment`,
   ES 3.1 `fbo.color.texcubearray.rg8ui` and `shaders.linkage...tessellation_geometry.varying.types.mat4`,
   the ES 3.1 depth-compare border colours (custom border colours are not applied to shadow
   samplers), `fbo.no_attachments` timeout.
13. Warning from Zink at start: no `rectangularLines` (wide lines are drawn as parallelograms).

## What the first day showed

Eight bugs in about 123,000 tests, none of which a game had shown yet. The pattern in most of
them is the same: Vulkan
leaves something undefined or optional, hardware drivers all do the same obvious thing, Zink and
vkd3d-proton were written against those drivers, and KosmicKrisp does something else that is
equally legal. The Vulkan suite cannot catch that class, because it only tests what the
specification defines. The OpenGL suites on Zink do, which makes them worth running even for the
Direct3D 12 path.
