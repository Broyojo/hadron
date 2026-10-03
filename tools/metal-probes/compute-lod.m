#import <Metal/Metal.h>
#include <stdio.h>
/* Whether implicit-LOD sampling and the LOD queries in a compute kernel use derivatives taken
 * across the quad group: each quad samples coordinates that step by k texels per lane, so real
 * derivatives give LOD log2(k); every mip level holds its own index. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float *out [[buffer(0)]],\n"
   "              constant float *step [[buffer(1)]], uint li [[thread_index_in_threadgroup]], uint3 gid [[thread_position_in_grid]]) {\n"
   "  float2 uv = float2(float(li & 1), float((li >> 1) & 1)) * step[0] + 0.25;\n"
   "  device float *o = out + (gid.y * 64 + gid.x) * 4;\n"
   "  o[0] = t.sample(s, uv).x;\n"
   "  o[1] = t.calculate_unclamped_lod(s, uv);\n"
   "  o[2] = t.calculate_clamped_lod(s, uv);\n"
   "  o[3] = t.sample(s, uv, bias(1.0)).x;\n"
   "}\n";
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLCommandQueue> q = [dev newCommandQueue];
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("compile: %s\n", err.localizedDescription.UTF8String); return 1; }
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
      MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:64 height:64 mipmapped:YES];
      td.storageMode = MTLStorageModeShared;
      id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
      for (unsigned l = 0; l < tex.mipmapLevelCount; l++) {
         unsigned w = 64 >> l; float texels[64 * 64];
         for (unsigned i = 0; i < w * w; i++) texels[i] = l;
         [tex replaceRegion:MTLRegionMake2D(0, 0, w, w) mipmapLevel:l withBytes:texels bytesPerRow:w * 4];
      }
      MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
      sd.minFilter = MTLSamplerMinMagFilterNearest; sd.magFilter = MTLSamplerMinMagFilterNearest; sd.mipFilter = MTLSamplerMipFilterLinear;
      id<MTLSamplerState> samp = [dev newSamplerStateWithDescriptor:sd];
      printf("device %s\n", dev.name.UTF8String);
      static const unsigned sizes[][2] = {{4, 1}, {16, 1}, {4, 4}, {8, 8}};
      for (unsigned si = 0; si < 4; si++) for (unsigned k = 1; k <= 8; k *= 2) {
         unsigned lx = sizes[si][0], ly = sizes[si][1];
         id<MTLBuffer> out = [dev newBufferWithLength:64 * 64 * 16 options:MTLResourceStorageModeShared];
         id<MTLBuffer> step = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
         *(float *)step.contents = k / 64.0f;
         id<MTLCommandBuffer> cb = [q commandBuffer];
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         [ce setComputePipelineState:ps]; [ce setTexture:tex atIndex:0]; [ce setSamplerState:samp atIndex:0];
         [ce setBuffer:out offset:0 atIndex:0]; [ce setBuffer:step offset:0 atIndex:1];
         [ce dispatchThreadgroups:MTLSizeMake(2, 1, 1) threadsPerThreadgroup:MTLSizeMake(lx, ly, 1)];
         [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
         float *o = out.contents, mn[4] = {1e9, 1e9, 1e9, 1e9}, mx[4] = {-1e9, -1e9, -1e9, -1e9};
         for (unsigned y = 0; y < ly; y++) for (unsigned x = 0; x < 2 * lx; x++) for (int c = 0; c < 4; c++) {
            float v = o[(y * 64 + x) * 4 + c]; if (v < mn[c]) mn[c] = v; if (v > mx[c]) mx[c] = v;
         }
         printf("local %2ux%u, %u texels per lane (real LOD %d): sample %.2f..%.2f  unclamped lod %.2f..%.2f  clamped lod %.2f..%.2f  sample bias 1 %.2f..%.2f\n",
                lx, ly, k, k == 1 ? 0 : k == 2 ? 1 : k == 4 ? 2 : 3, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], mn[3], mx[3]);
      }
   }
   return 0;
}
