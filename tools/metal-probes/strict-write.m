#import <Metal/Metal.h>
#include <stdio.h>
/* What happens to a shader write to an unmapped page of a placement sparse buffer: it is not
 * discarded. The value reads back at that address (not at other holes or other buffers) for the
 * rest of the command buffer, and is gone in the next one. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(constant ulong *addr [[buffer(0)]], device uint *out [[buffer(1)]], uint tid [[thread_position_in_grid]]) {\n"
   "  device uint *p = (device uint *)addr[0];\n"
   "  volatile device uint *v = (volatile device uint *)addr[0];\n"
   "  volatile device uint *w = (volatile device uint *)addr[1];\n"
   "  uint mode = (uint)addr[2];\n"
   "  if (mode == 0) { v[16384 + 200] = 0xABu; }\n"
   "  if (mode == 1) { out[0] = v[16384 + 200]; out[1] = v[3 * 16384 + 200]; out[2] = w[16384 + 200]; out[3] = w[3 * 16384 + 200]; } }\n";
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
      const NSUInteger page = 65536;
      id<MTLBuffer> sparse = [dev newBufferWithLength:4 * page options:MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeUntracked
                              placementSparsePageSize:MTLSparsePageSize64];
      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement; hd.storageMode = getenv("SHARED") ? MTLStorageModeShared : MTLStorageModePrivate; hd.size = 1 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      MTL4UpdateSparseBufferMappingOperation ops[2] = {
         {.mode = MTLSparseTextureMappingModeMap, .bufferRange = NSMakeRange(0, 1), .heapOffset = 0},
         {.mode = MTLSparseTextureMappingModeMap, .bufferRange = NSMakeRange(2, 1), .heapOffset = 1}};
      [q updateBufferMappings:sparse heap:heap operations:ops count:2];
      id<MTLSharedEvent> ev = [dev newSharedEvent]; [q signalEvent:ev value:1];
      id<MTLCommandQueue> q3 = [dev newCommandQueue];
      id<MTLCommandBuffer> c0 = [q3 commandBuffer];
      [c0 encodeWaitForEvent:ev value:1];
      id<MTLBlitCommandEncoder> bl = [c0 blitCommandEncoder];
      [bl fillBuffer:sparse range:NSMakeRange(0, page) value:1];
      [bl fillBuffer:sparse range:NSMakeRange(2 * page, page) value:3];
      [bl endEncoding]; [c0 commit]; [c0 waitUntilCompleted];
      id<MTLBuffer> other = [dev newBufferWithLength:4 * page options:MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeUntracked
                              placementSparsePageSize:MTLSparsePageSize64];
      id<MTLBuffer> out = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
      for (int pass = 0; pass < 3; pass++) {
         memset(out.contents, 0xee, 256);
         id<MTLCommandBuffer> cb = [q3 commandBuffer];
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         [ce setComputePipelineState:ps]; [ce setBuffer:out offset:0 atIndex:1];
         [ce useResource:sparse usage:MTLResourceUsageRead | MTLResourceUsageWrite];
         [ce useResource:other usage:MTLResourceUsageRead | MTLResourceUsageWrite];
         for (int m = (pass == 0 ? 0 : 1); m < 2; m++) {
            id<MTLBuffer> addr = [dev newBufferWithLength:24 options:MTLResourceStorageModeShared];
            uint64_t *a = addr.contents; a[0] = sparse.gpuAddress; a[1] = other.gpuAddress; a[2] = m;
            [ce setBuffer:addr offset:0 atIndex:0];
            [ce dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
         }
         [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
         if (pass == 1) sleep(2);
         uint32_t *o = out.contents;
         printf("cmdbuf %d: hole1=%08x hole3 same offset=%08x other buffer hole1=%08x other hole3=%08x err=%s\n", pass, o[0], o[1], o[2], o[3], cb.error ? "yes" : "none");
      }
   }
   return 0;
}
