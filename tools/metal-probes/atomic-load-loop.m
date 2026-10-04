#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A thread that waits in a loop for another thread's store, reading with a relaxed atomic load,
 * may never see the store when the other thread is in a different threadgroup.
 *
 * Every thread stores started[self] = 1 and later flag[self] = 1 (atomic stores to device memory).
 * "Waiter" threads spin until their partner has started and then until the partner's flag is set,
 * giving up each wait after LIMIT spins; partners never wait. By default the partner is DIST threads
 * further in the same threadgroup (waiters are every other run of DIST threads); with CROSS=1 it is
 * the same thread of the next threadgroup (waiters are the even threadgroups). MODE picks how the
 * waiter reads the flag:
 *
 *   MODE=0  atomic_load_explicit, relaxed             CROSS=1: most waiters never see the flag
 *   MODE=1  atomic_fetch_or_explicit with 0           CROSS=1: the same
 *   MODE=2  a device-scope atomic_thread_fence, then the load      every waiter sees it
 *   MODE=3  a compare-and-swap that changes nothing                every waiter sees it
 *
 * Inside one threadgroup (CROSS=0) every mode sees the flag, at every distance. The partner did run:
 * its "started" store was seen. So this is about what a relaxed load observes, not about threads
 * blocking each other; whether the compiler hoists the load out of the loop or the hardware serves
 * it from a stale copy is not known. A lock must not wait with a relaxed load.
 *
 * Environment: LOCAL=threads per threadgroup (default 64), GROUPS=threadgroups (4), DIST (32), CROSS,
 * MODE, LIMIT. */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "kernel void k(device atomic_uint *started [[buffer(0)]], device atomic_uint *flag [[buffer(1)]],\n"
    "              device uint *out [[buffer(2)]],\n"
    "              uint li [[thread_index_in_threadgroup]], uint3 tg [[threadgroup_position_in_grid]],\n"
    "              uint lane [[thread_index_in_simdgroup]], uint sg [[simdgroup_index_in_threadgroup]],\n"
    "              uint width [[threads_per_simdgroup]]) {\n"
    "  uint self = tg.x * LOCAL + li;\n"
    "  uint partner = CROSS ? ((tg.x + 1u) % GROUPS) * LOCAL + li : tg.x * LOCAL + (li + DIST) % LOCAL;\n"
    "  bool waiter = CROSS ? (tg.x & 1u) == 0u : ((li / DIST) & 1u) == 0u;\n"
    "  atomic_store_explicit(&started[self], 1u, memory_order_relaxed);\n"
    "  uint s1 = 0, s2 = 0;\n"
    "  if (waiter) {\n"
    "    while (atomic_load_explicit(&started[partner], memory_order_relaxed) == 0u && s1 < LIMIT) s1++;\n"
    "#if MODE == 0\n"
    "    while (atomic_load_explicit(&flag[partner], memory_order_relaxed) == 0u && s2 < LIMIT) s2++;\n"
    "#elif MODE == 1\n"
    "    while (atomic_fetch_or_explicit(&flag[partner], 0u, memory_order_relaxed) == 0u && s2 < LIMIT) s2++;\n"
    "#elif MODE == 2\n"
    "    while (s2 < LIMIT) { atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope::thread_scope_device); if (atomic_load_explicit(&flag[partner], memory_order_relaxed) != 0u) break; s2++; }\n"
    "#else\n"
    "    while (s2 < LIMIT) { uint e = 1u; if (atomic_compare_exchange_weak_explicit(&flag[partner], &e, 1u, memory_order_relaxed, memory_order_relaxed) || e == 1u) break; s2++; }\n"
    "#endif\n"
    "  }\n"
    "  atomic_store_explicit(&flag[self], 1u, memory_order_relaxed);\n"
    "  out[self * 4 + 0] = s1; out[self * 4 + 1] = s2; out[self * 4 + 2] = lane | (sg << 8) | (width << 16);\n"
    "  out[self * 4 + 3] = waiter ? 1u : 2u;\n"
    "}\n";

static int env(const char *name, int def) { return getenv(name) ? atoi(getenv(name)) : def; }

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        const unsigned local = env("LOCAL", 64), groups = env("GROUPS", 4), dist = env("DIST", 32);
        const unsigned limit = env("LIMIT", 1 << 21), n = local * groups;
        MTLCompileOptions *opts = [MTLCompileOptions new];
        const unsigned cross = env("CROSS", 0);
        opts.preprocessorMacros = @{@"LOCAL" : @(local), @"DIST" : @(dist), @"LIMIT" : @(limit), @"CROSS" : @(cross), @"GROUPS" : @(groups), @"MODE" : @(env("MODE", 0))};
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:opts error:&e];
        if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
        id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e];
        if (!ps) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
        id<MTLBuffer> started = [dev newBufferWithLength:n * 4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> flag = [dev newBufferWithLength:n * 4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> out = [dev newBufferWithLength:n * 16 options:MTLResourceStorageModeShared];
        memset(started.contents, 0, n * 4); memset(flag.contents, 0, n * 4); memset(out.contents, 0, n * 16);
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
        [ce setComputePipelineState:ps];
        [ce setBuffer:started offset:0 atIndex:0];
        [ce setBuffer:flag offset:0 atIndex:1];
        [ce setBuffer:out offset:0 atIndex:2];
        [ce dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(local, 1, 1)];
        [ce endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.error) { printf("error: %s\n", cb.error.localizedDescription.UTF8String); return 1; }

        const uint32_t *o = out.contents;
        unsigned waiters = 0, never_started = 0, independent = 0, blocked = 0, unwritten = 0, bad_lane = 0;
        unsigned width = (o[2] >> 16) & 0xffff;
        for (unsigned i = 0; i < n; i++) {
            if (!o[i * 4 + 3]) { unwritten++; continue; }
            if ((o[i * 4 + 2] & 0xff) != (i % local) % width) bad_lane++;
            if (o[i * 4 + 3] != 1) continue;
            waiters++;
            if (o[i * 4] >= limit) never_started++;
            else if (o[i * 4 + 1] >= limit) blocked++;
            else independent++;
        }
        printf("mode %d, local %u, groups %u, partner %s%u, SIMD width %u (pipeline: width %lu, max %lu)\n", env("MODE", 0), local, groups, cross ? "in the next threadgroup, " : "distance ", cross ? 0 : dist, width,
               (unsigned long)ps.threadExecutionWidth, (unsigned long)ps.maxTotalThreadsPerThreadgroup);
        printf("  waiters %u: saw the partner's flag %u, never saw it %u, never saw the partner start %u\n",
               waiters, independent, blocked, never_started);
        if (unwritten || bad_lane)
            printf("  %u threads wrote nothing, %u have a lane index that is not their thread index modulo the width\n", unwritten, bad_lane);
    }
    return 0;
}
