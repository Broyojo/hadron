#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
/* What the GPU does with denormal 32-bit and 16-bit floats, in a compute kernel and in a fragment
 * function, for each math mode: preserved or flushed to zero, on input and as a result. Operands
 * come from a buffer so the compiler can't fold them. */
static const char *src =
   "#include <metal_stdlib>\n using namespace metal;\n"
   "static void run(const device float *in, device float *out) {\n"
   "  float d = in[0];      /* denormal: 1e-40 */\n"
   "  float m = in[1];      /* FLT_MIN */\n"
   "  float one = in[2];    /* 1.0 */\n"
   "  float half_ = in[3];  /* 0.5 */\n"
   "  out[0] = d;                 /* copy */\n"
   "  out[1] = d * one;           /* denormal operand, identity */\n"
   "  out[2] = d + d;             /* denormal operands, denormal result */\n"
   "  out[3] = m * half_;         /* normal operands, denormal result */\n"
   "  out[4] = m - m * 0.75f * one;   /* denormal result of a subtraction */\n"
   "  out[5] = d * 1e10f * one * 1e10f * 1e10f; /* denormal operand, normal result */\n"
   "  out[6] = (d == 0.0f) ? 1.0f : 0.0f;  /* compares equal to zero? */\n"
   "  out[7] = sqrt(d * d * 1e38f * 1e38f);\n"
   "  half h = half(in[4]);       /* 1e-6: denormal as half */\n"
   "  out[8] = float(h);\n"
   "  out[9] = float(h * half(one));\n"
   "  out[10] = fma(d, one, d);\n"
   "  out[11] = min(d, m);\n"
   "}\n"
   "kernel void k(const device float *in [[buffer(0)]], device float *out [[buffer(1)]]) { run(in, out); }\n"
   "struct V { float4 pos [[position]]; };\n"
   "vertex V v(uint vid [[vertex_id]]) { V o; o.pos = float4(float2((vid << 1) & 2, vid & 2) * 2 - 1, 0, 1); return o; }\n"
   "fragment float4 f(V i [[stage_in]], const device float *in [[buffer(0)]], device float *out [[buffer(1)]]) { run(in, out); return float4(0); }\n";
static void show(const char *what, const float *o)
{
   static const char *names[] = {"copy", "d*1", "d+d", "FLT_MIN*0.5", "m-0.75m", "d*1e30", "d==0", "sqrt",
                                 "half copy", "half*1", "fma(d,1,d)", "min(d,m)"};
   printf("%s\n", what);
   for (int i = 0; i < 12; i++) {
      uint32_t bits; memcpy(&bits, &o[i], 4);
      printf("   %-12s %08x %g\n", names[i], bits, o[i]);
   }
}
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      id<MTLCommandQueue> q = [dev newCommandQueue];
      printf("device %s\n", dev.name.UTF8String);
      const MTLMathMode modes[] = {MTLMathModeSafe, MTLMathModeRelaxed, MTLMathModeFast};
      const char *mode_names[] = {"safe", "relaxed", "fast"};
      for (int mi = 0; mi < 3; mi++) {
         NSError *err = nil;
         MTLCompileOptions *opts = [MTLCompileOptions new];
         opts.mathMode = modes[mi];
         id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:opts error:&err];
         if (!lib) { printf("compile: %s\n", err.localizedDescription.UTF8String); return 1; }
         id<MTLBuffer> in = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
         float *i = in.contents;
         uint32_t dbits = 0x000116c2; /* 1e-40 */
         memcpy(&i[0], &dbits, 4); i[1] = 1.17549435e-38f; i[2] = 1.0f; i[3] = 0.5f; i[4] = 1e-6f;
         for (int stage = 0; stage < 2; stage++) {
            id<MTLBuffer> out = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb = [q commandBuffer];
            if (stage == 0) {
               id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
               id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
               [ce setComputePipelineState:ps];
               [ce setBuffer:in offset:0 atIndex:0]; [ce setBuffer:out offset:0 atIndex:1];
               [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
               [ce endEncoding];
            } else {
               MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
               pd.vertexFunction = [lib newFunctionWithName:@"v"];
               pd.fragmentFunction = [lib newFunctionWithName:@"f"];
               pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
               id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
               if (!ps) { printf("pipeline: %s\n", err.localizedDescription.UTF8String); return 1; }
               MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
               td.usage = MTLTextureUsageRenderTarget;
               MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new];
               rp.colorAttachments[0].texture = [dev newTextureWithDescriptor:td];
               rp.colorAttachments[0].loadAction = MTLLoadActionClear;
               rp.colorAttachments[0].storeAction = MTLStoreActionStore;
               id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
               [re setRenderPipelineState:ps];
               [re setFragmentBuffer:in offset:0 atIndex:0]; [re setFragmentBuffer:out offset:0 atIndex:1];
               [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
               [re endEncoding];
            }
            [cb commit]; [cb waitUntilCompleted];
            char what[64]; snprintf(what, sizeof(what), "math mode %s, %s", mode_names[mi], stage ? "fragment" : "compute");
            show(what, out.contents);
         }
      }
   }
   return 0;
}
