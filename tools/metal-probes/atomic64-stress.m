#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* The driver's locked 64-bit atomics under the load of the Vulkan memory model tests: GROUPS
 * threadgroups of LOCAL threads, each thread exchanging its own 64-bit word and then its partner's
 * (the thread at the mirrored position in the same threadgroup). An exchange takes one of 4096
 * locks picked by the word's index, only in the thread's turn within its SIMD group, with a
 * compare-and-swap that gives up after LIMIT tries, and reads and writes the word between
 * device-scope fences. Counts the threads that gave up, over RUNS dispatches.
 *
 * HOT=1 makes the second exchange hit one word per threadgroup (the first word of the mirrored
 * threadgroup), so every thread of a threadgroup contends for one lock, and with 1024 threads per
 * threadgroup all of those words share four locks: what the tests' read-modify-write variants do.
 * BARRIER=1 puts a threadgroup_barrier before the first exchange, BARRIER=2 between the two,
 * BARRIER=3 both, as the tests' shaders have them.
 *
 * GRID2D=1 lays the threads out as the tests do: an 8x8 grid of 32x32 threadgroups, started with
 * dispatchThreads and identified by their position in the grid alone.
 *
 * METAL4=1 DISPATCHES=n dispatches the kernel n times in one command buffer, with a barrier after each
 * (NOBARRIER=1 leaves the barriers out): what the Vulkan tests do.
 * METAL4=1 RAW=1 reaches every buffer through its GPU address, loaded from a table and cast to a
 * pointer, as the driver's shaders do, instead of through buffer arguments.
 *
 * MODE=0  as the driver does it
 * MODE=1  no fences: the word is read and written as two 32-bit atomics
 * MODE=2  as the driver, with a device-scope fence in the waiting loop as well
 * MODE=3  as the driver, but the lock is released with a compare-and-swap loop instead of exchange */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "#define FENCE atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope::thread_scope_device)\n"
    "static ulong exchange(device atomic_uint *locks, device ulong *words, device atomic_uint *stats,\n"
    "                      uint index, ulong value, uint lane) {\n"
    "  ulong old = 0;\n"
    "  uint slot = index & 4095u;\n"
    "  for (uint j = 0; j < 32u; j++) {\n"
    "    if (j == lane) {\n"
    "      uint spins = 0; bool held = false;\n"
    "      for (;;) {\n"
    "        uint e = 0u;\n"
    "        atomic_compare_exchange_weak_explicit(&locks[slot], &e, 1u, memory_order_relaxed, memory_order_relaxed);\n"
    "        held = e == 0u;\n"
    "        if (held) break;\n"
    "        if (spins >= LIMIT) break;\n"
    "        spins++;\n"
    "#if MODE == 2\n"
    "        FENCE;\n"
    "#endif\n"
    "      }\n"
    "      if (held) {\n"
    "#if MODE == 1\n"
    "        device atomic_uint *halves = (device atomic_uint *)&words[index];\n"
    "        old = (ulong)atomic_load_explicit(&halves[0], memory_order_relaxed) | ((ulong)atomic_load_explicit(&halves[1], memory_order_relaxed) << 32);\n"
    "        atomic_store_explicit(&halves[0], (uint)value, memory_order_relaxed);\n"
    "        atomic_store_explicit(&halves[1], (uint)(value >> 32), memory_order_relaxed);\n"
    "#else\n"
    "        FENCE; old = words[index]; FENCE; words[index] = value; FENCE;\n"
    "#endif\n"
    "#if MODE == 3\n"
    "        for (;;) { uint e = 1u; if (atomic_compare_exchange_weak_explicit(&locks[slot], &e, 0u, memory_order_relaxed, memory_order_relaxed)) break; }\n"
    "#else\n"
    "        atomic_exchange_explicit(&locks[slot], 0u, memory_order_relaxed);\n"
    "#endif\n"
    "        atomic_fetch_max_explicit(&stats[1], spins, memory_order_relaxed);\n"
    "      } else {\n"
    "        atomic_fetch_add_explicit(&stats[0], 1u, memory_order_relaxed);\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  return old;\n"
    "}\n"
    "#if RAW\n"
    "kernel void k(constant ulong *root [[buffer(0)]],\n"
    "#else\n"
    "kernel void k(device atomic_uint *locks [[buffer(0)]], device ulong *words [[buffer(1)]],\n"
    "              device atomic_uint *stats [[buffer(2)]], device ulong *out [[buffer(3)]],\n"
    "#endif\n"
    "#if GRID2D\n"
    "              uint3 gid [[thread_position_in_grid]],\n"
    "#else\n"
    "              uint li [[thread_index_in_threadgroup]], uint3 tg [[threadgroup_position_in_grid]],\n"
    "#endif\n"
    "              uint lane [[thread_index_in_simdgroup]]) {\n"
    "#if RAW\n"
    "  device atomic_uint *locks = (device atomic_uint *)root[0];\n"
    "  device ulong *words = (device ulong *)root[1];\n"
    "  device atomic_uint *stats = (device atomic_uint *)root[2];\n"
    "  device ulong *out = (device ulong *)root[3];\n"
    "#endif\n"
    "#if GRID2D\n"
    "  uint3 tg = uint3((gid.y / 32u) * 8u + gid.x / 32u, 0u, 0u);\n"
    "  uint li = (gid.y % 32u) * 32u + gid.x % 32u;\n"
    "#endif\n"
    "#if HOT\n"
    "  uint self = tg.x * LOCAL + li, partner = (GROUPS - 1u - tg.x) * LOCAL;\n"
    "#else\n"
    "  uint self = tg.x * LOCAL + li, partner = tg.x * LOCAL + (LOCAL - 1u - li);\n"
    "#endif\n"
    "#if BARRIER & 1\n"
    "  threadgroup_barrier(mem_flags::mem_none);\n"
    "#endif\n"
    "  exchange(locks, words, stats, self, 1ul, lane);\n"
    "#if BARRIER & 2\n"
    "  threadgroup_barrier(mem_flags::mem_none);\n"
    "#endif\n"
    "  out[self] = exchange(locks, words, stats, partner, 2ul, lane);\n"
    "  atomic_fetch_add_explicit(&stats[2], 1u, memory_order_relaxed);\n"
    "}\n";

