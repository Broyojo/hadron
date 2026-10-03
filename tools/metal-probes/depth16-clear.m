#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
/* Clears a Depth16Unorm texture with a render pass to n / 65535 (computed in float, as a Vulkan
 * driver receives it in VkClearDepthStencilValue) and counts the clears that do not store n: Metal
 * truncates (1/65535 stores 0). BIAS=x clears to (round(d * 65535) + x) / 65535 instead; with
 * x = 0.5 every value is stored exactly. DRAW=1 writes the depth by drawing a triangle at that z
 * instead of clearing (the depth is still cleared to it first, so DRAW=1 BIAS unset shows the draw). */
int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> q = [dev newCommandQueue];
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth16Unorm
                                                                                      width:4 height:4 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> t = [dev newTextureWithDescriptor:td];
        id<MTLBuffer> out = [dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
        int wrong = 0;
        for (unsigned n = 0; n <= 65535; n++) {
            float d = (float)n / 65535.0f;
            double clear = d;
            if (getenv("BIAS"))
                clear = fmin((rint((double)d * 65535.0) + atof(getenv("BIAS"))) / 65535.0, 1.0);
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.depthAttachment.texture = t;
            rp.depthAttachment.loadAction = MTLLoadActionClear;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            rp.depthAttachment.clearDepth = getenv("DRAW") ? 1.0 : clear;
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
            if (getenv("DRAW")) {
                /* Write the depth by drawing a triangle at that z instead of clearing to it */
                static id<MTLRenderPipelineState> ps;
                static id<MTLDepthStencilState> ds;
                if (!ps) {
                    NSError *e = nil;
                    id<MTLLibrary> lib = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
                        "vertex float4 v(uint i [[vertex_id]], constant float &z [[buffer(0)]]) { return float4(float(i & 1) * 4 - 1, float(i >> 1) * 4 - 1, z, 1); }\n"
                        "fragment void f() {}\n" options:nil error:&e];
                    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
                    pd.vertexFunction = [lib newFunctionWithName:@"v"];
                    pd.fragmentFunction = [lib newFunctionWithName:@"f"];
                    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth16Unorm;
                    ps = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
                    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
                    dd.depthCompareFunction = MTLCompareFunctionAlways;
                    dd.depthWriteEnabled = YES;
                    ds = [dev newDepthStencilStateWithDescriptor:dd];
                }
                float z = (float)clear;
                [re setRenderPipelineState:ps];
                [re setDepthStencilState:ds];
                [re setVertexBytes:&z length:4 atIndex:0];
                [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }
            [re endEncoding];
            id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
            [be copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(1, 1, 1)
                       toBuffer:out destinationOffset:0 destinationBytesPerRow:2 destinationBytesPerImage:2];
            [be endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            unsigned got = *(uint16_t *)out.contents;
            if (got != n) {
                if (wrong < 10) printf("clear %u/65535 (%.9g) stored %u\n", n, d, got);
                wrong++;
            }
            if (n == 256 && !getenv("ALL")) break;
        }
        printf("%d wrong\n", wrong);
    }
    return 0;
}
