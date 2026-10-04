#import <Metal/Metal.h>
#include <stdio.h>
/* Mip blend weight in a fragment function for an implicit LOD of exactly 0 (one texel per pixel)
 * plus a bias, against an explicit level() of the same value. Level 2 holds 0, level 3 holds 1. Each
 * pixel column uses its own bias. */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct V { float4 pos [[position]]; };\n"
    "vertex V vs(uint vid [[vertex_id]]) { V o; o.pos = float4(float((vid << 1) & 2) * 2.0 - 1.0, float(vid & 2) * 2.0 - 1.0, 0.0, 1.0); return o; }\n"
    "fragment float4 fs(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant float *lods [[buffer(0)]]) {\n"
    "  float b = lods[uint(in.pos.x)];\n"
    "  float2 uv = in.pos.xy / 256.0;\n"
    "  return float4(t.sample(s, uv, bias(b)).r, t.sample(s, uv, level(b)).r, t.sample(s, uv).r, 1.0); }\n";
int main(void) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
    id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
    if (!ps) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:256 height:256 mipmapped:YES];
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    for (int l = 0; l < (int)t.mipmapLevelCount; l++) { int w = 256 >> l; float *d = malloc(w * w * 4); for (int i = 0; i < w * w; i++) d[i] = l == 3 ? 1.0f : 0.0f;
        [t replaceRegion:MTLRegionMake2D(0, 0, w, w) mipmapLevel:l withBytes:d bytesPerRow:w * 4]; free(d); }
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; sd.mipFilter = MTLSamplerMipFilterLinear;
    id<MTLSamplerState> ss = [dev newSamplerStateWithDescriptor:sd];
    enum { N = 256 };
    float lods[N]; for (int i = 0; i < N; i++) lods[i] = 2.0f + i / 256.0f;
    lods[0] = 2.598424f; lods[1] = 2.59765625f; lods[2] = 2.6015625f;
    id<MTLBuffer> in = [dev newBufferWithBytes:lods length:sizeof(lods) options:MTLResourceStorageModeShared];
    td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:256 height:256 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModeShared;
    id<MTLTexture> rt = [dev newTextureWithDescriptor:td];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new];
    rp.colorAttachments[0].texture = rt; rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cb = [[dev newCommandQueue] commandBuffer]; id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
    [re setRenderPipelineState:ps]; [re setFragmentTexture:t atIndex:0]; [re setFragmentSamplerState:ss atIndex:0]; [re setFragmentBuffer:in offset:0 atIndex:0];
    [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [re endEncoding]; [cb commit]; [cb waitUntilCompleted];
    static float px[256 * 4]; [rt getBytes:px bytesPerRow:256 * 16 fromRegion:MTLRegionMake2D(0, 100, 256, 1) mipmapLevel:0];
    printf("unbiased implicit sample (LOD 0 expected, level 0 holds 0): %g\n", px[2]);
    int lower = 0, higher = 0; double worst = 0;
    for (int i = 0; i < N; i++) { double b = px[i * 4] * 64, l = px[i * 4 + 1] * 64, ideal = (lods[i] - 2.0) * 64; if (b < l) lower++; if (b > l) higher++; if (ideal - b > worst) worst = ideal - b;
        if (i < 3) printf("bias %.6f: ideal weight %7.3f/64, implicit LOD + bias %7.3f/64, explicit level %7.3f/64\n", lods[i], ideal, b, l); }
    printf("over %d biases the implicit path gives a lower weight than level() %d times and a higher one %d times; largest shortfall against the ideal: %.3f/64\n", N, lower, higher, worst);
} return 0; }
