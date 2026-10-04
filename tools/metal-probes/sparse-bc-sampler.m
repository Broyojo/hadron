#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* Shader view of the BC1 tail aliasing: write via blit, read via compute sampling (nearest, explicit level). */
static id<MTLDevice> dev; static id<MTLCommandQueue> q3; static id<MTLComputePipelineState> ps; static id<MTLSamplerState> smp;
static int W, H; static MTLPixelFormat PF = MTLPixelFormatBC1_RGBA; static int BD = 4, BB = 8;
static id<MTLTexture> mk(int sparse, id<MTLHeap> *hp)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:PF width:W height:H mipmapped:YES];
    td.storageMode = MTLStorageModePrivate; td.usage = MTLTextureUsageShaderRead;
    if (!sparse) return [dev newTextureWithDescriptor:td];
    td.placementSparsePageSize = MTLSparsePageSize64;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    MTLHeapDescriptor *hd = [MTLHeapDescriptor new]; hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate;
    hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64; hd.size = 1 << 20;
    id<MTLHeap> heap = [dev newHeapWithDescriptor:hd]; *hp = heap;
    MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:PF sampleCount:1 sparsePageSize:MTLSparsePageSize64];
    MTL4UpdateSparseTextureMappingOperation ops[20]; int n = 0; NSUInteger hpo = 0;
    for (NSUInteger l = 0; l < t.firstMipmapInTail && l < t.mipmapLevelCount; l++) {
        NSUInteger lw = MAX(W >> l, 1), lh = MAX(H >> l, 1), tx = (lw + tile.width - 1) / tile.width, ty = (lh + tile.height - 1) / tile.height;
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0,0,0,tx,ty,1), .textureLevel = l, .heapOffset = hpo}; hpo += tx * ty; }
    if (t.firstMipmapInTail < t.mipmapLevelCount)
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0,0,0,(t.tailSizeInBytes+65535)/65536,1,1), .textureLevel = t.firstMipmapInTail, .heapOffset = hpo};
    id<MTL4CommandQueue> q = [dev newMTL4CommandQueue]; [q updateTextureMappings:t heap:heap operations:ops count:n];
    id<MTLSharedEvent> ev = [dev newSharedEvent]; [q signalEvent:ev value:1]; [ev waitUntilSignaledValue:1 timeoutMS:5000];
    return t;
}
static void put(id<MTLTexture> t, id<MTLBuffer> src, size_t *offs, int l)
{
    int lw = W >> l ?: 1, lh = H >> l ?: 1;
    id<MTLCommandBuffer> cb = [q3 commandBuffer]; id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromBuffer:src sourceOffset:offs[l] sourceBytesPerRow:(lw+BD-1)/BD*BB sourceBytesPerImage:0 sourceSize:MTLSizeMake(lw,lh,1) toTexture:t destinationSlice:0 destinationLevel:l destinationOrigin:MTLOriginMake(0,0,0)];
    [b endEncoding]; [cb commit]; [cb waitUntilCompleted];
}
static void get(id<MTLTexture> t, int l, id<MTLBuffer> out)
{
    id<MTLCommandBuffer> cb = [q3 commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:ps]; [e setTexture:t atIndex:0]; [e setSamplerState:smp atIndex:0]; [e setBuffer:out offset:0 atIndex:0];
    uint32_t lv[3] = {(uint32_t)l, (uint32_t)(W >> l ?: 1), (uint32_t)(H >> l ?: 1)}; [e setBytes:lv length:12 atIndex:1];
    [e dispatchThreads:MTLSizeMake(lv[1], lv[2], 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
    if (cb.error) printf("err %s\n", cb.error.description.UTF8String);
}
static void cmp(const char *what, id<MTLBuffer> a, id<MTLBuffer> b, int l)
{
    int lw = W >> l ?: 1, lh = H >> l ?: 1, bw = (lw+3)/4; int nb = 0; char seen[4096] = {0};
    for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++)
        if (memcmp((float *)a.contents + 4*(y*lw+x), (float *)b.contents + 4*(y*lw+x), 16)) { int bi = (y/4)*bw + x/4; if (!seen[bi]) { seen[bi] = 1; nb++; printf("  %s: L%d block (%d,%d) differs\n", what, l, x/4, y/4); } }
    if (!nb) printf("  %s: L%d identical\n", what, l);
}
int main(int argc, char **argv) { @autoreleasepool {
    W = atoi(argv[1]); H = atoi(argv[2]); dev = MTLCreateSystemDefaultDevice(); q3 = [dev newCommandQueue];
    NSError *err; id<MTLLibrary> lib = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
      "kernel void k(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], constant uint3 &p [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {"
      " if (g.x >= p.y || g.y >= p.z) return; o[g.y * p.y + g.x] = t.sample(s, (float2(g) + 0.5) / float2(p.yz), level(float(p.x))); }" options:nil error:&err];
    if (!lib) { printf("%s\n", err.description.UTF8String); return 1; }
    ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest; sd.mipFilter = MTLSamplerMipFilterNearest; smp = [dev newSamplerStateWithDescriptor:sd];
    if (argc > 3 && !strcmp(argv[3], "grid")) { int bad = 0, n = 0; const char *fm = argc > 4 ? argv[4] : "BC1"; if (!strcmp(fm, "RGBA8")) { PF = MTLPixelFormatRGBA8Unorm; BD = 1; BB = 4; } if (!strcmp(fm, "BC7")) { PF = MTLPixelFormatBC7_RGBAUnorm; BB = 16; }
      for (H = 4; H <= 140; H += 4) for (W = 4; W <= 140; W += 4) { n++; q3 = [dev newCommandQueue];
        id<MTLHeap> h2; id<MTLTexture> S2 = mk(1, &h2), P2 = mk(0, NULL);
        MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new];
        id<MTLResidencySet> s2 = [dev newResidencySetWithDescriptor:rd error:nil]; [s2 addAllocation:h2]; [s2 addAllocation:S2]; [s2 commit]; [q3 addResidencySet:s2];
        int levels = (int)S2.mipmapLevelCount; size_t offs[16], tot = 0;
        for (int l = 0; l < levels; l++) { offs[l] = tot; tot += (size_t)(((W>>l?:1)+BD-1)/BD) * (((H>>l?:1)+BD-1)/BD) * BB; }
        id<MTLBuffer> src = [dev newBufferWithLength:tot options:MTLResourceStorageModeShared];
        srand(W*1000+H); for (size_t i = 0; i < tot; i++) ((uint8_t *)src.contents)[i] = rand();
        id<MTLBuffer> oa = [dev newBufferWithLength:W*H*16 options:0], ob = [dev newBufferWithLength:W*H*16 options:0];
        for (int l = 0; l < levels; l++) { put(S2, src, offs, l); put(P2, src, offs, l); }
        char line[256]; int pos = snprintf(line, sizeof line, "%dx%d:", W, H), any = 0;
        for (int l = 0; l < levels; l++) { get(S2, l, oa); get(P2, l, ob); int lw = W>>l?:1, lh = H>>l?:1;
          if (memcmp(oa.contents, ob.contents, (size_t)lw*lh*16)) { any = 1; pos += snprintf(line+pos, sizeof line - pos, " L%d", l);
            if (BD == 1) { int es = 0, ep = 0; const uint8_t *sb = (const uint8_t *)src.contents + offs[l];
              for (int i = 0; i < lw*lh*4; i++) { float e = sb[i] / 255.0f; if (fabsf(((float *)oa.contents)[i] - e) > 1e-6) es++; if (fabsf(((float *)ob.contents)[i] - e) > 1e-6) ep++; }
              if (getenv("DBG")) for (int i = 0; i < 8; i++) printf("  i%d exp %g sparse %g plain %g\n", i, sb[i]/255.0, ((float *)oa.contents)[i], ((float *)ob.contents)[i]);
              pos += snprintf(line+pos, sizeof line - pos, "(sparse %d wrong, plain %d wrong)", es, ep); } } }
        if (any) { bad++; printf("%s\n", line); } }
      printf("sampler: %d of %d sizes differ from plain\n", bad, n); return 0; }
    id<MTLHeap> heap = nil; id<MTLTexture> S = mk(1, &heap), P = mk(0, NULL);
    MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new]; id<MTLResidencySet> set = [dev newResidencySetWithDescriptor:rd error:nil];
    [set addAllocation:heap]; [set addAllocation:S]; [set commit]; [q3 addResidencySet:set];
    int levels = (int)S.mipmapLevelCount; size_t offs[16], tot = 0;
    for (int l = 0; l < levels; l++) { offs[l] = tot; tot += (size_t)(((W>>l?:1)+BD-1)/BD) * (((H>>l?:1)+BD-1)/BD) * BB; }
    id<MTLBuffer> src = [dev newBufferWithLength:tot options:MTLResourceStorageModeShared];
    srand(1); for (size_t i = 0; i < tot; i++) ((uint8_t *)src.contents)[i] = rand();
    id<MTLBuffer> oa = [dev newBufferWithLength:W*H*16 options:0], ob = [dev newBufferWithLength:W*H*16 options:0];
    /* 1: only L0 written in both */
    put(S, src, offs, 0); put(P, src, offs, 0);
    get(S, 0, oa); get(P, 0, ob); cmp("after L0 only, sparse vs plain", oa, ob, 0);
    int L = argc > 3 ? atoi(argv[3]) : 3;
    put(S, src, offs, L); put(P, src, offs, L);
    get(S, 0, oa); get(P, 0, ob); cmp("after L0 then L", oa, ob, 0);
    get(S, L, oa); get(P, L, ob); cmp("level L", oa, ob, L);
    { float *a = oa.contents, *b = ob.contents; printf("  sparse L%d texel0 %g %g %g %g, plain %g %g %g %g\n", L, a[0],a[1],a[2],a[3], b[0],b[1],b[2],b[3]); }
    /* fresh pair, all levels written in order, compare every level via the sampler */
    { id<MTLHeap> h2; id<MTLTexture> S2 = mk(1, &h2), P2 = mk(0, NULL);
      id<MTLResidencySet> s2 = [dev newResidencySetWithDescriptor:rd error:nil]; [s2 addAllocation:h2]; [s2 addAllocation:S2]; [s2 commit]; [q3 addResidencySet:s2];
      for (int l = 0; l < levels; l++) { put(S2, src, offs, l); put(P2, src, offs, l); }
      for (int l = 0; l < levels; l++) { get(S2, l, oa); get(P2, l, ob); cmp("all written", oa, ob, l); } }
} return 0; }
