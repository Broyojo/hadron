#import <Metal/Metal.h>
#include <stdio.h>
/* A read-only sparse texture as a stand-in for the residency of a writable one.
 *  - Where the mip tail starts and how large it is, for read-only and writable textures of the
 *    same shape: the writable one starts its tail at the same level or one level later.
 *  - Several tiles of the read-only texture, its tail, and a tile of the writable texture can all
 *    be mapped to one heap page; the read-only texture then reports the residency of its own
 *    mappings. (Its values are not those written through the other texture.) */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void rd(texture2d<uint> twin [[texture(0)]], texture2d<uint, access::read_write> real [[texture(1)]], device uint *out [[buffer(0)]]) {\n"
   "  real.write(uint4(0x1234), uint2(5, 5));\n"
   "  atomic_thread_fence(mem_flags::mem_texture, memory_order_seq_cst, thread_scope::thread_scope_device);\n"
   "  for (uint ty = 0; ty < 2; ty++) for (uint tx = 0; tx < 4; tx++) { auto s = twin.sparse_read(uint2(tx * 128 + 5, ty * 128 + 5), 0); out[ty * 4 + tx] = s.resident(); out[8 + ty * 4 + tx] = s.value().x; }\n"
   "  for (uint l = 0; l < 10; l++) out[16 + l] = twin.sparse_read(uint2(0), l).resident();\n"
   "  out[30] = real.read(uint2(5, 5)).x;\n"
   "  out[31] = 77;\n}\n";
static id<MTLTexture> mk(id<MTLDevice> dev, MTLPixelFormat f, unsigned w, unsigned h, int write, int mips)
{
   MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:mips];
   d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView | (write ? MTLTextureUsageShaderWrite : 0);
   d.placementSparsePageSize = MTLSparsePageSize64;
   return [dev newTextureWithDescriptor:d];
}
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.localizedDescription.UTF8String); return 1; }
      id<MTLComputePipelineState> rd = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rd"] error:&err];
      static const unsigned sz[][2] = {{11,37},{64,64},{128,128},{129,129},{256,128},{512,256},{503,137},{1000,1000},{16384,768},{4096,4096},{1,4096}};
      static const struct { MTLPixelFormat f; const char *n; } fmts[] = {{MTLPixelFormatR32Uint, "r32"}, {MTLPixelFormatR8Uint, "r8"}, {MTLPixelFormatRGBA32Uint, "rgba32"}, {MTLPixelFormatRGBA16Uint, "rgba16"}};
      printf("tail start / tail bytes, read-only vs writable (mips | no mips)\n");
      for (unsigned f = 0; f < 4; f++) for (unsigned i = 0; i < sizeof(sz)/sizeof(sz[0]); i++) {
         MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:fmts[f].f sampleCount:1 sparsePageSize:MTLSparsePageSize64];
         id<MTLTexture> a = mk(dev, fmts[f].f, sz[i][0], sz[i][1], 0, 1), b = mk(dev, fmts[f].f, sz[i][0], sz[i][1], 1, 1);
         id<MTLTexture> c = mk(dev, fmts[f].f, sz[i][0], sz[i][1], 0, 0), d = mk(dev, fmts[f].f, sz[i][0], sz[i][1], 1, 0);
         printf("%-6s tile %3lux%-3lu %5ux%-5u ro %lu/%-7lu w %lu/%-7lu | ro %lu/%-6lu w %lu/%-6lu\n", fmts[f].n, tile.width, tile.height, sz[i][0], sz[i][1],
                a.firstMipmapInTail, a.tailSizeInBytes, b.firstMipmapInTail, b.tailSizeInBytes, c.firstMipmapInTail, c.tailSizeInBytes, d.firstMipmapInTail, d.tailSizeInBytes);
      }
      id<MTLTexture> twin = mk(dev, MTLPixelFormatR32Uint, 512, 256, 0, 1), real = mk(dev, MTLPixelFormatR32Uint, 512, 256, 1, 1);
      MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
      hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 4 << 20;
      hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
      id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
      /* twin: three level 0 tiles and the tail all on heap page 0; real: tile (0,0) on heap page 0 too */
      MTL4UpdateSparseTextureMappingOperation ops[4] = {
         {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = 0, .textureSlice = 0, .heapOffset = 0},
         {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(1, 0, 0, 1, 1, 1), .textureLevel = 0, .textureSlice = 0, .heapOffset = 0},
         {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(2, 1, 0, 1, 1, 1), .textureLevel = 0, .textureSlice = 0, .heapOffset = 0},
         {.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = twin.firstMipmapInTail, .textureSlice = 0, .heapOffset = 0}};
      [q updateTextureMappings:twin heap:heap operations:ops count:4];
      [q updateTextureMappings:real heap:heap operations:ops count:1];
      id<MTLSharedEvent> ev = [dev newSharedEvent]; [q signalEvent:ev value:1];
      id<MTLBuffer> out = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
      memset(out.contents, 0xee, 256);
      id<MTLCommandQueue> q3 = [dev newCommandQueue];
      id<MTLCommandBuffer> cb = [q3 commandBuffer];
      [cb encodeWaitForEvent:ev value:1];
      id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
      [ce setComputePipelineState:rd]; [ce setTexture:twin atIndex:0]; [ce setTexture:real atIndex:1]; [ce setBuffer:out offset:0 atIndex:0];
      [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
      [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
      const uint32_t *o = out.contents;
      printf("aliased mappings: twin level 0 tiles resident:"); for (int i = 0; i < 8; i++) printf(" %u", o[i]);
      printf(" (expect 1 1 0 0 0 0 1 0)\n   twin values:"); for (int i = 0; i < 8; i++) printf(" %x", o[8 + i]);
      printf("\n   twin by level:"); for (int i = 0; i < 10; i++) printf(" %u", o[16 + i]);
      printf("\n   real readback %x %s%s\n", o[30], o[31] == 77 ? "" : "KERNEL DID NOT FINISH", cb.error ? cb.error.localizedDescription.UTF8String : "");
   }
   return 0;
}
