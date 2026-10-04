#import <Metal/Metal.h>
#include <math.h>
#include <stdio.h>
/* Depth comparison against 32-bit float depth texels outside [0, 1]: Vulkan (with
 * VK_EXT_depth_range_unrestricted) and OpenGL compare floating point depth without clamping. Fills a
 * 1x1 texture with each depth, compares with each reference (LESS: reference < texel passes) through
 * sample_compare and gather_compare, and prints the result next to the unclamped answer. Three ways of
 * filling it: Depth32Float with replaceRegion, Depth32Float with a blit from a buffer, and
 * Depth32Float_Stencil8 with a blit from a buffer into its depth plane. A fourth case uses Depth16Unorm,
 * where Vulkan and OpenGL clamp the reference to [0, 1]: its expected column is the clamped answer. */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "kernel void k(depth2d<float> t [[texture(0)]], sampler s [[sampler(0)]],\n"
    "              constant float *refs [[buffer(0)]], device float2 *out [[buffer(1)]],\n"
    "              uint i [[thread_position_in_grid]]) {\n"
    "  out[i] = float2(t.sample_compare(s, float2(0.5), refs[i], level(0)),\n"
    "                  t.gather_compare(s, float2(0.5), refs[i]).x); }\n";

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&e];
        if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
        id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e];
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.compareFunction = MTLCompareFunctionLess;
        id<MTLSamplerState> ss = [dev newSamplerStateWithDescriptor:sd];
        id<MTLCommandQueue> q = [dev newCommandQueue];

        float depths[] = {0.5f, 1.0f, 1.5f, -0.5f, 0.0f};
        float refs[] = {-1.0f, 0.25f, 0.75f, 1.2f, 2.0f};
        const int nref = sizeof(refs) / sizeof(refs[0]);
        printf("texel   ref    sample  gather  expected\n");
        const char *modes[] = {"Depth32Float, replaceRegion", "Depth32Float, blit from buffer",
                               "Depth32Float_Stencil8, blit from buffer", "Depth16Unorm, blit from buffer"};
        for (int m = 0; m < 4; m++)
        for (unsigned d = 0; d < sizeof(depths) / sizeof(depths[0]); d++) {
            if (m == 3 && (depths[d] < 0.0f || depths[d] > 1.0f))
                continue;
            if (d == 0)
                printf("%s\n", modes[m]);
            MTLTextureDescriptor *td = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:m == 3 ? MTLPixelFormatDepth16Unorm
                                                : m == 2 ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatDepth32Float
                                             width:1 height:1 mipmapped:NO];
            td.storageMode = m == 0 ? MTLStorageModeShared : MTLStorageModePrivate;
            td.usage = MTLTextureUsageShaderRead;
            id<MTLTexture> t = [dev newTextureWithDescriptor:td];
            if (m == 0) {
                [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&depths[d] bytesPerRow:4];
            } else {
                uint16_t unorm = (uint16_t)(depths[d] * 65535.0f);
                id<MTLBuffer> src = m == 3 ? [dev newBufferWithBytes:&unorm length:2 options:MTLResourceStorageModeShared]
                                           : [dev newBufferWithBytes:&depths[d] length:4 options:MTLResourceStorageModeShared];
                unsigned bpp = m == 3 ? 2 : 4;
                id<MTLCommandBuffer> ub = [q commandBuffer];
                id<MTLBlitCommandEncoder> be = [ub blitCommandEncoder];
                [be copyFromBuffer:src sourceOffset:0 sourceBytesPerRow:bpp sourceBytesPerImage:bpp
                        sourceSize:MTLSizeMake(1, 1, 1) toTexture:t destinationSlice:0 destinationLevel:0
                 destinationOrigin:MTLOriginMake(0, 0, 0)
                           options:m == 2 ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
                [be endEncoding];
                [ub commit];
                [ub waitUntilCompleted];
            }
            id<MTLBuffer> rb = [dev newBufferWithBytes:refs length:sizeof(refs) options:MTLResourceStorageModeShared];
            id<MTLBuffer> ob = [dev newBufferWithLength:nref * 8 options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps];
            [ce setTexture:t atIndex:0];
            [ce setSamplerState:ss atIndex:0];
            [ce setBuffer:rb offset:0 atIndex:0];
            [ce setBuffer:ob offset:0 atIndex:1];
            [ce dispatchThreads:MTLSizeMake(nref, 1, 1) threadsPerThreadgroup:MTLSizeMake(nref, 1, 1)];
            [ce endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            float *o = ob.contents;
            for (int r = 0; r < nref; r++) {
                float ref = m == 3 ? fminf(fmaxf(refs[r], 0.0f), 1.0f) : refs[r];
                float want = ref < depths[d] ? 1.0f : 0.0f;
                printf("%5.2f  %5.2f   %4.1f    %4.1f    %4.1f%s\n", depths[d], refs[r], o[r * 2], o[r * 2 + 1], want,
                       (o[r * 2] != want || o[r * 2 + 1] != want) ? "  differs" : "");
            }
        }
    }
    return 0;
}
