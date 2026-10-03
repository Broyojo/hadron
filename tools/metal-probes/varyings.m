#import <Metal/Metal.h>
#include <stdio.h>
/* How many user varying components a render pipeline takes, as scalar members (mixed float and flat
 * uint) and as float4 members, with built-in fragment inputs read as well; and whether a value
 * interpolates to the same bits in a scalar member and in a component of a vector member. */
static id<MTLRenderPipelineState> build(id<MTLDevice> dev, NSString *src, NSError **err)
{
    id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:err];
    if (!lib) return nil;
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@"vs"];
    d.fragmentFunction = [lib newFunctionWithName:@"fs"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Uint;
    return [dev newRenderPipelineStateWithDescriptor:d error:err];
}

static void limit(id<MTLDevice> dev, int vec4, int builtins, int n)
{
    NSMutableString *members = [NSMutableString string];
    for (int i = 0; i < n; i++) {
        const char *type = vec4 ? "float4" : (i % 3 == 0 ? "uint" : "float");
        [members appendFormat:@"%s v%d [[user(v%d)]]%s;\n", type, i, i, !vec4 && i % 3 == 0 ? " [[flat]]" : ""];
    }
    NSMutableString *src = [NSMutableString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct V { float4 pos [[position]];\n%@};\nstruct F { %@\n%@};\n"
        "vertex V vs(uint id [[vertex_id]]) { V o = {}; o.pos = float4(0, 0, 0, 1);\n", members,
        builtins ? @"float4 fc [[position]]; bool ff [[front_facing]]; uint sid [[sample_id]];" : @"", members];
    for (int i = 0; i < n; i++) [src appendFormat:@"o.v%d = id + %d;\n", i, i];
    [src appendFormat:@"return o; }\nfragment uint4 fs(F in [[stage_in]]) { float s = %s;\n",
        builtins ? "in.fc.x + float(in.ff) + float(in.sid)" : "0"];
    for (int i = 0; i < n; i++) [src appendFormat:vec4 ? @"s += in.v%d.x + in.v%d.w;\n" : @"s += float(in.v%d);\n", i, i];
    [src appendString:@"return uint4(s); }\n"];
    NSError *err = nil;
    id<MTLRenderPipelineState> ps = build(dev, src, &err);
    printf("%s, %s built-ins, %3d members (%3d components): %s\n", vec4 ? "float4" : "scalar",
           builtins ? "with" : "no", n, vec4 ? 4 * n : n, ps ? "ok" : err.localizedDescription.UTF8String);
}

static void interpolation(id<MTLDevice> dev)
{
    NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct V { float4 pos [[position]]; float s0 [[user(s0)]]; float s1 [[user(s1)]]; float4 v0 [[user(v0)]]; float4 v1 [[user(v1)]]; };\n"
        "vertex V vs(uint id [[vertex_id]]) { float2 p[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};\n"
        " float w[3] = {1.0, 2.7, 0.6}; float val[3] = {0.1234567, 7.654321, -3.3333};\n"
        " V o; o.pos = float4(p[id] * w[id], 0.3 * w[id], w[id]); float x = val[id];\n"
        " o.s0 = x; o.s1 = x; o.v0 = float4(x, x, 0, 0); o.v1 = float4(0, x, x, 0); return o; }\n"
        "struct F { float s0 [[user(s0)]]; float s1 [[user(s1)]]; float4 v0 [[user(v0)]]; float4 v1 [[user(v1)]]; };\n"
        "fragment uint4 fs(F in [[stage_in]]) { return uint4(in.s0 != in.s1, in.s0 != in.v0.x, in.v0.x != in.v0.y, in.v0.y != in.v1.z); }\n";
    NSError *err = nil;
    id<MTLRenderPipelineState> ps = build(dev, src, &err);
    if (!ps) { printf("%s\n", err.localizedDescription.UTF8String); return; }
    const int W = 256;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint width:W height:W mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    id<MTLCommandBuffer> cb = [[dev newCommandQueue] commandBuffer];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = t;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
    [e setRenderPipelineState:ps];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    [cb commit]; [cb waitUntilCompleted];
    static uint8_t px[256 * 256 * 4];
    [t getBytes:px bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, W) mipmapLevel:0];
    int n[4] = {0};
    for (int i = 0; i < W * W; i++) for (int c = 0; c < 4; c++) n[c] += px[i * 4 + c];
    printf("interpolation over %d pixels: scalar vs scalar %d differ, scalar vs vector %d, "
           "vector components %d, two vectors %d\n", W * W, n[0], n[1], n[2], n[3]);
}

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        int scalar[] = {120, 124, 125};
        for (int i = 0; i < 3; i++) limit(dev, 0, 0, scalar[i]);
        for (int i = 0; i < 3; i++) limit(dev, 0, 1, scalar[i]);
        limit(dev, 1, 0, 31);
        limit(dev, 1, 0, 32);
        interpolation(dev);
    }
    return 0;
}
