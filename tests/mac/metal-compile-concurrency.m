#import <Metal/Metal.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>

/* How many shaders Metal compiles at once through one MTL4Compiler, as KosmicKrisp compiles
 * every pipeline: a library from MSL, then a compute pipeline. Compiles <count> different
 * kernels from <threads> threads, with MTLDevice's shouldMaximizeConcurrentCompilation as given.
 * Each run's sources carry a random token, so Metal's shader cache has none of them.
 *
 *   clang -fobjc-arc -framework Metal -framework Foundation -o metal-compile-concurrency \
 *       tests/mac/metal-compile-concurrency.m
 *   metal-compile-concurrency <threads> <maximize 0|1> [count] */

static double now_ms(void)
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() * (double)tb.numer / tb.denom / 1e6;
}

/* A kernel that takes Metal's compiler a noticeable time: a long chain of dependent math. */
static NSString *kernel_source(unsigned token, unsigned index)
{
    NSMutableString *s = [NSMutableString stringWithFormat:
        @"#include <metal_stdlib>\nusing namespace metal;\n"
         "kernel void k_%u_%u(device float4 *buf [[buffer(0)]], uint id [[thread_position_in_grid]]) {\n"
         "    float4 v = buf[id] + float4(%u.0, %u.0, 1.0, 2.0);\n", token, index, token % 1000, index];
    for (int i = 0; i < 400; i++)
        [s appendFormat:@"    v = fma(v, float4(%d.5), sin(v.yzwx * %d.25)) / (abs(v.wxyz) + 1.0);\n"
                         "    if (v.x > %d.0) v = cos(v) * v.zwxy;\n", i % 7 + 1, i % 5 + 1, i];
    [s appendString:@"    buf[id] = v;\n}\n"];
    return s;
}

int main(int argc, const char **argv)
{
    @autoreleasepool {
        if (argc < 3) { fprintf(stderr, "usage: %s <threads> <maximize 0|1> [count]\n", argv[0]); return 2; }
        int threads = atoi(argv[1]), maximize = atoi(argv[2]), count = argc > 3 ? atoi(argv[3]) : 48;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        device.shouldMaximizeConcurrentCompilation = maximize != 0;
        NSError *error;
        id<MTL4Compiler> compiler = [device newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&error];
        if (!compiler) { fprintf(stderr, "no compiler: %s\n", error.localizedDescription.UTF8String); return 1; }

        unsigned token = arc4random();
        NSMutableArray<NSString *> *sources = [NSMutableArray array];
        for (int i = 0; i < count; i++) [sources addObject:kernel_source(token, i)];

        __block int failed = 0;
        dispatch_queue_t queue = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
        dispatch_semaphore_t slots = dispatch_semaphore_create(threads);
        dispatch_group_t group = dispatch_group_create();
        double start = now_ms();
        for (int i = 0; i < count; i++) {
            dispatch_semaphore_wait(slots, DISPATCH_TIME_FOREVER);
            dispatch_group_async(group, queue, ^{
                @autoreleasepool {
                    MTL4LibraryDescriptor *ld = [MTL4LibraryDescriptor new];
                    ld.source = sources[i];
                    ld.options = [MTLCompileOptions new];
                    NSError *e;
                    id<MTLLibrary> lib = [compiler newLibraryWithDescriptor:ld error:&e];
                    MTL4LibraryFunctionDescriptor *fd = [MTL4LibraryFunctionDescriptor new];
                    fd.name = [NSString stringWithFormat:@"k_%u_%d", token, i];
                    fd.library = lib;
                    MTL4ComputePipelineDescriptor *pd = [MTL4ComputePipelineDescriptor new];
                    pd.computeFunctionDescriptor = fd;
                    if (!lib || ![compiler newComputePipelineStateWithDescriptor:pd compilerTaskOptions:nil error:&e]) {
                        fprintf(stderr, "kernel %d: %s\n", i, e.localizedDescription.UTF8String);
                        __atomic_add_fetch(&failed, 1, __ATOMIC_RELAXED);
                    }
                }
                dispatch_semaphore_signal(slots);
            });
        }
        dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
        double ms = now_ms() - start;
        printf("threads %d, maximize %d (concurrent tasks %lu): %d kernels in %.0f ms, %.0f ms each%s\n",
               threads, maximize, (unsigned long)device.maximumConcurrentCompilationTaskCount, count, ms,
               ms / count, failed ? ", SOME FAILED" : "");
        return failed != 0;
    }
}
