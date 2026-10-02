#import <Metal/Metal.h>
#include <stdio.h>
/* Residency that sparse_read reports through texture views, on a read-only sparse texture (tier 2)
 * and a writable one (tier 1). 512x256 R32Uint array of 3 slices with mips. Mapped: level 0 tile
 * (0,0) of slice 0, level 1 tile (1,0) of slice 0, level 1 tile (0,0) of slice 2, level 2 tile (0,0)
 * of slice 1. Level 2 is one tile wide, so its second column reads outside the level.
 *
 * On macOS 27.0.1 the tier 1 texture ignores a view's first level: a view of levels 1-2 reports the
 * residency of levels 0-1. Slice offsets are honoured, views that start at level 0 are right, and
 * so is every view of the tier 2 texture. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void rd(texture2d_array<uint> full [[texture(0)]], texture2d_array<uint> a [[texture(1)]], texture2d_array<uint> b [[texture(2)]],\n"
   "               texture2d_array<uint> c [[texture(3)]], texture2d_array<uint> d [[texture(4)]], texture2d_array<uint, access::read_write> e [[texture(5)]],\n"
   "               device uint *out [[buffer(0)]]) {\n"
   "  uint n = 0;\n"
   "  for (uint s = 0; s < 3; s++) for (uint l = 0; l < 3; l++) for (uint tx = 0; tx < 2; tx++) out[n++] = full.sparse_read(uint2(tx * 128 + 5, 5), s, l).resident();\n"
   "  n = 32; /* a: levels 1..2, all slices */\n"
   "  for (uint s = 0; s < 3; s++) for (uint l = 0; l < 2; l++) for (uint tx = 0; tx < 2; tx++) out[n++] = a.sparse_read(uint2(tx * 128 + 5, 5), s, l).resident();\n"
   "  n = 64; /* b: levels 0..1, slices 1..2 */\n"
   "  for (uint s = 0; s < 2; s++) for (uint l = 0; l < 2; l++) for (uint tx = 0; tx < 2; tx++) out[n++] = b.sparse_read(uint2(tx * 128 + 5, 5), s, l).resident();\n"
   "  n = 96; /* c: level 1, slice 2 */\n"
   "  for (uint tx = 0; tx < 2; tx++) out[n++] = c.sparse_read(uint2(tx * 128 + 5, 5), 0, 0).resident();\n"
   "  n = 100; /* d: levels 0..2, slice 2 */\n"
   "  for (uint l = 0; l < 3; l++) for (uint tx = 0; tx < 2; tx++) out[n++] = d.sparse_read(uint2(tx * 128 + 5, 5), 0, l).resident();\n"
   "  n = 110; /* e: read_write binding of d */\n"
   "  for (uint l = 0; l < 3; l++) for (uint tx = 0; tx < 2; tx++) out[n++] = e.sparse_read(uint2(tx * 128 + 5, 5), 0, l).resident();\n"
   "  out[127] = 77;\n}\n";
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.localizedDescription.UTF8String); return 1; }
      id<MTLComputePipelineState> rd = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rd"] error:&err];
      for (int write = 0; write < 2; write++) {
         MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:512 height:256 mipmapped:YES];
         d.textureType = MTLTextureType2DArray; d.arrayLength = 3;
         d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView | (write ? MTLTextureUsageShaderWrite : 0);
         d.placementSparsePageSize = MTLSparsePageSize64;
         id<MTLTexture> tex = [dev newTextureWithDescriptor:d];
         printf("%s: tier %ld, %lu levels, tail from %lu\n", write ? "write" : "read-only", (long)tex.sparseTextureTier, tex.mipmapLevelCount, tex.firstMipmapInTail);
         MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
         hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 4 << 20;
         hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
         id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
         id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
         MTL4UpdateSparseTextureMappingOperation ops[4] = {
            {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = 0, .textureSlice = 0, .heapOffset = 0},
            {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(1, 0, 0, 1, 1, 1), .textureLevel = 1, .textureSlice = 0, .heapOffset = 1},
            {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = 1, .textureSlice = 2, .heapOffset = 2},
            {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = 2, .textureSlice = 1, .heapOffset = 3}};
         [q updateTextureMappings:tex heap:heap operations:ops count:4];
         id<MTLSharedEvent> ev = [dev newSharedEvent]; [q signalEvent:ev value:1];
         MTLPixelFormat f = MTLPixelFormatR32Uint; MTLTextureType ty = MTLTextureType2DArray;
         id<MTLTexture> a = [tex newTextureViewWithPixelFormat:f textureType:ty levels:NSMakeRange(1, 2) slices:NSMakeRange(0, 3)];
         id<MTLTexture> b = [tex newTextureViewWithPixelFormat:f textureType:ty levels:NSMakeRange(0, 2) slices:NSMakeRange(1, 2)];
         id<MTLTexture> c = [tex newTextureViewWithPixelFormat:f textureType:ty levels:NSMakeRange(1, 1) slices:NSMakeRange(2, 1)];
         id<MTLTexture> dd = [tex newTextureViewWithPixelFormat:f textureType:ty levels:NSMakeRange(0, 3) slices:NSMakeRange(2, 1)];
         id<MTLBuffer> out = [dev newBufferWithLength:512 options:MTLResourceStorageModeShared];
         memset(out.contents, 0xee, 512);
         id<MTLCommandQueue> q3 = [dev newCommandQueue];
         id<MTLCommandBuffer> cb = [q3 commandBuffer];
         [cb encodeWaitForEvent:ev value:1];
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         [ce setComputePipelineState:rd]; [ce setTexture:tex atIndex:0]; [ce setTexture:a atIndex:1]; [ce setTexture:b atIndex:2];
         [ce setTexture:c atIndex:3]; [ce setTexture:dd atIndex:4]; [ce setTexture:write ? dd : nil atIndex:5];
         [ce setBuffer:out offset:0 atIndex:0];
         [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
         [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
         const uint32_t *o = out.contents;
         printf("  whole (slice x level x tile):   "); for (int i = 0; i < 18; i++) printf("%u%s", o[i], i % 6 == 5 ? " | " : i % 2 ? " " : "");
         printf("\n  expect                          10 01 00 | 00 00 10 | 00 10 00 |");
         printf("\n  a levels 1-2, all slices:       "); for (int i = 0; i < 12; i++) printf("%u%s", o[32 + i], i % 4 == 3 ? " | " : i % 2 ? " " : "");
         printf("\n  expect                          01 00 | 00 10 | 10 00 |");
         printf("\n  b levels 0-1, slices 1-2:       "); for (int i = 0; i < 8; i++) printf("%u%s", o[64 + i], i % 4 == 3 ? " | " : i % 2 ? " " : "");
         printf("\n  expect                          00 00 | 00 10 |");
         printf("\n  c level 1, slice 2:             %u%u   expect 10", o[96], o[97]);
         printf("\n  d levels 0-2, slice 2:          "); for (int i = 0; i < 6; i++) printf("%u%s", o[100 + i], i % 2 ? " " : "");
         printf("  expect 00 10 00");
         if (write) { printf("\n  e same view bound read_write:   "); for (int i = 0; i < 6; i++) printf("%u%s", o[110 + i], i % 2 ? " " : ""); }
         printf("\n  %s%s\n", o[127] == 77 ? "kernel finished" : "KERNEL DID NOT FINISH", cb.error ? " ERROR" : "");
      }
   }
   return 0;
}
