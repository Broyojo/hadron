#import <Metal/Metal.h>
#include <stdio.h>
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(texture2d<uint> t [[texture(0)]], device uint *out [[buffer(0)]]) {\n"
   "  constexpr sampler sm(filter::nearest, mip_filter::nearest);\n"
   "  for (uint l = 0; l < 6; l++) { out[l] = t.sparse_read(uint2(0, 0), l).resident();\n"
   "    out[6 + l] = t.sparse_sample(sm, float2(0.2, 0.2), level(float(l))).resident(); }\n"
   "  out[12] = 77; }\n";
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.description.UTF8String); return 1; }
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
      MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:(getenv("W") ? atoi(getenv("W")) : 11) height:(getenv("H") ? atoi(getenv("H")) : 37) mipmapped:YES];
      d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead;
      if (getenv("RT")) d.usage |= MTLTextureUsageRenderTarget;
      if (getenv("SW")) d.usage |= MTLTextureUsageShaderWrite;
      if (getenv("PFV")) d.usage |= MTLTextureUsagePixelFormatView;
      if (getenv("OPT")) d.allowGPUOptimizedContents = YES;
      if (getenv("NOOPT")) d.allowGPUOptimizedContents = NO;
      d.placementSparsePageSize = MTLSparsePageSize64;
      if (getenv("ARR")) { d.textureType = MTLTextureType2DArray; d.arrayLength = 1; }
      id<MTLTexture> tex = [dev newTextureWithDescriptor:d];
      printf("levels %lu tail first %lu size %lu tier %ld\n", tex.mipmapLevelCount, tex.firstMipmapInTail, tex.tailSizeInBytes, (long)tex.sparseTextureTier);
      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 1 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      MTL4UpdateSparseTextureMappingOperation op = {.mode = MTLSparseTextureMappingModeMap,
         .textureRegion = MTLRegionMake3D(0, 0, 0, getenv("TW") ? atoi(getenv("TW")) : 1, 1, 1), .textureLevel = tex.firstMipmapInTail, .textureSlice = 0, .heapOffset = 0};
      [q updateTextureMappings:tex heap:heap operations:&op count:1];
      id<MTLSharedEvent> ev = [dev newSharedEvent];
      [q signalEvent:ev value:1];
      id<MTLBuffer> out = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
      memset(out.contents, 0xee, 64);
      id<MTLCommandQueue> q3 = [dev newCommandQueue];
      id<MTLCommandBuffer> cb = [q3 commandBuffer];
      [cb encodeWaitForEvent:ev value:1];
      id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
      uint32_t v = 1234;
      id<MTLBuffer> src = [dev newBufferWithBytes:&v length:4 options:MTLResourceStorageModeShared];
      [bl copyFromBuffer:src sourceOffset:0 sourceBytesPerRow:4 sourceBytesPerImage:4 sourceSize:MTLSizeMake(1,1,1)
               toTexture:tex destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(3,5,0)];
      [bl endEncoding];
      id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
      [ce setComputePipelineState:ps];
      id<MTLTexture> v0 = getenv("ARR") ? [tex newTextureViewWithPixelFormat:MTLPixelFormatR32Uint textureType:MTLTextureType2D levels:NSMakeRange(0,1) slices:NSMakeRange(0,1)] : tex;
      [ce setTexture:v0 atIndex:0]; [ce setBuffer:out offset:0 atIndex:0];
      [ce dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
      [ce endEncoding];
      [cb commit]; [cb waitUntilCompleted];
      uint32_t *o = out.contents;
      printf("read:"); for (int i = 0; i < 6; i++) printf(" %u", o[i]);
      printf(" sample:"); for (int i = 6; i < 12; i++) printf(" %u", o[i]);
      printf(" marker=%u err=%s\n", o[12], cb.error ? cb.error.description.UTF8String : "none");
   }
   return 0;
}
