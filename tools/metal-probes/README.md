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
