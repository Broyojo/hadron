#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
/* Precision of texture filtering weights: between two mip levels (sampled with an explicit LOD
 * from 2.0 to 3.0 in steps of 1/256) and between two texel centres (bilinear, in steps of 1/1024).
 * The number of distinct weights is 2^bits + 1. */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "kernel void mip(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float *out [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
    "  out[i] = t.sample(s, float2(0.5, 0.5), level(2.0 + float(i) / 256.0)).r; }\n"
    "kernel void texel(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float *out [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
    "  out[i] = t.sample(s, float2((0.5 + float(i) / 1024.0) / 2.0, 0.5), level(0)).r; }\n";

static int run(id<MTLDevice> dev, id<MTLLibrary> lib, const char *fn, id<MTLTexture> t, int n)
{
    NSError *e = nil;
    id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@(fn)] error:&e];
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.mipFilter = MTLSamplerMipFilterLinear;
    id<MTLSamplerState> ss = [dev newSamplerStateWithDescriptor:sd];
    id<MTLBuffer> out = [dev newBufferWithLength:(n + 1) * 4 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [[dev newCommandQueue] commandBuffer];
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:ps];
    [ce setTexture:t atIndex:0];
    [ce setSamplerState:ss atIndex:0];
    [ce setBuffer:out offset:0 atIndex:0];
    [ce dispatchThreads:MTLSizeMake(n + 1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [ce endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    float *r = out.contents;
    int distinct = 1;
    for (int i = 1; i <= n; i++)
        if (r[i] != r[i - 1]) distinct++;
    return distinct;
}

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&e];

        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:64 height:64 mipmapped:YES];
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> mips = [dev newTextureWithDescriptor:td];
        for (int l = 0; l < (int)mips.mipmapLevelCount; l++) {
            int w = 64 >> l;
            float *d = malloc(w * w * 4);
            for (int i = 0; i < w * w; i++) d[i] = l == 3 ? 1.0f : 0.0f;
            [mips replaceRegion:MTLRegionMake2D(0, 0, w, w) mipmapLevel:l withBytes:d bytesPerRow:w * 4];
            free(d);
        }

        td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:2 height:1 mipmapped:NO];
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> texels = [dev newTextureWithDescriptor:td];
        float d[2] = {0, 1};
        [texels replaceRegion:MTLRegionMake2D(0, 0, 2, 1) mipmapLevel:0 withBytes:d bytesPerRow:8];

        printf("between mip levels: %d distinct weights\n", run(dev, lib, "mip", mips, 256));
        printf("between texel centres: %d distinct weights\n", run(dev, lib, "texel", texels, 1024));
    }
    return 0;
}
