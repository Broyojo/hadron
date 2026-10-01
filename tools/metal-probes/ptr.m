#import <Metal/Metal.h>
#include <stdio.h>
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(constant ulong *addr [[buffer(0)]], device uint *out [[buffer(1)]]) {\n"
   "  device uint *p = (device uint *)addr[0];\n"
   "  out[0] = p[5]; out[1] = p[16384 + 5]; out[2] = p[2 * 16384 + 5]; out[3] = p[3 * 16384 + 5]; }\n";
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
      id<MTLBuffer> addr = [dev newBufferWithLength:8 options:MTLResourceStorageModeShared];
      *(uint64_t *)addr.contents = sparse.gpuAddress;
      id<MTLBuffer> out = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
      id<MTLCommandBuffer> cb = [q3 commandBuffer];
      id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
      [ce setComputePipelineState:ps]; [ce setBuffer:addr offset:0 atIndex:0]; [ce setBuffer:out offset:0 atIndex:1];
      [ce useResource:sparse usage:MTLResourceUsageRead];
      [ce dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
      [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
      uint32_t *o = out.contents;
      printf("page0=%08x hole1=%08x page2=%08x hole3=%08x err=%s\n", o[0], o[1], o[2], o[3], cb.error ? "yes" : "none");
   }
   return 0;
}
