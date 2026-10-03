#import <Metal/Metal.h>
#include <stdio.h>
/* Which 64-bit atomic operations MSL accepts, on buffers and on textures, and whether the ones it
 * accepts work: each of 4096 threads applies the operation to one shared 64-bit value. */
#define HDR "#include <metal_stdlib>\n using namespace metal;\n"
#define BUF(body) HDR "kernel void k(device atomic_ulong *a [[buffer(0)]], device ulong *out [[buffer(1)]], uint tid [[thread_position_in_grid]]) {\n" \
   "  ulong v = 0x100000000ul + tid;\n" body "}\n"
#define TEX(type, body) HDR "kernel void k(texture2d<" type ", access::read_write> t [[texture(0)]], device ulong *out [[buffer(1)]], uint tid [[thread_position_in_grid]]) {\n" \
   "  ulong v = 0x100000000ul + tid;\n" body "}\n"
static const struct { const char *name, *src; } tests[] = {
   {"buffer max", BUF("  atomic_max_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer min", BUF("  atomic_min_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer add", BUF("  atomic_fetch_add_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer sub", BUF("  atomic_fetch_sub_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer and", BUF("  atomic_fetch_and_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer or", BUF("  atomic_fetch_or_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer xor", BUF("  atomic_fetch_xor_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer fetch_max", BUF("  out[1 + tid] = atomic_fetch_max_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer exchange", BUF("  atomic_exchange_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer load", BUF("  out[1 + tid] = atomic_load_explicit(a, memory_order_relaxed);\n")},
   {"buffer store", BUF("  atomic_store_explicit(a, v, memory_order_relaxed);\n")},
   {"buffer cas", BUF("  ulong e = 0; atomic_compare_exchange_weak_explicit(a, &e, v, memory_order_relaxed, memory_order_relaxed);\n")},
   {"buffer atomic_long max", HDR "kernel void k(device atomic<long> *a [[buffer(0)]], uint tid [[thread_position_in_grid]]) {\n"
                              "  atomic_max_explicit(a, (long)tid, memory_order_relaxed); }\n"},
   {"texture ulong max", TEX("ulong", "  t.atomic_max(uint2(0, 0), ulong4(v));\n")},
   {"texture ulong min", TEX("ulong", "  t.atomic_min(uint2(0, 0), ulong4(v));\n")},
   {"texture ulong fetch_max", TEX("ulong", "  out[1 + tid] = t.atomic_fetch_max(uint2(0, 0), ulong4(v)).x;\n")},
   {"texture ulong add", TEX("ulong", "  t.atomic_fetch_add(uint2(0, 0), ulong4(v));\n")},
   {"texture ulong exchange", TEX("ulong", "  t.atomic_exchange(uint2(0, 0), ulong4(v));\n")},
   {"texture ulong load", TEX("ulong", "  out[1 + tid] = t.atomic_load(uint2(0, 0)).x;\n")},
   {"texture ulong store", TEX("ulong", "  t.atomic_store(uint2(0, 0), ulong4(v));\n")},
   {"texture ulong cas", TEX("ulong", "  ulong4 e = ulong4(0); t.atomic_compare_exchange_weak(uint2(0, 0), &e, ulong4(v));\n")},
   {"texture ulong read", TEX("ulong", "  out[1 + tid] = t.read(uint2(0, 0)).x;\n")},
   {"texture ulong write", TEX("ulong", "  t.write(ulong4(v), uint2(0, 0));\n")},
   {"texture uint add", TEX("uint", "  t.atomic_fetch_add(uint2(0, 0), uint4(tid));\n")},
   {"texture uint cas", TEX("uint", "  uint4 e = uint4(0); t.atomic_compare_exchange_weak(uint2(0, 0), &e, uint4(tid));\n")},
};
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLCommandQueue> q = [dev newCommandQueue];
      printf("device %s\n", dev.name.UTF8String);
      for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
         NSError *err = nil;
         MTLCompileOptions *opts = [MTLCompileOptions new];
         id<MTLLibrary> lib = [dev newLibraryWithSource:@(tests[i].src) options:opts error:&err];
         if (!lib) {
            NSString *msg = err.localizedDescription;
            NSRange r = [msg rangeOfString:@"error: "];
            if (r.location != NSNotFound) msg = [msg substringFromIndex:r.location];
            r = [msg rangeOfString:@"\n"];
            if (r.location != NSNotFound) msg = [msg substringToIndex:r.location];
            printf("%-26s does not compile: %s\n", tests[i].name, msg.UTF8String);
            continue;
         }
         id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
         if (!ps) { printf("%-26s no pipeline: %s\n", tests[i].name, err.localizedDescription.UTF8String); continue; }
         const uint64_t init = strstr(tests[i].name, "min") ? ~0ull : strstr(tests[i].name, "and") ? ~0ull : 0;
         id<MTLBuffer> a = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
         id<MTLBuffer> out = [dev newBufferWithLength:8 * 4100 options:MTLResourceStorageModeShared];
         *(uint64_t *)a.contents = init;
         MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            strstr(tests[i].name, "ulong") ? MTLPixelFormatRG32Uint : MTLPixelFormatR32Uint width:4 height:4 mipmapped:NO];
         td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageShaderAtomic;
         td.storageMode = MTLStorageModeShared;
         id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
         uint64_t texels[16]; for (int j = 0; j < 16; j++) texels[j] = init;
         [tex replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:texels bytesPerRow:strstr(tests[i].name, "ulong") ? 32 : 16];
         id<MTLCommandBuffer> cb = [q commandBuffer];
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         [ce setComputePipelineState:ps];
         [ce setBuffer:a offset:0 atIndex:0]; [ce setBuffer:out offset:0 atIndex:1]; [ce setTexture:tex atIndex:0];
         [ce dispatchThreads:MTLSizeMake(4096, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
         [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
         uint64_t result = *(uint64_t *)a.contents;
         if (strstr(tests[i].name, "texture")) {
            [tex getBytes:texels bytesPerRow:strstr(tests[i].name, "ulong") ? 32 : 16 fromRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0];
            result = strstr(tests[i].name, "ulong") ? texels[0] : (uint32_t)texels[0];
         }
         printf("%-26s compiles, result %016llx%s\n", tests[i].name, (unsigned long long)result, cb.error ? " (command buffer error)" : "");
      }
   }
   return 0;
}
