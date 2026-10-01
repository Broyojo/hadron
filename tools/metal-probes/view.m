#import <Metal/Metal.h>
#include <stdio.h>

static const char *src =
   "#include <metal_stdlib>\n"
   "using namespace metal;\n"
   "kernel void k(texture2d<uint, access::read> full [[texture(0)]],\n"
   "              texture2d<uint, access::read> view1 [[texture(1)]],\n"
   "              texture2d<uint, access::sample> sfull [[texture(2)]],\n"
   "              texture2d<uint, access::sample> sview1 [[texture(3)]],\n"
   "              device uint *out [[buffer(0)]], constant ulong *ids [[buffer(1)]]) {\n"
   "  constexpr sampler s(filter::nearest, mip_filter::nearest);\n"
   "  out[0] = full.sparse_read(uint2(0, 0), 0).resident();\n"
   "  out[1] = full.sparse_read(uint2(0, 0), 1).resident();\n"
   "  out[2] = view1.sparse_read(uint2(0, 0), 0).resident();\n"
   "  out[3] = sfull.sparse_sample(s, float2(0.1, 0.1), level(1)).resident();\n"
   "  out[4] = sview1.sparse_sample(s, float2(0.1, 0.1), level(0)).resident();\n"
   "  out[5] = sview1.sparse_read(uint2(0, 0), 0).resident();\n"
   "  texture2d<uint> bv = *(constant texture2d<uint>*)&ids[0];\n"
   "  out[6] = bv.sparse_read(uint2(0, 0), 0).resident();\n"
   "  texture2d<uint> bf = *(constant texture2d<uint>*)&ids[1];\n"
   "  out[7] = bf.sparse_read(uint2(0, 0), 1).resident();\n"
   "}\n";

int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("compile: %s\n", err.description.UTF8String); return 1; }
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];

      MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                 width:512 height:256 mipmapped:YES];
      d.storageMode = MTLStorageModePrivate;
      d.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
      d.placementSparsePageSize = MTLSparsePageSize64;
      if (getenv("ARR")) { d.textureType = MTLTextureType2DArray; d.arrayLength = 1; }
      id<MTLTexture> tex = [dev newTextureWithDescriptor:d];
      printf("tail first %lu size %lu\n", (unsigned long)tex.firstMipmapInTail, (unsigned long)tex.tailSizeInBytes);

      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 4 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];

      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      MTL4UpdateSparseTextureMappingOperation op = {
         .mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake2D(0, 0, 4, 2),
         .textureLevel = 0, .textureSlice = 0, .heapOffset = 0};
      [q updateTextureMappings:tex heap:heap operations:&op count:1];
      id<MTLSharedEvent> ev = [dev newSharedEvent];
      [q signalEvent:ev value:1];

      id<MTLTexture> view1 = getenv("SWZ") ?
         [tex newTextureViewWithPixelFormat:MTLPixelFormatR32Uint textureType:MTLTextureType2D
                              levels:NSMakeRange(1, 1) slices:NSMakeRange(0, 1)
                             swizzle:MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleZero, MTLTextureSwizzleZero, MTLTextureSwizzleOne)] :
         [tex newTextureViewWithPixelFormat:MTLPixelFormatR32Uint textureType:MTLTextureType2D
                              levels:NSMakeRange(1, 1) slices:NSMakeRange(0, 1)];
      id<MTLBuffer> out = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
      memset(out.contents, 0xff, 64);

      id<MTLCommandQueue> q3 = [dev newCommandQueue];
      id<MTLCommandBuffer> cb = [q3 commandBuffer];
      [cb encodeWaitForEvent:ev value:1];
      id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
      [ce setComputePipelineState:ps];
      [ce setTexture:tex atIndex:0]; [ce setTexture:view1 atIndex:1];
      [ce setTexture:tex atIndex:2]; [ce setTexture:view1 atIndex:3];
      [ce setBuffer:out offset:0 atIndex:0];
      id<MTLBuffer> ids = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
      ((MTLResourceID *)ids.contents)[0] = view1.gpuResourceID;
      ((MTLResourceID *)ids.contents)[1] = tex.gpuResourceID;
      [ce setBuffer:ids offset:0 atIndex:1];
      [ce useResource:view1 usage:MTLResourceUsageRead];
      [ce useResource:tex usage:MTLResourceUsageRead];
      [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
      [ce endEncoding];
      [cb commit];
      [cb waitUntilCompleted];
      uint32_t *o = out.contents;
      printf("full mip0 read=%u  full mip1 read=%u  view(mip1) read=%u  full mip1 sample=%u  view(mip1) sample=%u  view(mip1) sample-access read=%u bindless view=%u bindless full lod1=%u err=%s\n",
             o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], cb.error ? cb.error.description.UTF8String : "none");
   }
   return 0;
}
