#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
/* Does atomic_compare_exchange_weak_explicit fail spuriously on Apple GPUs? Every thread increments
 * one shared counter ROUNDS times with a compare-and-swap loop, and counts the calls that returned
 * false although the expected value came back unchanged (a spurious failure: nothing was stored, yet
 * the value read back equals the comparand, which is all Vulkan's OpAtomicCompareExchange returns).
 * A second counter is incremented by threads that trust the returned value alone, as the driver
 * does: it ends short of THREADS * ROUNDS exactly when spurious failures happen. */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "kernel void k(device atomic_uint *c [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
    "  for (uint r = 0; r < ROUNDS; r++) {\n"
    "    for (;;) {\n"
    "      uint old = atomic_load_explicit(&c[0], memory_order_relaxed);\n"
    "      uint expected = old;\n"
    "      bool ok = atomic_compare_exchange_weak_explicit(&c[0], &expected, old + 1, memory_order_relaxed, memory_order_relaxed);\n"
    "      if (!ok && expected == old) atomic_fetch_add_explicit(&c[2], 1, memory_order_relaxed);\n"
    "      if (ok) break;\n"
    "    }\n"
    "    for (;;) {\n"
    "      uint old = atomic_load_explicit(&c[1], memory_order_relaxed);\n"
    "      uint expected = old;\n"
    "      atomic_compare_exchange_weak_explicit(&c[1], &expected, old + 1, memory_order_relaxed, memory_order_relaxed);\n"
    "      if (expected == old) break;\n"
    "    }\n"
    "  }\n"
    "}\n";

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        const unsigned threads = 4096, rounds = getenv("ROUNDS") ? atoi(getenv("ROUNDS")) : 256;
        MTLCompileOptions *opts = [MTLCompileOptions new];
        opts.preprocessorMacros = @{@"ROUNDS" : @(rounds)};
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:opts error:&e];
        if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
        id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e];
        id<MTLBuffer> b = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        for (int run = 0; run < 5; run++) {
            memset(b.contents, 0, 16);
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps];
            [ce setBuffer:b offset:0 atIndex:0];
            [ce dispatchThreads:MTLSizeMake(threads, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [ce endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.error) { printf("error: %s\n", cb.error.localizedDescription.UTF8String); return 1; }
            const uint32_t *c = b.contents;
            printf("run %d: %u increments wanted; checked loop %u, value-only loop %u (%d short), spurious failures seen %u\n", run,
                   threads * rounds, c[0], c[1], (int)(threads * rounds - c[1]), c[2]);
        }
    }
    return 0;
}
