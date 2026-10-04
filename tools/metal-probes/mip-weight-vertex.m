#import <Metal/Metal.h>
#include <stdio.h>
/* Mip blend weight for an explicit LOD when the sample is taken in a vertex function, against the
 * same sample in a fragment function. Level 2 holds 0, level 3 holds 1, so the red result is the
 * weight. One point per LOD is drawn into a row of pixels. */
static const char *src =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct V { float4 pos [[position]]; float size [[point_size]]; float w; float lod; };\n"
    "struct F { float4 pos [[position]]; float w; float lod; };\n"
    "vertex V vs(uint vid [[vertex_id]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant float *lods [[buffer(0)]]) {\n"
    "  V o; o.pos = float4((float(vid) + 0.5) / 32.0 - 1.0, 0.0, 0.0, 1.0); o.size = 1.0;\n"
    "  o.lod = lods[vid]; o.w = t.sample(s, float2(0.5, 0.5), level(o.lod)).r; return o; }\n"
    "fragment float4 fs(F in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {\n"
    "  return float4(in.w, t.sample(s, float2(0.5, 0.5), level(in.lod)).r, 0.0, 1.0); }\n";
int main(void) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float; pd.inputPrimitiveTopology = MTLPrimitiveTopologyClassPoint;
    id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
    if (!ps) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:256 height:256 mipmapped:YES];
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    for (int l = 0; l < (int)t.mipmapLevelCount; l++) { int w = 256 >> l; float *d = malloc(w * w * 4); for (int i = 0; i < w * w; i++) d[i] = l == 3 ? 1.0f : 0.0f;
        [t replaceRegion:MTLRegionMake2D(0, 0, w, w) mipmapLevel:l withBytes:d bytesPerRow:w * 4]; free(d); }
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; sd.mipFilter = MTLSamplerMipFilterLinear;
    id<MTLSamplerState> ss = [dev newSamplerStateWithDescriptor:sd];
    enum { N = 64 };
    float lods[N]; for (int i = 0; i < N; i++) lods[i] = 2.0f + (i + 0.3f) / 64.0f;
    lods[0] = 2.598424f; lods[1] = 2.59765625f;
    id<MTLBuffer> in = [dev newBufferWithBytes:lods length:sizeof(lods) options:MTLResourceStorageModeShared];
    td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:N height:1 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModeShared;
    id<MTLTexture> rt = [dev newTextureWithDescriptor:td];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new];
    rp.colorAttachments[0].texture = rt; rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cb = [[dev newCommandQueue] commandBuffer]; id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
    [re setRenderPipelineState:ps]; [re setVertexTexture:t atIndex:0]; [re setVertexSamplerState:ss atIndex:0]; [re setVertexBuffer:in offset:0 atIndex:0];
    [re setFragmentTexture:t atIndex:0]; [re setFragmentSamplerState:ss atIndex:0];
    [re drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:N]; [re endEncoding]; [cb commit]; [cb waitUntilCompleted];
    float px[N * 4]; [rt getBytes:px bytesPerRow:N * 16 fromRegion:MTLRegionMake2D(0, 0, N, 1) mipmapLevel:0];
    int differ = 0; double worst = 0;
    for (int i = 0; i < N; i++) { double v = px[i * 4] * 64, f = px[i * 4 + 1] * 64, ideal = (lods[i] - 2.0) * 64; if (v != f) differ++; if (ideal - v > worst) worst = ideal - v;
        if (i < 6) printf("LOD %.6f: ideal weight %7.3f/64, vertex function %7.3f/64, fragment function %7.3f/64\n", lods[i], ideal, v, f); }
    printf("%d of %d LODs give a different weight in the vertex function; largest shortfall against the ideal weight there: %.3f/64\n", differ, N, worst);
} return 0; }
