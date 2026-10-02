#import <Metal/Metal.h>
#include <stdio.h>
/* sparse_read on writable (tier 1) sparse textures of various sizes, with the mip tail mapped.
 * The kernel reads the residency of every level through the whole texture.
 *
 * On macOS 27.0.1 (M2 Pro) the command buffer ends with a GPU address fault for some sizes
 * (128x1, 129x129, 64x128, 130x130, 257x257, 513x513, 11x37, 64x64) and not for others (127x128,
 * 128x128, 200x200, 512x512, 16384x768); plain read and write never fault, and read-only (tier 2)
 * textures of the same sizes never fault. NOMIPS=1 creates the textures without mips. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void rd(texture2d<uint> full [[texture(0)]], texture2d<uint, access::read_write> v0 [[texture(1)]], device uint *out [[buffer(0)]], constant uint *p [[buffer(1)]]) {\n"
   "  uint ops = p[1];\n"
   "  if (ops & 1) for (uint l = 0; l < p[0]; l++) { auto s = full.sparse_read(uint2(0), l); out[l] = s.resident(); }\n"
   "  if (ops & 2) { auto s = v0.sparse_read(uint2(0)); out[32] = s.resident(); }\n"
   "  if (ops & 4) out[34] = v0.read(uint2(0)).x;\n"
   "  if (ops & 8) out[35] = full.read(uint2(0), 0).x;\n"
   "  if (ops & 16) v0.write(uint4(0xabc), uint2(0));\n"
   "  if (ops & 32) { auto s = full.sparse_read(uint2(0), 0); out[36] = s.resident(); }\n"
   "  out[63] = 77;\n}\n";
static void run(id<MTLDevice> dev, id<MTLLibrary> lib, unsigned w, unsigned h, int write, int map, unsigned ops)
{
   NSError *err = nil;
   id<MTLComputePipelineState> rd = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rd"] error:&err];
   MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:w height:h mipmapped:(getenv("NOMIPS") ? NO : YES)];
   d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView | (write ? MTLTextureUsageShaderWrite : 0);
   d.placementSparsePageSize = MTLSparsePageSize64;
   id<MTLTexture> tex = [dev newTextureWithDescriptor:d];
   MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
   hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 4 << 20;
   hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
   id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
   id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
   if (map) {
      MTL4UpdateSparseTextureMappingOperation op = {.mode = MTLSparseTextureMappingModeMap,
         .textureRegion = MTLRegionMake3D(0, 0, 0, (tex.tailSizeInBytes + 16383) / 16384, 1, 1), .textureLevel = tex.firstMipmapInTail, .textureSlice = 0, .heapOffset = 0};
      [q updateTextureMappings:tex heap:heap operations:&op count:1];
   }
   id<MTLSharedEvent> ev = [dev newSharedEvent]; [q signalEvent:ev value:1];
   id<MTLTexture> v0 = write ? [tex newTextureViewWithPixelFormat:MTLPixelFormatR32Uint textureType:MTLTextureType2D levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)] : nil;
   id<MTLBuffer> out = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
   memset(out.contents, 0xee, 256);
   id<MTLBuffer> pb = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
   ((uint32_t *)pb.contents)[0] = (uint32_t)tex.mipmapLevelCount; ((uint32_t *)pb.contents)[1] = ops;
   id<MTLCommandQueue> q3 = [dev newCommandQueue];
   id<MTLCommandBuffer> cb = [q3 commandBuffer];
   [cb encodeWaitForEvent:ev value:1];
   id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
   [ce setComputePipelineState:rd]; [ce setTexture:tex atIndex:0]; [ce setTexture:v0 atIndex:1];
   [ce setBuffer:out offset:0 atIndex:0]; [ce setBuffer:pb offset:0 atIndex:1];
   [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
   [ce endEncoding];
   [cb commit]; [cb waitUntilCompleted];
   const uint32_t *o = out.contents;
   printf("%3ux%-3u %s %s tier %ld tail from %lu (%lu B) ops %2u: %s", w, h, write ? "write" : "read ", map ? "tail mapped" : "unmapped   ",
          (long)tex.sparseTextureTier, tex.firstMipmapInTail, tex.tailSizeInBytes, ops, cb.error ? "PAGE FAULT" : "ok");
   if (!cb.error && (ops & 1)) { printf("  resident by level:"); for (unsigned l = 0; l < tex.mipmapLevelCount; l++) printf(" %u", o[l]); }
   if (!cb.error && (ops & 34)) printf("  view %x whole-l0 %x", o[32], o[36]);
   printf("\n");
}
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.localizedDescription.UTF8String); return 1; }
      static const unsigned sz[][2] = {{128,1},{129,129},{127,128},{128,128},{300,200},{129,129},{64,128},{256,127},{130,130},{200,200},{255,255},{257,257},{512,512},{513,513},{1000,1000},{16384,768}};
      for (unsigned i = 0; i < sizeof(sz)/sizeof(sz[0]); i++) run(dev, lib, sz[i][0], sz[i][1], 1, 1, 33);
   }
   return 0;
}
