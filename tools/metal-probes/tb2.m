#import <Metal/Metal.h>
#include <stdio.h>
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLBuffer> sparse = [dev newBufferWithLength:4 * 65536
                                              options:MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeUntracked
                              placementSparsePageSize:MTLSparsePageSize64];
      printf("buffer options=%lx\n", (unsigned long)sparse.resourceOptions);

      /* 1: texture buffer view, options matching the buffer */
      MTLTextureDescriptor *d1 = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                  width:65536 resourceOptions:sparse.resourceOptions usage:MTLTextureUsageShaderRead];
      printf("1 tb view, matching options: %p\n", [sparse newTextureWithDescriptor:d1 offset:0 bytesPerRow:4 * 65536]);

      /* 2: same, with the sparse page size on the descriptor */
      d1.placementSparsePageSize = MTLSparsePageSize64;
      printf("2 tb view, + sparse page size: %p\n", [sparse newTextureWithDescriptor:d1 offset:0 bytesPerRow:4 * 65536]);

      /* 3: a placement sparse texture of type texture buffer, from the device */
      MTLTextureDescriptor *d3 = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                  width:65536 resourceOptions:MTLResourceStorageModePrivate usage:MTLTextureUsageShaderRead];
      d3.placementSparsePageSize = MTLSparsePageSize64;
      id<MTLTexture> t3 = [dev newTextureWithDescriptor:d3];
      printf("3 sparse texture-buffer texture: %p tier=%ld\n", t3, t3 ? (long)t3.sparseTextureTier : -1);

      /* 4: placement texture-buffer texture on a sparse-compatible heap (non-sparse) */
      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 1 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
      id<MTLTexture> t4 = [heap newTextureWithDescriptor:d1 offset:0];
      printf("4 heap texture-buffer: %p\n", t4);
   }
   return 0;
}
