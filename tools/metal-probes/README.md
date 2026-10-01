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
