#import <Metal/Metal.h>
#include <stdio.h>
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLBuffer> sparse = [dev newBufferWithLength:4 * 65536 options:MTLResourceStorageModePrivate
                              placementSparsePageSize:MTLSparsePageSize64];
      MTLTextureDescriptor *d = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                 width:1024 resourceOptions:MTLResourceStorageModePrivate usage:MTLTextureUsageShaderRead];
      id<MTLTexture> t = [sparse newTextureWithDescriptor:d offset:0 bytesPerRow:4096];
      printf("texture buffer view on sparse: %p\n", t);
      MTLTextureDescriptor *d2 = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:1024 height:4 mipmapped:NO];
      d2.storageMode = MTLStorageModePrivate;
      id<MTLTexture> t2 = [sparse newTextureWithDescriptor:d2 offset:0 bytesPerRow:4096];
      printf("2D linear on sparse: %p\n", t2);
   }
   return 0;
}
