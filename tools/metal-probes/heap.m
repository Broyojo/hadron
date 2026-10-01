#import <Metal/Metal.h>
#include <stdio.h>
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      unsigned long sizes[] = {4096, 16384, 65536, 100000, 1 << 20};
      for (int s = 0; s < 2; s++) for (int i = 0; i < 5; i++) {
         MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
         hd.type = MTLHeapTypePlacement;
         hd.storageMode = s ? MTLStorageModeShared : MTLStorageModePrivate;
         hd.size = sizes[i];
         hd.sparsePageSize = MTLSparsePageSize16;
         hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
         id<MTLHeap> h = [dev newHeapWithDescriptor:hd];
         printf("%s size %lu -> %p actual %lu\n", s ? "shared" : "private", sizes[i], h, (unsigned long)h.size);
      }
   }
   return 0;
}
