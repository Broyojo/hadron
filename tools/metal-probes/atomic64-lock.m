#import <Metal/Metal.h>
#include <stdio.h>
/* What it takes to make a 64-bit read-modify-write atomic on the GPU with a lock built from a
 * 32-bit atomic. Every thread adds 0x100000001 to one shared 64-bit value, in a buffer or in an
 * RG32Uint texel; the result is exact only when both words equal the thread count.
 *
 *  - A plain spin lock deadlocks: the threads of a SIMD group run in lockstep, so the thread that
 *    takes the lock is held until the others leave the loop, and they wait for the lock. Loops
 *    here are bounded, so that shows as threads giving up.
 *  - Taking turns by SIMD lane (only the lane whose turn it is spins) avoids that.
 *  - Plain loads and stores are not coherent across threads even under the lock; they are with an
 *    atomic_thread_fence at device scope on both sides, or as 32-bit atomics on the two halves.
 *  - A texture read does not see a write() from the same kernel without that fence.
 *  - 32-bit texture atomics on RG32Uint touch the first channel only: there is no 64-bit
 *    compare-and-swap to build on. */
#define HDR "#include <metal_stdlib>\n using namespace metal;\n"
#define ARGS "(device atomic_uint *c [[buffer(0)]], device ulong *data [[buffer(1)]],\n" \
   " texture2d<ulong, access::read_write> t [[texture(0)]], texture2d<uint, access::read_write> t32 [[texture(1)]],\n" \
   " uint tid [[thread_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]], uint lanes [[threads_per_simdgroup]]) {\n"
#define FENCE "atomic_thread_fence(mem_flags::mem_texture | mem_flags::mem_device, memory_order_seq_cst, thread_scope::thread_scope_device);\n"
#define CAS "atomic_compare_exchange_weak_explicit(&c[1], &e, 1u, memory_order_relaxed, memory_order_relaxed)"
#define UNLOCK "atomic_store_explicit(&c[1], 0u, memory_order_relaxed);\n"
#define GAVE_UP "atomic_fetch_add_explicit(&c[0], 1u, memory_order_relaxed);\n"
#define TURNS(body) \
    "  for (uint j = 0; j < lanes; j++) {\n" \
    "    if (j == lane) {\n" \
    "      bool held = false;\n" \
    "      for (uint i = 0; i < 2000000 && !held; i++) { uint e = 0; held = " CAS "; }\n" \
    "      if (held) {\n" body UNLOCK "      } else " GAVE_UP \
    "    }\n" \
    "  }\n}\n"
#define HALVES \
    "        uint lo = atomic_load_explicit(&c[4], memory_order_relaxed), hi = atomic_load_explicit(&c[5], memory_order_relaxed);\n" \
    "        ulong nv = (((ulong)hi << 32) | lo) + 0x100000001ul;\n" \
    "        atomic_store_explicit(&c[4], (uint)nv, memory_order_relaxed); atomic_store_explicit(&c[5], (uint)(nv >> 32), memory_order_relaxed);\n"
enum { IN_BUFFER, IN_HALVES, IN_TEXTURE, IN_TEXTURE32 };
static const struct { const char *name, *src; int where; } tests[] = {
   {"buffer, plain spin lock", HDR "kernel void k" ARGS
    "  for (uint i = 0; i < 100000; i++) {\n"
    "    uint e = 0;\n"
    "    if (" CAS ") {\n" HALVES UNLOCK "      return;\n    }\n  }\n  " GAVE_UP "}\n", IN_HALVES},
   {"buffer, lane turns, plain load and store", HDR "kernel void k" ARGS TURNS(
    "        ulong v = data[0]; data[0] = v + 0x100000001ul;\n"), IN_BUFFER},
   {"buffer, lane turns, fenced load and store", HDR "kernel void k" ARGS TURNS(
    "        " FENCE "        ulong v = data[0]; data[0] = v + 0x100000001ul;\n        " FENCE), IN_BUFFER},
   {"buffer, lane turns, 32-bit atomic halves", HDR "kernel void k" ARGS TURNS(HALVES), IN_HALVES},
   {"buffer, lock state machine, atomic halves", HDR "kernel void k" ARGS
    "  uint state = 0;\n"
    "  for (uint i = 0; i < 2000000 && state != 2; i++) {\n"
    "    if (state == 1) {\n" HALVES
    "      state = 1 + atomic_exchange_explicit(&c[1], 0u, memory_order_relaxed); }\n"
    "    else { uint e = 0; if (" CAS ") state = 1; }\n"
    "  }\n"
    "  if (state != 2) " GAVE_UP "}\n", IN_HALVES},
   {"texture, lane turns, plain read and write", HDR "kernel void k" ARGS TURNS(
    "        ulong v = t.read(uint2(0)).x; t.write(ulong4(v + 0x100000001ul), uint2(0));\n"), IN_TEXTURE},
   {"texture, lane turns, fenced read and write", HDR "kernel void k" ARGS TURNS(
    "        " FENCE "        ulong v = t.read(uint2(0)).x; t.write(ulong4(v + 0x100000001ul), uint2(0));\n        " FENCE), IN_TEXTURE},
   {"texture, 32-bit atomic add of (1, 1)", HDR "kernel void k" ARGS
    "  t32.atomic_fetch_add(uint2(0), uint4(1, 1, 0, 0));\n}\n", IN_TEXTURE32},
};
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLCommandQueue> q = [dev newCommandQueue];
      printf("device %s\n", dev.name.UTF8String);
      static const unsigned shapes[][2] = {{32, 1}, {32, 8}, {64, 64}};
      for (unsigned t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
         NSError *err = nil;
         id<MTLLibrary> lib = [dev newLibraryWithSource:@(tests[t].src) options:nil error:&err];
         if (!lib) { printf("%s: %s\n", tests[t].name, err.localizedDescription.UTF8String); continue; }
         id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
         for (unsigned s = 0; s < 3; s++) {
            unsigned per = shapes[s][0], groups = shapes[s][1];
            id<MTLBuffer> c = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
            id<MTLBuffer> data = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG32Uint width:4 height:4 mipmapped:NO];
            td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageShaderAtomic;
            td.storageMode = MTLStorageModeShared;
            id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
            uint32_t zero[32] = {0}, out[32] = {0};
            [tex replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:zero bytesPerRow:32];
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps];
            [ce setBuffer:c offset:0 atIndex:0]; [ce setBuffer:data offset:0 atIndex:1];
            [ce setTexture:tex atIndex:0]; [ce setTexture:tex atIndex:1];
            [ce dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(per, 1, 1)];
            [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
            const uint32_t *cc = c.contents, *w;
            switch (tests[t].where) {
            case IN_BUFFER: w = data.contents; break;
            case IN_HALVES: w = cc + 4; break;
            default:
               [tex getBytes:out bytesPerRow:32 fromRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0];
               w = out;
               break;
            }
            unsigned threads = per * groups;
            printf("%-44s %5u threads: words %5u %5u, %5u gave up: %s%s\n", tests[t].name, threads, w[0], w[1], cc[0],
                   w[0] == threads && w[1] == threads ? "exact" : "not exact", cb.error ? " (command buffer error)" : "");
         }
      }
   }
   return 0;
}
