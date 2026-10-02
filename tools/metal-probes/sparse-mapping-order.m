#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Which orders of MTL4CommandQueue updateTextureMappings calls on two sparse textures crash.
 * A is writable (tier 1), B read-only (tier 2). Arguments are operations in order:
 *   <A|B>,<level or -1 for the tail>,<tile x>,<width in tiles>,<heap page>
 *   s: signal an event, w: signal and wait, k: commit a command buffer with barriers,
 *   c: commit an MTLCommandBuffer that waits for the signal
 *
 * On macOS 27.0.1 mapping B, then A, then B again without anything between them crashes in
 * -[AGXG14XFamilyComputeContext_mtlnext updateTextureMappings:heap:operations:count:]:
 *   B,0,0,1,3 A,0,0,1,0 B,0,1,1,5      crashes
 *   B,0,0,1,3 A,0,0,1,0 s B,0,1,1,5    fine (so are w, k and c)
 *   A,0,0,1,0 A,0,1,1,1 B,0,0,1,3 B,0,1,1,5   fine
 * A, B, A is fine, and so is any order when both textures are read-only. */
static id<MTLTexture> mk(id<MTLDevice> dev, int write)
{
   MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:512 height:256 mipmapped:YES];
   d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead | (write ? MTLTextureUsageShaderWrite : 0);
   d.placementSparsePageSize = MTLSparsePageSize64;
   return [dev newTextureWithDescriptor:d];
}
int main(int argc, char **argv)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLTexture> t[2] = {mk(dev, 1), mk(dev, 0)};
      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 4 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      id<MTLSharedEvent> ev = [dev newSharedEvent];
      unsigned value = 0;
      /* ops: <A|B>,<level or -1 for the tail>,<tile x>,<width in tiles>,<heap page>; A is writable, B read-only */
      for (int i = 1; i < argc; i++) {
         char c; int level, x, w, page;
         if (argv[i][0] == 'w') { [q signalEvent:ev value:++value]; while (ev.signaledValue < value) usleep(1000); printf("w "); continue; }
         if (argv[i][0] == 's') { [q signalEvent:ev value:++value]; printf("s "); continue; }
         if (argv[i][0] == 'k') {
            id<MTL4CommandAllocator> allocator = [dev newCommandAllocator];
            id<MTL4CommandBuffer> cmd_buf = [dev newCommandBuffer];
            [cmd_buf beginCommandBufferWithAllocator:allocator];
            id<MTL4ComputeCommandEncoder> enc = [cmd_buf computeCommandEncoder];
            [enc barrierAfterQueueStages:MTLStageResourceState beforeStages:MTLStageDispatch | MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
            [enc barrierAfterStages:MTLStageDispatch | MTLStageBlit beforeQueueStages:MTLStageVertex | MTLStageFragment | MTLStageDispatch | MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
            [enc endEncoding]; [cmd_buf endCommandBuffer];
            [q commit:&cmd_buf count:1];
            printf("k "); continue;
         }
         if (argv[i][0] == 'c') { id<MTLCommandQueue> q3 = [dev newCommandQueue]; id<MTLCommandBuffer> cb = [q3 commandBuffer]; [q signalEvent:ev value:++value]; [cb encodeWaitForEvent:ev value:value]; [cb commit]; [cb waitUntilCompleted]; printf("c "); continue; }
         sscanf(argv[i], "%c,%d,%d,%d,%d", &c, &level, &x, &w, &page);
         id<MTLTexture> tex = t[c == 'A' ? 0 : 1];
         MTL4UpdateSparseTextureMappingOperation op = {.mode = MTLSparseTextureMappingModeMap,
            .textureRegion = MTLRegionMake3D(x, 0, 0, w, 1, 1), .textureLevel = level < 0 ? tex.firstMipmapInTail : level, .textureSlice = 0, .heapOffset = page};
         printf("%s ", argv[i]); fflush(stdout);
         [q updateTextureMappings:tex heap:heap operations:&op count:1];
      }
      [q signalEvent:ev value:++value];
      while (ev.signaledValue < value) usleep(1000);
      printf("ok\n");
   }
   return 0;
}
