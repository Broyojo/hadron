#import <Metal/Metal.h>
#include <stdio.h>
/* Sparse textures that shaders write to (D3D12 reserved resources with UAV access). For each
 * shape: the sparse tier Metal gives the texture, then with one tile of slice 0 mapped, what a
 * kernel sees when it writes and reads a texel in the mapped tile, in an unmapped tile of the same
 * slice and in the other slice, and the residency sparse_read reports for each mip level. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "kernel void k(texture2d_array<uint, access::read_write> t [[texture(0)]], texture2d_array<uint> r [[texture(1)]],\n"
   "              device uint *out [[buffer(0)]], constant uint *p [[buffer(1)]]) {\n"
   "  uint2 mapped = uint2(p[0], p[1]), unmapped = uint2(p[2], p[3]); uint levels = p[4], slices = p[5];\n"
   "  t.write(uint4(0x1111), mapped, 0, 0);\n"
   "  t.write(uint4(0x2222), unmapped, 0, 0);\n"
   "  if (slices > 1) t.write(uint4(0x3333), mapped, 1, 0);\n"
   "  atomic_thread_fence(mem_flags::mem_texture, memory_order_seq_cst, thread_scope::thread_scope_device);\n"
   "  out[0] = t.read(mapped, 0, 0).x; out[1] = t.read(unmapped, 0, 0).x; out[2] = slices > 1 ? t.read(mapped, 1, 0).x : 0;\n"
   "  out[3] = r.sparse_read(mapped, 0, 0).resident(); out[4] = r.sparse_read(unmapped, 0, 0).resident();\n"
   "  out[5] = slices > 1 ? uint(r.sparse_read(mapped, 1, 0).resident()) : 0u;\n"
   "  for (uint l = 0; l < levels && l < 16; l++) out[8 + l] = r.sparse_read(uint2(0), 0, l).resident();\n"
   "  out[30] = 77;\n}\n"
   "kernel void later(texture2d_array<uint> r [[texture(1)]], device uint *out [[buffer(0)]], constant uint *p [[buffer(1)]]) {\n"
   "  out[32] = r.read(uint2(p[0], p[1]), 0, 0).x; out[33] = r.read(uint2(p[2], p[3]), 0, 0).x; out[34] = r.read(uint2(p[0], p[1]), p[5] > 1 ? 1 : 0, 0).x;\n}\n";
static void run(id<MTLDevice> dev, id<MTLComputePipelineState> ps, id<MTLComputePipelineState> later, MTLPixelFormat fmt, const char *name,
                unsigned w, unsigned h, unsigned slices, bool mips, bool write, bool atomic)
{
   MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:mips];
   d.textureType = MTLTextureType2DArray; d.arrayLength = slices;
   d.storageMode = MTLStorageModePrivate;
   d.usage = MTLTextureUsageShaderRead | (write ? MTLTextureUsageShaderWrite : 0) | (atomic ? MTLTextureUsageShaderAtomic : 0);
   d.placementSparsePageSize = MTLSparsePageSize64;
   id<MTLTexture> tex = [dev newTextureWithDescriptor:d];
   if (!tex) { printf("%-10s %5ux%-5u x%u %s: no texture\n", name, w, h, slices, write ? "write" : "read"); return; }
   MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType2DArray pixelFormat:fmt sampleCount:1 sparsePageSize:MTLSparsePageSize64];
   printf("%-10s %5ux%-5u x%u %s%s%s: tier %ld, %lu levels, tail from level %lu (%lu bytes), tile %lux%lu\n", name, w, h, slices,
          mips ? "mips " : "", write ? "write" : "read-only", atomic ? "+atomic" : "", (long)tex.sparseTextureTier, tex.mipmapLevelCount,
          tex.firstMipmapInTail, tex.tailSizeInBytes, tile.width, tile.height);
   if (!write) return;
   MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
   hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 4 << 20;
   hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
   id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
   id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
   MTL4UpdateSparseTextureMappingOperation op = {.mode = MTLSparseTextureMappingModeMap,
      .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = 0, .textureSlice = 0, .heapOffset = 0};
   [q updateTextureMappings:tex heap:heap operations:&op count:1];
   id<MTLSharedEvent> ev = [dev newSharedEvent];
   [q signalEvent:ev value:1];
   id<MTLBuffer> out = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
   memset(out.contents, 0xee, 256);
   id<MTLBuffer> pb = [dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
   uint32_t *p = pb.contents;
   p[0] = 3; p[1] = 5; p[2] = (unsigned)tile.width + 3 < w ? (unsigned)tile.width + 3 : w - 1; p[3] = (unsigned)tile.height + 5 < h ? (unsigned)tile.height + 5 : h - 1;
   p[4] = (uint32_t)tex.mipmapLevelCount; p[5] = slices;
   id<MTLCommandQueue> q3 = [dev newCommandQueue];
   id<MTLCommandBuffer> cb = [q3 commandBuffer];
   [cb encodeWaitForEvent:ev value:1];
   id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
   [ce setComputePipelineState:ps];
   [ce setTexture:tex atIndex:0]; [ce setTexture:tex atIndex:1]; [ce setBuffer:out offset:0 atIndex:0]; [ce setBuffer:pb offset:0 atIndex:1];
   [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
   [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
   id<MTLCommandBuffer> cb2 = [q3 commandBuffer];
   ce = [cb2 computeCommandEncoder];
   [ce setComputePipelineState:later];
   [ce setTexture:tex atIndex:1]; [ce setBuffer:out offset:0 atIndex:0]; [ce setBuffer:pb offset:0 atIndex:1];
   [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
   [ce endEncoding]; [cb2 commit]; [cb2 waitUntilCompleted];
   const uint32_t *o = out.contents;
   printf("     same kernel: mapped %x, unmapped %x, other slice %x; resident: mapped %u, unmapped %u, other slice %u; by level:", o[0], o[1], o[2], o[3], o[4], o[5]);
   for (unsigned l = 0; l < tex.mipmapLevelCount && l < 16; l++) printf(" %u", o[8 + l]);
   printf("\n     next command buffer: mapped %x, unmapped %x, other slice %x%s%s\n", o[32], o[33], o[34], o[30] == 77 ? "" : " (kernel did not finish)",
          cb.error || cb2.error ? " ERROR" : "");
   if (cb.error) printf("     %s\n", cb.error.localizedDescription.UTF8String);
}
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      NSError *err = nil;
      id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:nil error:&err];
      if (!lib) { printf("%s\n", err.localizedDescription.UTF8String); return 1; }
      id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
      id<MTLComputePipelineState> later = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"later"] error:&err];
      printf("device %s\n", dev.name.UTF8String);
      run(dev, ps, later, MTLPixelFormatR32Uint, "r32uint", 16384, 768, 2, false, false, false);
      run(dev, ps, later, MTLPixelFormatR32Uint, "r32uint", 16384, 768, 2, false, true, false);
      run(dev, ps, later, MTLPixelFormatR32Uint, "r32uint", 16384, 768, 2, false, true, true);
      run(dev, ps, later, MTLPixelFormatR32Uint, "r32uint", 1024, 1024, 1, false, true, false);
      run(dev, ps, later, MTLPixelFormatR32Uint, "r32uint", 1024, 1024, 1, true, true, false);
      run(dev, ps, later, MTLPixelFormatR32Uint, "r32uint", 1024, 1024, 4, true, true, true);
      run(dev, ps, later, MTLPixelFormatR32Uint, "r32uint", 300, 200, 1, true, true, false);
   }
   return 0;
}