static int env(const char *name, int def) { return getenv(name) ? atoi(getenv(name)) : def; }

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        const unsigned local = env("LOCAL", 1024), groups = env("GROUPS", 64), runs = env("RUNS", 200);
        const unsigned limit = env("LIMIT", 1 << 22), mode = env("MODE", 0), dispatches = env("DISPATCHES", 1);
        const unsigned n = local * groups * dispatches;
        MTLCompileOptions *opts = [MTLCompileOptions new];
        opts.preprocessorMacros = @{@"LOCAL" : @(local), @"LIMIT" : @(limit), @"MODE" : @(mode), @"BARRIER" : @(env("BARRIER", 0)), @"HOT" : @(env("HOT", 0)), @"GROUPS" : @(groups), @"GRID2D" : @(env("GRID2D", 0)), @"RAW" : @(env("RAW", 0))};
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:opts error:&e];
        if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
        id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e];
        if (!ps) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
        id<MTLBuffer> locks = [dev newBufferWithLength:4096 * 4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> words = [dev newBufferWithLength:n * 8 options:MTLResourceStorageModeShared];
        id<MTLBuffer> stats = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
        id<MTLBuffer> out = [dev newBufferWithLength:n * 8 options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        /* METAL4=1: the pipeline from MTL4Compiler with maxTotalThreadsPerThreadgroup set, run on a
         * Metal 4 queue through an argument table, as the driver does. */
        const bool metal4 = env("METAL4", 0);
        id<MTL4CommandQueue> q4 = nil;
        id<MTL4CommandAllocator> al = nil;
        id<MTL4ArgumentTable> table = nil;
        id<MTLSharedEvent> ev = nil;
        if (metal4) {
            id<MTL4Compiler> comp = [dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&e];
            MTL4LibraryDescriptor *ld = [MTL4LibraryDescriptor new];
            ld.source = @(src);
            ld.options = opts;
            id<MTLLibrary> lib4 = [comp newLibraryWithDescriptor:ld error:&e];
            if (!lib4) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
            MTL4LibraryFunctionDescriptor *fd = [MTL4LibraryFunctionDescriptor new];
            fd.name = @"k"; fd.library = lib4;
            MTL4ComputePipelineDescriptor *pd = [MTL4ComputePipelineDescriptor new];
            pd.computeFunctionDescriptor = fd;
            pd.maxTotalThreadsPerThreadgroup = local;
            ps = [comp newComputePipelineStateWithDescriptor:pd compilerTaskOptions:nil error:&e];
            if (!ps) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
            q4 = [dev newMTL4CommandQueue];
            id<MTLResidencySet> rs = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&e];
            [rs addAllocation:locks]; [rs addAllocation:words]; [rs addAllocation:stats]; [rs addAllocation:out];
            [rs commit];
            [q4 addResidencySet:rs];
            al = [dev newCommandAllocator];
            MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new];
            ad.maxBufferBindCount = 4;
            table = [dev newArgumentTableWithDescriptor:ad error:&e];
            if (env("RAW", 0)) {
                id<MTLBuffer> root = [dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
                uint64_t *a = root.contents;
                a[0] = locks.gpuAddress; a[1] = words.gpuAddress; a[2] = stats.gpuAddress; a[3] = out.gpuAddress;
                [rs addAllocation:root]; [rs commit];
                [table setAddress:root.gpuAddress atIndex:0];
            } else {
                [table setAddress:locks.gpuAddress atIndex:0];
                [table setAddress:words.gpuAddress atIndex:1];
                [table setAddress:stats.gpuAddress atIndex:2];
                [table setAddress:out.gpuAddress atIndex:3];
            }
            ev = [dev newSharedEvent];
        }
        unsigned bad_runs = 0, gave_up = 0, longest = 0, left_held = 0, errors = 0, unfinished = 0;
        double slowest = 0, longest_wall = 0;
        for (unsigned r = 0; r < runs; r++) {
            memset(locks.contents, 0, 4096 * 4); memset(words.contents, 0, n * 8); memset(stats.contents, 0, 16);
            if (metal4) {
                [al reset];
                id<MTL4CommandBuffer> cb4 = [dev newCommandBuffer];
                [cb4 beginCommandBufferWithAllocator:al];
                id<MTL4ComputeCommandEncoder> ce4 = [cb4 computeCommandEncoder];
                [ce4 setArgumentTable:table];
                for (unsigned k = 0; k < dispatches; k++) {
                    [ce4 setComputePipelineState:ps];
                    if (env("GRID2D", 0))
                        [ce4 dispatchThreads:MTLSizeMake(256, 256, 1) threadsPerThreadgroup:MTLSizeMake(32, 32, 1)];
                    else
                        [ce4 dispatchThreads:MTLSizeMake(groups * local, 1, 1) threadsPerThreadgroup:MTLSizeMake(local, 1, 1)];
                    if (env("NOBARRIER", 0) == 0)
                        [ce4 barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch
                                     visibilityOptions:MTL4VisibilityOptionResourceAlias];
                }
                [ce4 endEncoding];
                [cb4 endCommandBuffer];
                MTL4CommitOptions *co = [MTL4CommitOptions new];
                __block NSString *failure = nil;
                __block double gpu_time = 0;
                [co addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
                    gpu_time = feedback.GPUEndTime - feedback.GPUStartTime;
                    if (feedback.error) failure = [NSString stringWithFormat:@"%@ (code %ld)", feedback.error.localizedDescription, (long)feedback.error.code];
                }];
                CFAbsoluteTime began = CFAbsoluteTimeGetCurrent();
                [q4 commit:&cb4 count:1 options:co];
                [q4 signalEvent:ev value:r + 1];
                if (![ev waitUntilSignaledValue:r + 1 timeoutMS:120000]) { printf("dispatch %u did not finish in two minutes\n", r); return 1; }
                double wall = CFAbsoluteTimeGetCurrent() - began;
                if (wall > longest_wall) longest_wall = wall;
                const uint32_t *s = stats.contents, *l = locks.contents;
                if (s[2] != n) {
                    uint32_t at_signal = s[2], held_now = 0;
                    for (unsigned i = 0; i < 4096; i++) held_now += l[i] != 0;
                    unsigned waited = 0;
                    while (s[2] != n && waited < 5000) { usleep(1000); waited++; }
                    if (unfinished++ < 4)
                        printf("  dispatch %u: %u of %u threads done when the queue signalled (%u locks held); %u done after %u ms more; gave up so far %u; %.3f s on the GPU, %.3f s wall; error: %s\n",
                               r, at_signal, n, held_now, s[2], waited, s[0], gpu_time, wall, failure ? failure.UTF8String : "none reported");
                }
                if (failure) errors++;
                if (gpu_time > slowest) slowest = gpu_time;
                if (s[0]) { bad_runs++; gave_up += s[0]; }
                if (s[1] > longest) longest = s[1];
                for (unsigned i = 0; i < 4096; i++) left_held += l[i] != 0;
                continue;
            }
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps];
            [ce setBuffer:locks offset:0 atIndex:0];
            [ce setBuffer:words offset:0 atIndex:1];
            [ce setBuffer:stats offset:0 atIndex:2];
            [ce setBuffer:out offset:0 atIndex:3];
            if (env("GRID2D", 0))
                [ce dispatchThreads:MTLSizeMake(256, 256, 1) threadsPerThreadgroup:MTLSizeMake(32, 32, 1)];
            else
                [ce dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(local, 1, 1)];
            [ce endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            double took = cb.GPUEndTime - cb.GPUStartTime;
            if (took > slowest) slowest = took;
            if (cb.error) {
                if (!errors++) printf("first error, dispatch %u after %.2f s on the GPU: %s\n", r, took, cb.error.localizedDescription.UTF8String);
            }
            const uint32_t *s = stats.contents, *l = locks.contents;
            if (s[2] != n) unfinished++;
            if (s[0]) { bad_runs++; gave_up += s[0]; }
            if (s[1] > longest) longest = s[1];
            for (unsigned i = 0; i < 4096; i++) left_held += l[i] != 0;
        }
        printf("%s%smode %u, barrier %d, hot %d, %u x %u threads, limit %u: %u of %u dispatches had threads give up (%u threads); longest successful wait %u tries; locks left held %u; %u dispatches read before every thread had finished; %u dispatches ended in an error; slowest dispatch %.3f s on the GPU, %.3f s wall\n",
               metal4 ? "Metal 4, " : "", env("RAW", 0) ? "raw addresses, " : "", mode, env("BARRIER", 0), env("HOT", 0), groups, local, limit, bad_runs, runs, gave_up, longest, left_held, unfinished, errors, slowest, longest_wall);
    }
    return 0;
}
