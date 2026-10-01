#import <Metal/Metal.h>
#include <stdio.h>
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(texture_buffer<uint> t [[texture(0)]], texture_buffer<uint, access::read_write> w [[texture(1)]], device uint *out [[buffer(0)]]) {\n"
   "  w.write(uint4(42), uint(16384 * 2 + 5));\n" /* page 2: unmapped, write dropped */
   "  out[0] = t.read(uint(5)).x; out[1] = t.read(uint(16384 + 5)).x; out[2] = t.read(uint(16384 * 2 + 5)).x; }\n";
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.description.UTF8String); return 1; }
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
      const NSUInteger page = 65536;
      id<MTLBuffer> sparse = [dev newBufferWithLength:4 * page options:MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeUntracked
                              placementSparsePageSize:MTLSparsePageSize64];
      MTLTextureDescriptor *d = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                 width:4 * page / 4 resourceOptions:sparse.resourceOptions usage:MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite];
      d.placementSparsePageSize = MTLSparsePageSize64;
      id<MTLTexture> view = [sparse newTextureWithDescriptor:d offset:0 bytesPerRow:4 * page];
      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 1 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      MTL4UpdateSparseBufferMappingOperation op = {.mode = MTLSparseTextureMappingModeMap, .bufferRange = NSMakeRange(0, 2), .heapOffset = 0};
      [q updateBufferMappings:sparse heap:heap operations:&op count:1];
      id<MTLSharedEvent> ev = [dev newSharedEvent]; [q signalEvent:ev value:1];
      id<MTLCommandQueue> q3 = [dev newCommandQueue];
      id<MTLBuffer> out = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
      memset(out.contents, 0xee, 16);
      id<MTLCommandBuffer> cb = [q3 commandBuffer];
      [cb encodeWaitForEvent:ev value:1];
      id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
      [bl fillBuffer:sparse range:NSMakeRange(0, 4 * page) value:7];
      [bl endEncoding];
      id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
      [ce setComputePipelineState:ps]; [ce setTexture:view atIndex:0]; [ce setTexture:view atIndex:1]; [ce setBuffer:out offset:0 atIndex:0];
      [ce dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
      [ce endEncoding];
      id<MTLBuffer> cpy = [dev newBufferWithLength:4 * page options:MTLResourceStorageModeShared];
      id<MTLBlitCommandEncoder> b2 = [cb blitCommandEncoder];
      [b2 copyFromBuffer:sparse sourceOffset:0 toBuffer:cpy destinationOffset:0 size:4 * page];
      [b2 endEncoding];
      [cb commit]; [cb waitUntilCompleted];
      const uint32_t *c = cpy.contents;
      printf("blit copy: page0=%08x page1=%08x page2=%08x\n", c[5], c[16384 + 5], c[2 * 16384 + 5]);
      uint32_t *o = out.contents;
      printf("page0=%08x page1=%08x page2(unmapped)=%08x err=%s\n", o[0], o[1], o[2], cb.error ? "yes" : "none");
   }
   return 0;
}
