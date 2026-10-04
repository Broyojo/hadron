# Metal probes

Standalone programs behind the claims in docs/findings.md #22: what Metal 4 on Apple GPUs actually does,
checked on M2 Pro and M4 instead of assumed. Build one with

    clang -fobjc-arc -framework Metal -framework Foundation <probe>.m -o /tmp/probe && /tmp/probe

- `counter-sets.m`: counter sets and counter heap types (only timestamps exist, so pipeline statistics are counted in software)
- `sparse.m`, `heap.m`, `ptr.m`: placement sparse buffers, heaps and mapping updates
- `tiles.m`, `tail.m`, `view.m`: sparse texture tile shapes, mip tails, residency through views
- `tb.m`, `tb2.m`, `tb3.m`: texel-buffer views of sparse buffers
- `minmax.m`, `minmax2.m`, `minmax3.m`: sampler min/max reduction (ignored below Apple family 10)
- `strict-write.m`: shader writes to unmapped sparse buffer pages read back within the command buffer
- `store-guard.m`, `store-guard-versioned.m`: cost of guarding buffer stores per store (65-100% in a
  store loop) against choosing a guarded or plain copy of the code once (free)
- `atomic64.m`: which 64-bit atomic operations MSL accepts (only `atomic_min`/`atomic_max` without a
  result, on buffers and on RG32Uint textures; every other operation fails to compile)
- `denorm.m`: denormal floats in shaders (32-bit denormals are flushed to zero as operands and as
  results in every math mode, compute and fragment; 16-bit denormals are kept)
