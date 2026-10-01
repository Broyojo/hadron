#import <Metal/Metal.h>
#include <stdio.h>

int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      printf("device %s supportsPlacementSparse=%d tile64=%lu\n", dev.name.UTF8String,
             (int)dev.supportsPlacementSparse,
             (unsigned long)[dev sparseTileSizeInBytesForSparsePageSize:MTLSparsePageSize64]);
      MTLSize ts = [dev sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:MTLPixelFormatRGBA8Unorm
                                          sampleCount:1 sparsePageSize:MTLSparsePageSize64];
      printf("rgba8 2D tile %lux%lux%lu\n", ts.width, ts.height, ts.depth);

      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement;
      hd.storageMode = MTLStorageModePrivate;
      hd.size = 4 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
      printf("heap %p\n", heap);

      const NSUInteger page = 65536;
      id<MTLBuffer> sparse = [dev newBufferWithLength:4 * page options:MTLResourceStorageModePrivate
                              placementSparsePageSize:MTLSparsePageSize64];
      printf("sparse buffer %p tier=%ld gpu=%llx\n", sparse, (long)sparse.sparseBufferTier, sparse.gpuAddress);

      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      MTL4UpdateSparseBufferMappingOperation op = {
         .mode = MTLSparseTextureMappingModeMap, .bufferRange = NSMakeRange(1, 1), .heapOffset = 2};
      if (!getenv("NOMAP"))
         [q updateBufferMappings:sparse heap:heap operations:&op count:1];

      id<MTLCommandQueue> q3 = [dev newCommandQueue];
      id<MTLBuffer> out = [dev newBufferWithLength:4 * page options:MTLResourceStorageModeShared];
      memset(out.contents, 0xab, 4 * page);
      if (getenv("NOMAP") == NULL) {
         MTLSharedEventHandle *unused = nil; (void)unused;
      }
      id<MTLSharedEvent> ev = [dev newSharedEvent];
      [q signalEvent:ev value:1];
      id<MTLCommandBuffer> cb0 = [q3 commandBuffer];
      [cb0 encodeWaitForEvent:ev value:1];
      id<MTLBlitCommandEncoder> bl0 = [cb0 blitCommandEncoder];
      [bl0 fillBuffer:sparse range:NSMakeRange(0, 4 * page) value:0x5a];
      [bl0 endEncoding];
      [cb0 commit];
      [cb0 waitUntilCompleted];
      id<MTLCommandBuffer> cb = [q3 commandBuffer];
      id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
      [bl copyFromBuffer:sparse sourceOffset:0 toBuffer:out destinationOffset:0 size:4 * page];
      [bl endEncoding];
      [cb commit];
      [cb waitUntilCompleted];
      const unsigned char *o = out.contents;
      printf("page0=%02x page1=%02x page2=%02x page3=%02x err=%s\n", o[0], o[page], o[2 * page], o[3 * page],
             cb.error ? cb.error.description.UTF8String : "none");
   }
   return 0;
}
