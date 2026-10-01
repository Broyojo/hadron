#import <Metal/Metal.h>
#include <stdio.h>
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "struct Args { texture2d<float> t; sampler s; device float *out; };\n"
   "kernel void k(constant Args &a [[buffer(0)]]) {\n"
   "  a.out[0] = a.t.sample(a.s, float2(0.5, 0.5), level(0)).x; }\n";
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.description.UTF8String); return 1; }
      MTL4ComputePipelineDescriptor *pd = [MTL4ComputePipelineDescriptor new];
      MTL4LibraryFunctionDescriptor *fd = [MTL4LibraryFunctionDescriptor new];
      fd.library = lib; fd.name = @"k"; pd.computeFunctionDescriptor = fd;
      id<MTL4Compiler> comp = [dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&err];
      id<MTLComputePipelineState> ps = [comp newComputePipelineStateWithDescriptor:pd compilerTaskOptions:nil error:&err];
      if (!ps) { printf("pso: %s\n", err.description.UTF8String); return 1; }
      MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:4 height:4 mipmapped:YES];
      d.storageMode = MTLStorageModeShared;
      id<MTLTexture> t = [dev newTextureWithDescriptor:d];
      float px[16]; for (int i = 0; i < 16; i++) px[i] = (i % 2) ? 1.0f : 0.0f;
      [t replaceRegion:MTLRegionMake2D(0,0,4,4) mipmapLevel:0 withBytes:px bytesPerRow:16];
      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      id<MTLResidencySet> rs = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&err];
      for (int mode = 0; mode < 3; mode++) {
         MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
         sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; sd.mipFilter = MTLSamplerMipFilterLinear;
         sd.reductionMode = (MTLSamplerReductionMode)mode; sd.supportArgumentBuffers = YES;
         id<MTLSamplerState> ss = [dev newSamplerStateWithDescriptor:sd];
         id<MTLBuffer> out = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
         id<MTLBuffer> args = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
         uint64_t *a = args.contents;
         a[0] = t.gpuResourceID._impl; a[1] = ss.gpuResourceID._impl; a[2] = out.gpuAddress;
         [rs addAllocation:t]; [rs addAllocation:out]; [rs addAllocation:args]; [rs commit];
         id<MTL4CommandAllocator> al = [dev newCommandAllocator];
         id<MTL4CommandBuffer> cb = [dev newCommandBuffer];
         [cb beginCommandBufferWithAllocator:al];
         [cb useResidencySet:rs];
         MTL4ArgumentTableDescriptor *atd = [MTL4ArgumentTableDescriptor new]; atd.maxBufferBindCount = 1;
         id<MTL4ArgumentTable> at = [dev newArgumentTableWithDescriptor:atd error:&err];
         [at setAddress:args.gpuAddress atIndex:0];
         id<MTL4ComputeCommandEncoder> ce = [cb computeCommandEncoder];
         [ce setComputePipelineState:ps]; [ce setArgumentTable:at];
         [ce dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
         [ce endEncoding]; [cb endCommandBuffer];
         id<MTLSharedEvent> ev = [dev newSharedEvent];
         [q commit:&cb count:1]; [q signalEvent:ev value:1];
         [ev waitUntilSignaledValue:1 timeoutMS:5000];
         printf("MTL4 mode %d -> %.3f (avg 0.5, min 0, max 1)\n", mode, ((float *)out.contents)[0]);
      }
   }
   return 0;
}
