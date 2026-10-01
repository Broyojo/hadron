#import <Metal/Metal.h>
#include <stdio.h>
static NSString *src(const char *extra) {
   return [NSString stringWithFormat:@
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float *out [[buffer(0)]]) {\n"
   "  %s\n"
   "  out[0] = t.sample(s, float2(0.5, 0.5), level(0)).x;\n"
   "  out[1] = t.sample(s, float2(0.5, 0.5), level(0.5)).x;\n"
   "  out[2] = t.sample(s, float2(0.25, 0.25), level(0)).x; }\n", extra];
}
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:4 height:4 mipmapped:YES];
      d.storageMode = MTLStorageModeShared;
      id<MTLTexture> t = [dev newTextureWithDescriptor:d];
      float px[16]; for (int i = 0; i < 16; i++) px[i] = (i % 2) ? 1.0f : 0.0f;
      [t replaceRegion:MTLRegionMake2D(0,0,4,4) mipmapLevel:0 withBytes:px bytesPerRow:16];
      float px1[4] = {0.25f, 0.75f, 0.25f, 0.75f};
      [t replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:1 withBytes:px1 bytesPerRow:8];
      id<MTLCommandQueue> q = [dev newCommandQueue];
      const char *variants[] = {"", "constexpr sampler s2(filter::linear, mip_filter::linear); (void)s2;"};
      for (int mode = 0; mode < 3; mode++) for (int ab = 0; ab < 2; ab++) {
         id<MTLLibrary> lib = [dev newLibraryWithSource:src(variants[0]) options:nil error:&err];
         id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
         MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
         sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
         sd.mipFilter = MTLSamplerMipFilterLinear;
         sd.reductionMode = (MTLSamplerReductionMode)mode;
         sd.supportArgumentBuffers = ab;
         id<MTLSamplerState> ss = [dev newSamplerStateWithDescriptor:sd];
         id<MTLBuffer> out = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
         id<MTLCommandBuffer> cb = [q commandBuffer];
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         [ce setComputePipelineState:ps]; [ce setTexture:t atIndex:0]; [ce setSamplerState:ss atIndex:0]; [ce setBuffer:out offset:0 atIndex:0];
         [ce dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
         [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
         float *o = out.contents;
         printf("mode %d argbuf %d: lod0=%.3f lod0.5=%.3f off=%.3f\n", mode, ab, o[0], o[1], o[2]);
      }
      /* MSL-side reduction, if the language has it */
      const char *msl_variants[] = {"reduction::minimum", "reduction::min", "sampler_reduction::minimum"};
      for (int i = 0; i < 3; i++) {
         NSString *s = [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\nconstexpr sampler s(filter::linear, %s);\nkernel void k(){}\n", @(msl_variants[i])];
         id<MTLLibrary> l = [dev newLibraryWithSource:s options:nil error:&err];
         printf("MSL %s: %s\n", msl_variants[i], l ? "compiles" : "no");
      }
   }
   return 0;
}
