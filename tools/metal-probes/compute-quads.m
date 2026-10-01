#import <Metal/Metal.h>
#include <stdio.h>
/* How compute threads are grouped into quad groups and SIMD groups, and whether quad-group
 * functions see the other threads of the quad: Vulkan's compute derivatives (linear groups) need
 * invocations 4n..4n+3 of a workgroup to form a quad. Each thread records its indices and what
 * quad_broadcast returns for each of the four quad lanes. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(device uint *out [[buffer(0)]], uint3 lid [[thread_position_in_threadgroup]], uint3 gid [[thread_position_in_grid]],\n"
   "              uint li [[thread_index_in_threadgroup]], uint qi [[thread_index_in_quadgroup]], uint si [[thread_index_in_simdgroup]],\n"
   "              uint spt [[simdgroups_per_threadgroup]], uint ts [[threads_per_simdgroup]], uint sgi [[simdgroup_index_in_threadgroup]]) {\n"
   "  uint W = out[0];\n"
   "  uint g = gid.y * W + gid.x;\n"
   "  device uint *o = out + 16 + g * 12;\n"
   "  o[0] = li; o[1] = qi; o[2] = si; o[3] = sgi; o[4] = ts; o[5] = spt;\n"
   "  o[6] = quad_broadcast(li, (ushort)0); o[7] = quad_broadcast(li, (ushort)1);\n"
   "  o[8] = quad_broadcast(li, (ushort)2); o[9] = quad_broadcast(li, (ushort)3);\n"
   "  o[10] = simd_broadcast(li, (ushort)0); o[11] = (uint)(simd_vote::vote_t)simd_active_threads_mask();\n"
   "}\n";
static void run(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLComputePipelineState> ps, unsigned lx, unsigned ly, unsigned groups, bool verbose)
{
   unsigned W = lx * groups, H = ly, n = W * H;
   id<MTLBuffer> out = [dev newBufferWithLength:(16 + n * 12) * 4 options:MTLResourceStorageModeShared];
   uint32_t *o = out.contents; o[0] = W;
   id<MTLCommandBuffer> cb = [q commandBuffer];
   id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
   [ce setComputePipelineState:ps]; [ce setBuffer:out offset:0 atIndex:0];
   [ce dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(lx, ly, 1)];
   [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
   unsigned linear_quads = 0, quad_lane_matches = 0, same_simd = 0, total = 0;
   for (unsigned g = 0; g < n; g++) {
      uint32_t *t = o + 16 + g * 12;
      unsigned li = t[0];
      total++;
      /* does quad lane j hold local index (li & ~3) + j? */
      bool ok = true;
      for (int j = 0; j < 4; j++) ok &= t[6 + j] == (li & ~3u) + j;
      linear_quads += ok;
      quad_lane_matches += t[1] == (li & 3);
      same_simd += t[2] == (li % t[4]);
   }
   printf("local size %ux%u, %u groups: threads/simdgroup %u, simdgroups/threadgroup %u; quad = invocations 4n..4n+3 in lane order for %u/%u threads; quad index == local index & 3 for %u/%u; simd lane == local index %% simd size for %u/%u\n",
          lx, ly, groups, o[16 + 4], o[16 + 5], linear_quads, total, quad_lane_matches, total, same_simd, total);
   if (verbose) {
      for (unsigned g = 0; g < n && g < 40; g++) {
         uint32_t *t = o + 16 + g * 12;
         printf("   grid %3u: local %3u quad lane %u simd lane %2u simd group %u  quad sees [%u %u %u %u]  active mask %08x\n",
                g, t[0], t[1], t[2], t[3], t[6], t[7], t[8], t[9], t[11]);
      }
   }
}
int main(int argc, char **argv)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLCommandQueue> q = [dev newCommandQueue];
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("compile: %s\n", err.localizedDescription.UTF8String); return 1; }
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
      if (!ps) { printf("pipeline: %s\n", err.localizedDescription.UTF8String); return 1; }
      printf("device %s\n", dev.name.UTF8String);
      static const unsigned sizes[][2] = {{4, 1}, {8, 1}, {64, 1}, {8, 8}, {2, 2}, {4, 4}, {16, 16}, {32, 32}, {12, 1}, {6, 2}, {100, 1}, {20, 3}};
      for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
         run(dev, q, ps, sizes[i][0], sizes[i][1], 3, argc > 1 && i < 5);
   }
   return 0;
}
