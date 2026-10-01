#import <Metal/Metal.h>
#include <stdio.h>
#include <mach/mach_time.h>

/* Follow-up to store-guard.m: with the guard hoisted out of the loop (two copies of the body
 * chosen by one check), a store loop and a load+store loop cost the same as unguarded ones. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "struct Root { ulong desc; ulong arena; ulong sink; };\n"
   "#define PRE uint4 d = uint4(*(device packed_uint4*)root.desc); ulong base = ulong(d.x) | (ulong(d.y) << 32); uint b = tid * 16u;\n"
   "kernel void plain(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  for (uint i = 0; i < 16u; i++) { *(device uint*)(base + ulong((b + i) << 2)) = b + i * 7u; } }\n"
   "kernel void branch(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  bool sparse = d.w != 0u;\n"
   "  for (uint i = 0; i < 16u; i++) { uint off = (b + i) << 2;\n"
   "    if (sparse) { uint page = ((d.x & 65535u) + off) >> 16; uchar r = *(device uchar*)(root.arena + ulong(d.w - 1u + page));\n"
   "      if (r != 0) *(device uint*)(base + ulong(off)) = b + i * 7u; }\n"
   "    else *(device uint*)(base + ulong(off)) = b + i * 7u; } }\n"
   "kernel void branchless(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  bool sparse = d.w != 0u;\n"
   "  for (uint i = 0; i < 16u; i++) { uint off = (b + i) << 2;\n"
   "    uint page = ((d.x & 65535u) + off) >> 16; uint idx = sparse ? d.w - 1u + page : 0u;\n"
   "    uchar r = *(device uchar*)(root.arena + ulong(idx));\n"
   "    ulong addr = r != 0 ? base + ulong(off) : root.sink;\n"
   "    *(device uint*)addr = b + i * 7u; } }\n"
   "kernel void sinksel(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  bool sparse = d.w != 0u;\n"
   "  for (uint i = 0; i < 16u; i++) { uint off = (b + i) << 2; bool ok = true;\n"
   "    if (sparse) { uint page = ((d.x & 65535u) + off) >> 16; ok = *(device uchar*)(root.arena + ulong(d.w - 1u + page)) != 0; }\n"
   "    ulong addr = ok ? base + ulong(off) : root.sink;\n"
   "    *(device uint*)addr = b + i * 7u; } }\n"
   "kernel void hoisted(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  if (d.w != 0u) { for (uint i = 0; i < 16u; i++) { uint off = (b + i) << 2; uint page = ((d.x & 65535u) + off) >> 16;\n"
   "      if (*(device uchar*)(root.arena + ulong(d.w - 1u + page)) != 0) *(device uint*)(base + ulong(off)) = b + i * 7u; } }\n"
   "  else { for (uint i = 0; i < 16u; i++) { *(device uint*)(base + ulong((b + i) << 2)) = b + i * 7u; } } }\n"
   "kernel void ls_plain(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  uint acc = 0u; for (uint i = 0; i < 16u; i++) { device uint *p = (device uint*)(base + ulong((b + i) << 2)); acc += *p; *p = acc; } }\n"
   "kernel void ls_hoisted(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  if (d.w != 0u) { uint acc = 0u; for (uint i = 0; i < 16u; i++) { uint off = (b + i) << 2; uint page = ((d.x & 65535u) + off) >> 16; device uint *p = (device uint*)(base + ulong(off)); acc += *p;\n"
   "      if (*(device uchar*)(root.arena + ulong(d.w - 1u + page)) != 0) *p = acc; } }\n"
   "  else { uint acc = 0u; for (uint i = 0; i < 16u; i++) { device uint *p = (device uint*)(base + ulong((b + i) << 2)); acc += *p; *p = acc; } } }\n"
   "kernel void ls_flag(constant Root &root [[buffer(0)]], uint tid [[thread_position_in_grid]]) { PRE\n"
   "  uint live = *(constant uint*)root.arena;\n"
   "  if (live != 0u) { uint acc = 0u; for (uint i = 0; i < 16u; i++) { uint off = (b + i) << 2; device uint *p = (device uint*)(base + ulong(off)); acc += *p;\n"
   "      if (d.w != 0u) { uint page = ((d.x & 65535u) + off) >> 16; if (*(device uchar*)(root.arena + ulong(d.w - 1u + page)) != 0) *p = acc; } else *p = acc; } }\n"
   "  else { uint acc = 0u; for (uint i = 0; i < 16u; i++) { device uint *p = (device uint*)(base + ulong((b + i) << 2)); acc += *p; *p = acc; } } }\n";



static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.description.UTF8String); return 1; }
      const NSUInteger threads = 1 << 22;
      id<MTLBuffer> data = [dev newBufferWithLength:threads * 64 options:MTLResourceStorageModePrivate];
      id<MTLBuffer> arena = [dev newBufferWithLength:65536 options:MTLResourceStorageModeShared];
      ((uint8_t *)arena.contents)[0] = 0;
      id<MTLBuffer> desc = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
      uint32_t *d = desc.contents;
      d[0] = (uint32_t)data.gpuAddress; d[1] = (uint32_t)(data.gpuAddress >> 32); d[2] = (uint32_t)data.length; d[3] = 0;
      id<MTLBuffer> root = [dev newBufferWithLength:24 options:MTLResourceStorageModeShared];
      uint64_t *r = root.contents;
      r[0] = desc.gpuAddress; r[1] = arena.gpuAddress; r[2] = arena.gpuAddress + 4096;
      id<MTLCommandQueue> q = [dev newCommandQueue];
      mach_timebase_info_data_t tb; mach_timebase_info(&tb);
      const char *names[] = {"plain", "hoisted", "ls_plain", "ls_hoisted", "ls_flag"};
      for (int rep = 0; rep < 3; rep++)
      for (int k = 0; k < 5; k++) {
         id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@(names[k])] error:&err];
         double t[21];
         for (int run = 0; run < 24; run++) {
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps]; [ce setBuffer:root offset:0 atIndex:0];
            [ce useResource:data usage:MTLResourceUsageWrite]; [ce useResource:arena usage:MTLResourceUsageRead | MTLResourceUsageWrite];
            [ce useResource:desc usage:MTLResourceUsageRead];
            [ce dispatchThreads:MTLSizeMake(threads, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [ce endEncoding];
            uint64_t t0 = mach_absolute_time();
            [cb commit]; [cb waitUntilCompleted];
            if (run >= 3) t[run - 3] = (double)(mach_absolute_time() - t0) * tb.numer / tb.denom / 1e6;
         }
         qsort(t, 21, sizeof(double), cmp);
         printf("%-11s %.3f ms\n", names[k], t[10]);
      }
   }
   return 0;
}
