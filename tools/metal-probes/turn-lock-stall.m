#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A spin lock in which the lanes of a SIMD group take turns stalls when its kernel is dispatched
 * several times in one Metal 4 command buffer with a barrier after each dispatch.
 *
 * The kernel is turn-lock-stall.metal: what the Vulkan driver emitted for the memory model test
 * dEQP-VK.memory_model.message_passing.ext.u64.coherent.atomic_atomic.atomicrmw.device.
 * payload_nonlocal.workgroup.guard_nonlocal.workgroup.comp when its 64-bit atomics took a lock from
 * a table of 4096 with a compare-and-swap, one lane of the SIMD group at a time ("for turn in 0..31:
 * if (turn == lane) { spin for the lock; exchange; unlock }"), giving up after 4,194,304 tries. This
 * program lays out the root table the kernel reads (lock table, result buffer, flags) by hand and
 * dispatches it as the test does: an 8x8 grid of 31x31 threadgroups, DISPATCHES times per command
 * buffer (default 8), with BARRIERS barriers after each (default 2).
 *
 *   turn-lock-stall turn-lock-stall.metal                 seconds per run, threads give up
 *   DISPATCHES=1, or BARRIERS=0                            milliseconds, nobody gives up (with
 *                                                          DISPATCHES=2 it happens now and then)
 *
 * Small changes to the kernel that do not change what it does (a different value stored in the lock
 * each turn, an extra statement in the branch taken on giving up) also make the stall go away, and
 * a kernel in which every lane spins at once (no turns) shows the same stall with a single
 * dispatch. No lock is left held afterwards. The driver now uses a lock that does not depend on
 * turns. Why the turns fail under these conditions is not known. */
static int env(const char *name, int def) { return getenv(name) ? atoi(getenv(name)) : def; }

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 2) { printf("usage: turn-lock-stall turn-lock-stall.metal\n"); return 1; }
        NSError *e = nil;
        NSString *src = [NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:&e];
        if (!src) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        const unsigned dim = env("DIM", 31), groups = env("GROUPS", 8), runs = env("RUNS", 3);

        id<MTL4Compiler> comp = [dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&e];
        MTL4LibraryDescriptor *ld = [MTL4LibraryDescriptor new];
        ld.source = src;
        MTLCompileOptions *opts = [MTLCompileOptions new];
        if (env("NOOPT", 0)) opts.optimizationLevel = MTLLibraryOptimizationLevelSize;
        if (env("SAFE", 0)) opts.mathMode = MTLMathModeSafe;
        if (env("DRIVER", 1)) {
            opts.languageVersion = (MTLLanguageVersion)((4 << 16) + env("MINOR", 1));
            opts.mathMode = MTLMathModeFast;
            opts.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsFast;
        }
        ld.options = opts;
        id<MTLLibrary> lib = [comp newLibraryWithDescriptor:ld error:&e];
        if (!lib) { printf("compile: %s\n", e.localizedDescription.UTF8String); return 1; }
        MTL4LibraryFunctionDescriptor *fd = [MTL4LibraryFunctionDescriptor new];
        fd.name = @"main_entrypoint"; fd.library = lib;
        MTL4ComputePipelineDescriptor *pd = [MTL4ComputePipelineDescriptor new];
        pd.computeFunctionDescriptor = fd;
        pd.maxTotalThreadsPerThreadgroup = dim * dim;
        id<MTLComputePipelineState> ps = [comp newComputePipelineStateWithDescriptor:pd compilerTaskOptions:nil error:&e];
        if (!ps) { printf("pipeline: %s\n", e.localizedDescription.UTF8String); return 1; }

        const NSUInteger fail_size = (NSUInteger)dim * dim * groups * groups * 4;
        id<MTLBuffer> root = [dev newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        id<MTLBuffer> desc = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
        id<MTLBuffer> zero = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
        id<MTLBuffer> arena = [dev newBufferWithLength:64 + 8 * 4096 options:MTLResourceStorageModeShared];
        id<MTLBuffer> fail = [dev newBufferWithLength:fail_size options:MTLResourceStorageModeShared];
        id<MTLBuffer> samplers = [dev newBufferWithLength:4096 * 8 options:MTLResourceStorageModeShared];
        memset(root.contents, 0, 4096); memset(desc.contents, 0, 256); memset(zero.contents, 0, 256);
        *(uint32_t *)zero.contents = env("FLAG", 0);
        uint64_t *r = root.contents;
        r[928 / 8] = desc.gpuAddress;
        r[2240 / 8] = zero.gpuAddress;
        r[2248 / 8] = arena.gpuAddress;
        uint32_t *d = (uint32_t *)((uint8_t *)desc.contents + 32);
        d[0] = (uint32_t)fail.gpuAddress; d[1] = (uint32_t)(fail.gpuAddress >> 32); d[2] = 0; d[3] = 0;

        id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
        id<MTLResidencySet> rs = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&e];
        for (id<MTLBuffer> b in @[root, desc, zero, arena, fail, samplers]) [rs addAllocation:b];
        [rs commit];
        [q addResidencySet:rs];
        id<MTL4CommandAllocator> al = [dev newCommandAllocator];
        MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new];
        ad.maxBufferBindCount = 2;
        id<MTL4ArgumentTable> table = [dev newArgumentTableWithDescriptor:ad error:&e];
        [table setAddress:root.gpuAddress atIndex:0];
        [table setAddress:samplers.gpuAddress atIndex:1];
        id<MTLSharedEvent> ev = [dev newSharedEvent];

        for (unsigned run = 0; run < runs; run++) {
            memset(arena.contents, 0, arena.length); memset(fail.contents, 0, fail_size);
            [al reset];
            id<MTL4CommandBuffer> cb = [dev newCommandBuffer];
            [cb beginCommandBufferWithAllocator:al];
            id<MTL4ComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setArgumentTable:table];
            for (int n = 0; n < env("DISPATCHES", 8); n++) {
                [ce setComputePipelineState:ps];
                [ce dispatchThreads:MTLSizeMake(dim * groups, dim * groups, 1) threadsPerThreadgroup:MTLSizeMake(dim, dim, 1)];
                for (int k = 0; k < env("BARRIERS", 2); k++)
                    [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:env("VISNONE", 0) ? MTL4VisibilityOptionNone : MTL4VisibilityOptionResourceAlias];
            }
            [ce endEncoding];
            [cb endCommandBuffer];
            MTL4CommitOptions *co = [MTL4CommitOptions new];
            __block NSString *failure = nil;
            [co addFeedbackHandler:^(id<MTL4CommitFeedback> fb) {
                if (fb.error) failure = [NSString stringWithFormat:@"%@", fb.error.localizedDescription];
            }];
            CFAbsoluteTime began = CFAbsoluteTimeGetCurrent();
            [q commit:&cb count:1 options:co];
            [q signalEvent:ev value:run + 1];
            if (![ev waitUntilSignaledValue:run + 1 timeoutMS:600000]) { printf("no completion in ten minutes\n"); return 1; }
            double wall = CFAbsoluteTimeGetCurrent() - began;
            const uint32_t *a = arena.contents, *f = fail.contents;
            unsigned wrong = 0, held = 0;
            for (NSUInteger i = 0; i < fail_size / 4; i++) wrong += f[i] != 0;
            for (unsigned i = 0; i < 4096; i++) held += a[16 + i] != 0;
            printf("run %u: %.4f s; threads gave up on a lock: %s; %u wrong results; %u locks left held%s%s\n", run, wall, a[2] ? "yes" : "no", wrong, held,
                   failure ? "; error: " : "", failure ? failure.UTF8String : "");
        }
    }
    return 0;
}
