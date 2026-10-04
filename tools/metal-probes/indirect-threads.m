#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* A compute pipeline built by MTL4Compiler with maxTotalThreadsPerThreadgroup set, dispatched with
 * dispatchThreadsWithIndirectBuffer on a Metal 4 queue, against the same dispatch made directly.
 * Each thread computes a value from many live registers and stores it; the direct dispatch is the
 * reference. Prints, per pipeline thread limit, how many threads differ. */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "kernel void k(device const float *in [[buffer(0)]], device float *out [[buffer(1)]],\n"
    "              uint i [[thread_position_in_grid]]) {\n"
    "  float v[N];\n"
    "  for (int j = 0; j < N; j++) v[j] = in[(i * 7u + j) % 1024u];\n"
    "  float s = 0;\n"
    "  for (int r = 0; r < 4; r++) for (int j = 0; j < N; j++) { s += v[j] * v[(j + r + 1) % N]; v[j] = s * 0.5f - v[j]; }\n"
    "  for (int j = 0; j < N; j++) s += v[j];\n"
    "  out[i] = s; }\n";

static int run(id<MTLDevice> dev, id<MTL4Compiler> comp, int live, int max_threads, int tg, int threads, bool groups)
{
    NSError *e = nil;
    char *code = malloc(strlen(src) + 64);
    snprintf(code, strlen(src) + 64, "#define N %d\n%s", live, src);
    MTL4LibraryDescriptor *ld = [MTL4LibraryDescriptor new];
    ld.source = @(code);
    free(code);
    id<MTLLibrary> lib = [comp newLibraryWithDescriptor:ld error:&e];
    if (!lib) { printf("compile: %s\n", e.localizedDescription.UTF8String); exit(1); }
    MTL4LibraryFunctionDescriptor *fd = [MTL4LibraryFunctionDescriptor new];
    fd.name = @"k"; fd.library = lib;
    MTL4ComputePipelineDescriptor *pd = [MTL4ComputePipelineDescriptor new];
    pd.computeFunctionDescriptor = fd;
    if (max_threads) pd.maxTotalThreadsPerThreadgroup = max_threads;
    id<MTLComputePipelineState> ps = [comp newComputePipelineStateWithDescriptor:pd compilerTaskOptions:nil error:&e];
    if (!ps) { printf("pipeline: %s\n", e.localizedDescription.UTF8String); exit(1); }

    enum { IN = 0, OUT_DIRECT = 8192, OUT_INDIRECT = 16384, ARGS = 24576, SIZE = 32768 };
    id<MTLBuffer> b = [dev newBufferWithLength:SIZE options:MTLResourceStorageModeShared];
    uint8_t *m = b.contents; uint64_t g = b.gpuAddress;
    memset(m, 0, SIZE);
    for (int j = 0; j < 1024; j++) ((float *)(m + IN))[j] = (float)((j * 37) % 101) / 101.0f;
    uint32_t *args = (uint32_t *)(m + ARGS);
    args[0] = groups ? (threads + tg - 1) / tg : threads; args[1] = 1; args[2] = 1;
    args[3] = tg; args[4] = 1; args[5] = 1;

    id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
    id<MTLResidencySet> rs = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&e];
    [rs addAllocation:b]; [rs commit];
    [q addResidencySet:rs];
    id<MTL4CommandAllocator> al = [dev newCommandAllocator];
    id<MTL4CommandBuffer> cb = [dev newCommandBuffer];
    [cb beginCommandBufferWithAllocator:al];
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new];
    ad.maxBufferBindCount = 2;
    id<MTL4ArgumentTable> direct = [dev newArgumentTableWithDescriptor:ad error:&e];
    id<MTL4ArgumentTable> indirect = [dev newArgumentTableWithDescriptor:ad error:&e];
    [direct setAddress:g + IN atIndex:0]; [direct setAddress:g + OUT_DIRECT atIndex:1];
    [indirect setAddress:g + IN atIndex:0]; [indirect setAddress:g + OUT_INDIRECT atIndex:1];
    id<MTL4ComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:ps];
    [ce setArgumentTable:direct];
    if (groups)
        [ce dispatchThreadgroups:MTLSizeMake((threads + tg - 1) / tg, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    else
        [ce dispatchThreads:MTLSizeMake(threads, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [ce setArgumentTable:indirect];
    if (groups)
        [ce dispatchThreadgroupsWithIndirectBuffer:g + ARGS threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    else
        [ce dispatchThreadsWithIndirectBuffer:g + ARGS];
    [ce endEncoding];
    [cb endCommandBuffer];
    id<MTLSharedEvent> ev = [dev newSharedEvent];
    [q commit:&cb count:1];
    [q signalEvent:ev value:1];
    [ev waitUntilSignaledValue:1 timeoutMS:10000];

    /* Reference on the CPU, compared with a tolerance since fast math may contract differently */
    int bad = 0, bad_direct = 0;
    const float *in = (const float *)(m + IN);
    for (int t = 0; t < threads; t++) {
        float v[128], s = 0;
        for (int j = 0; j < live; j++) v[j] = in[(t * 7u + j) % 1024u];
        for (int r = 0; r < 4; r++)
            for (int j = 0; j < live; j++) { s += v[j] * v[(j + r + 1) % live]; v[j] = s * 0.5f - v[j]; }
        for (int j = 0; j < live; j++) s += v[j];
        float d = ((float *)(m + OUT_DIRECT))[t], i = ((float *)(m + OUT_INDIRECT))[t];
        if (fabsf(d - s) > 1e-3f * fmaxf(1.0f, fabsf(s))) bad_direct++;
        if (fabsf(i - s) > 1e-3f * fmaxf(1.0f, fabsf(s))) bad++;
    }
    if (bad_direct) printf("(direct wrong in %d) ", bad_direct);
    return bad;
}

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        id<MTL4Compiler> comp = [dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:NULL];
        int lives[] = {8, 32, 64, 96, 128};
        int limits[] = {0, 32, 48, 64, 96, 128};
        for (int groups = 0; groups < 2; groups++) {
        printf("%s: threads whose indirect result is wrong of 48 threads in groups of 32 or 64\n",
               groups ? "dispatchThreadgroupsWithIndirectBuffer (64 threads run)" : "dispatchThreadsWithIndirectBuffer");
        printf("live floats \\ maxTotalThreadsPerThreadgroup:");
        for (unsigned l = 0; l < sizeof(limits) / sizeof(limits[0]); l++) printf(" %6d", limits[l]);
        printf("\n");
        for (unsigned v = 0; v < sizeof(lives) / sizeof(lives[0]); v++) {
            printf("%11d %31s", lives[v], "");
            for (unsigned l = 0; l < sizeof(limits) / sizeof(limits[0]); l++) {
                int tg = limits[l] && limits[l] < 64 ? 32 : 64;
                printf(" %6d", run(dev, comp, lives[v], limits[l], tg, getenv("THREADS") ? atoi(getenv("THREADS")) : 48, groups));
            }
            printf("\n");
        }
        }
    }
    return 0;
}