- `compute-quads.m`: quad and SIMD groups in compute kernels (threads 4n..4n+3 of a threadgroup, by
  linear index, form a quad for every local size tried: Vulkan's linear derivative groups)
- `compute-lod.m`: implicit-LOD sampling and LOD queries in a compute kernel (always LOD 0: Metal takes no
  derivatives there, so the driver passes gradients from quad operations)
- `atomic64-lock.m`: what makes a locked 64-bit read-modify-write atomic (a plain spin lock deadlocks
  a SIMD group; lanes taking turns works; loads, stores and texture reads need device-scope fences)
- `sparse-write.m`: sparse textures with shader-write usage (sparse tier 1, where read-only ones get
  tier 2): writes land in mapped tiles, residency is reported per tile and per level, a write to an
  unmapped tile reads back within the kernel and is gone in the next command buffer
- `sparse-residency-views.m`: residency (`sparse_read`, `sparse_sample`) through a view of a writable
  sparse texture ignores the view's first level: levels 1-2 of the view report the residency of
  levels 0-1, while values read through the view are right, and read-only textures are right (for
  in-bounds reads; the probe's reads at x=133 on a 128-wide level have no defined residency). The
  probe does not make the heap resident; a minimal Metal 4 version that does shows the same. The MSL
  specification says sparse textures do not support `write` or `read_write` access, while the
  `MTLTextureSparseTier1` header describes writes to them
- `sparse-residency-fault.m`: `sparse_read` on writable sparse textures of some sizes ends the
  command buffer with a GPU address fault (129x129 and 11x37 do, 128x128 does not), whether or not
  any tile is mapped; plain reads are fine, and so are read-only textures. Its tail mapping assumes
  16 KB tiles where placement sparse uses 64 KB, which maps more tail tiles than exist, but the
  fault does not depend on it
- `sparse-twin.m`: mip tail layouts of read-only and writable sparse textures of one shape, and a
  read-only texture mapped to the same heap pages as a writable one reporting residency for it
- `sparse-mapping-order.m`: three `updateTextureMappings` calls in a row on one queue, mapping a
  read-only sparse texture, a writable one and the read-only one again, crash inside Metal
  (`EXC_BAD_ACCESS` in `updateTextureMappings`, with validation on and nothing reported); a signal,
  a wait or a committed command buffer between the writable and the second read-only mapping avoids
  it
- `sparse-3d-tail.m`: the mip tail of a placement-sparse 3D texture (1024x128x8 RGBA8: a 10-page
  tail). Mapped in one operation from consecutive heap pages it reads back right; mapped one page
  per operation it does not: tail position n covers several pages (here 4, 4 and 2), so the
  page-sized operations overlap. Why sparse binding stays off for 3D images
- `filter-precision.m`: texture filtering weights take 6 bits between mip levels (65 distinct
  weights from LOD 2 to 3) and 8 bits between texel centres, so the driver reports
  `mipmapPrecisionBits` 6 and `subTexelPrecisionBits` 8
- `varyings.m`: a render pipeline takes 124 user varying components into a fragment shader, as scalar
  members of mixed types and interpolation or as `float4` members; built-in fragment inputs do not
  count. 124 is Apple's documented limit (Metal feature set tables). At 125 and 126 components Metal
  reports the limit; at 127 or more, of any shape, the compiler service aborts and the pipeline
  fails with a lost connection instead of that message (a Metal bug, but only for shaders already
  over the limit). A value interpolates to the same bits in a scalar member and in a vector
  component
- `sparse-3d-units.m`: which heap pages each mip tail position of a placement-sparse 3D texture
  covers, found by reading the heap back through a buffer placed over the same pages
  (`WHOLE=1` maps the whole tail in one operation, `PAGES=1` prints the page map, `BPP1=1` and
  `BPP16=1` change the format). Positions cover different numbers of pages (4, 4 and 2 for a
  1024x128x8 RGBA8 texture; one position covers all 64 pages of 256x256x256), and the tail can
  use more heap pages than `tailSizeInBytes` (R8 1024x128x8: 20 reported, pages up to 22 written)
- `indirect-threads.m`: a compute pipeline with `maxTotalThreadsPerThreadgroup` set, dispatched with
  `dispatchThreadsWithIndirectBuffer` on a Metal 4 encoder, computes wrong values in most threads
  (36 to 43 of 48 for a kernel holding 32 floats live, against a CPU reference, with limits from
  16 to 128). Deterministic, uniform grids too, with safe math too, and with a pipeline built by the
  legacy API too; API validation reports nothing. Groups of 4 threads are right; groups of 8 are
  right under a limit of 8 or 16 and wrong under 32 and up. Kernels holding 16 or 64 floats live are
  right, 24 wrong; 40 and 48 with a limit of 64 hang the GPU until the command buffer times out. The
  same pipeline dispatched directly, or with `dispatchThreadgroupsWithIndirectBuffer` (right for 16
  to 56 live floats, limits 16 to 128, groups of 8 to 64), is right, and so is one left at the
  default limit. Why the driver dispatches its emulation passes as indirect threadgroups (0076)
- `depth16-clear.m`: a render pass that clears a `Depth16Unorm` texture to n/65535 stores n - 1 for
  half of all n (32,895 of 65,536 values): Metal converts the clear value by truncating, also for the
  exact double n/65535. Metal documents no conversion rule, and Vulkan and OpenGL allow either
  neighbour (rounding to nearest is only preferred), so this is Metal behaviour, not a bug; the
  OpenGL suite's `clear_tex_image` expects the nearest value. A depth written by rasterization
  rounds. `BIAS=0.25` shows the fix the driver uses: clear to (round(d * 65535) + 0.25) / 65535
- `depth-compare-range.m`: `sample_compare` and `gather_compare` against depth texels and references
  outside [0, 1]. `Depth32Float` and `Depth32Float_Stencil8` compare them unclamped, whether filled
  with `replaceRegion` or a blit from a buffer; `Depth16Unorm` clamps the reference to [0, 1], as
  Vulkan asks for fixed-point formats. No difference from the expected answer anywhere
- `sparse-bc-tail.m`: block-compressed placement-sparse textures with full mip chains, every level
  written by blit and read back for every size in a grid (`FMT=BC1|BC7|ETC2|EAC|ASTC`): for some
  sizes copies place two levels of the mip tail on the same blocks (39 of 1,225 sizes for BC1, ETC2
  and EAC, 43 for BC7, where this probe's 256-byte pattern hides 4; ASTC 0). Writing one level then
  the other clobbers whichever came first: aliasing, not a race. Uncompressed formats are clean,
  which this probe cannot show (it has no uncompressed mode)
- `sparse-bc-sampler.m`: the same textures read through the sampler, against a non-sparse texture
  filled the same way (`<w> <h>` for one size, `4 4 grid BC1|BC7|RGBA8` for the grid): blit copies
  and the sampler disagree on where tail levels live, in 265 of 1,225 BC1 sizes (BC7 the same);
  RGBA8 0. Written by an independent review of `sparse-bc-tail.m`
- `sparse-3d-positions.m`: a placement-sparse 3D tail mapped in one operation or one position per
  operation (`OPS=x:page,...`, `X0`, `WIDTH`, `FMT=R8|RGBA8|RGBA32`, args `w h d`): positions cover
  several pages each and positions from 3 up do nothing for 1024x128x8 RGBA8; mapped one position
  per operation back to back, the tail fills exactly `tailSizeInBytes` and every level is right,
  while the single whole-tail operation skips a page and writes one past the tail