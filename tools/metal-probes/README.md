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
- `sparse-residency-views.m`: residency through views of a writable sparse texture (a view's first
  level is ignored: levels 1-2 report the residency of levels 0-1; read-only textures are right)
- `sparse-residency-fault.m`: `sparse_read` on writable sparse textures of some sizes ends the
  command buffer with a GPU address fault (129x129 and 11x37 do, 128x128 does not)
- `sparse-twin.m`: mip tail layouts of read-only and writable sparse textures of one shape, and a
  read-only texture mapped to the same heap pages as a writable one reporting residency for it
- `sparse-mapping-order.m`: mapping a read-only sparse texture, a writable one and the read-only one
  again in one batch crashes in `updateTextureMappings`; a signal or a commit in between avoids it
- `sparse-3d-tail.m`: the mip tail of a placement-sparse 3D texture (1024x128x8 RGBA8: a 10-page
  tail). Mapped in one operation from consecutive heap pages it reads back right; mapped one page
  per operation it does not: tail position n covers several pages (here 4, 4 and 2), so the
  page-sized operations overlap. Why sparse binding stays off for 3D images
- `filter-precision.m`: texture filtering weights take 6 bits between mip levels (65 distinct
  weights from LOD 2 to 3) and 8 bits between texel centres, so the driver reports
  `mipmapPrecisionBits` 6 and `subTexelPrecisionBits` 8
- `varyings.m`: a render pipeline takes 124 user varying components, as scalar members of mixed
  types and interpolation or as `float4` members; built-in fragment inputs do not count. At 125
  scalar components Metal reports the limit; 32 `float4` members crash the compiler service. A
  value interpolates to the same bits in a scalar member and in a vector component
- `sparse-3d-units.m`: which heap pages each mip tail position of a placement-sparse 3D texture
  covers, found by reading the heap back through a buffer placed over the same pages
  (`WHOLE=1` maps the whole tail in one operation, `PAGES=1` prints the page map, `BPP1=1` and
  `BPP16=1` change the format). Positions cover different numbers of pages (4, 4 and 2 for a
  1024x128x8 RGBA8 texture; one position covers all 64 pages of 256x256x256), and the tail can
  use more heap pages than `tailSizeInBytes` (R8 1024x128x8: 20 reported, pages up to 22 written)
- `indirect-threads.m`: a compute pipeline from `MTL4Compiler` with `maxTotalThreadsPerThreadgroup`
  set, dispatched with `dispatchThreadsWithIndirectBuffer`, computes wrong values in most threads
  (36 to 43 of 48 for a kernel holding 32 floats live, against a CPU reference, with limits from
  16 to 128; with a limit and groups of 8 threads or fewer it is right, and kernels holding 8 or
  64 floats live are right too). The same pipeline dispatched directly, or with
  `dispatchThreadgroupsWithIndirectBuffer`, is right, and so is one left at the default limit. Why
  the driver leaves the limit unset for its emulation passes
- `depth16-clear.m`: a render pass that clears a `Depth16Unorm` texture to n/65535 stores n - 1 for
  half of all n: Metal truncates the clear value where Vulkan asks for rounding to nearest (32,895
  of 65,536 values wrong). A depth written by rasterization rounds. `BIAS=0.25` shows the fix the
  driver uses: clear to (round(d * 65535) + 0.25) / 65535
- `sparse-bc-tail.m`: block-compressed placement-sparse textures with full mip chains, every level
  written and read back for every size in a grid (`FMT=BC1|BC7|ETC2|EAC|ASTC`): for some sizes two
  levels of the mip tail share memory (39 of 1,225 sizes for BC1, BC7, ETC2 and EAC; a 51x65 BC1
  texture loses its last level-0 block to level 3). Uncompressed formats do not show it
