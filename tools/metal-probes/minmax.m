#import <Metal/Metal.h>
#include <stdio.h>
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float *out [[buffer(0)]]) {\n"
   "  out[0] = t.sample(s, float2(0.5, 0.5), level(0)).x; }\n";
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      printf("family10=%d family9=%d family8=%d\n", [dev supportsFamily:MTLGPUFamilyApple10], [dev supportsFamily:MTLGPUFamilyApple9], [dev supportsFamily:MTLGPUFamilyApple8]);
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
      MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:2 height:2 mipmapped:NO];
      d.storageMode = MTLStorageModeShared;
      id<MTLTexture> t = [dev newTextureWithDescriptor:d];
      float px[4] = {0.0f, 1.0f, 0.25f, 0.75f};
      [t replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0 withBytes:px bytesPerRow:8];
      id<MTLCommandQueue> q = [dev newCommandQueue];
      for (int mode = 0; mode < 3; mode++) {
         MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
         sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
         sd.mipFilter = MTLSamplerMipFilterLinear;
         sd.reductionMode = (MTLSamplerReductionMode)mode;
         sd.supportArgumentBuffers = YES;
         id<MTLSamplerState> ss = [dev newSamplerStateWithDescriptor:sd];
         id<MTLBuffer> out = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
         id<MTLCommandBuffer> cb = [q commandBuffer];
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         [ce setComputePipelineState:ps]; [ce setTexture:t atIndex:0]; [ce setSamplerState:ss atIndex:0]; [ce setBuffer:out offset:0 atIndex:0];
         [ce dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
         [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
         printf("mode %d -> %f (expect avg 0.5, min 0, max 1) err=%s\n", mode, ((float *)out.contents)[0], cb.error ? "yes" : "none");
      }
   }
   return 0;
}
