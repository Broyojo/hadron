#import <Metal/Metal.h>
#include <stdio.h>
int main(void)
{
   @autoreleasepool {
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      struct { MTLPixelFormat f; const char *n; } fmts[] = {
         {MTLPixelFormatR8Unorm, "r8 (1B)"}, {MTLPixelFormatRG8Unorm, "rg8 (2B)"}, {MTLPixelFormatR16Float, "r16f (2B)"},
         {MTLPixelFormatRGBA8Unorm, "rgba8 (4B)"}, {MTLPixelFormatR32Float, "r32f (4B)"}, {MTLPixelFormatRGB10A2Unorm, "rgb10a2 (4B)"},
         {MTLPixelFormatRGBA16Float, "rgba16f (8B)"}, {MTLPixelFormatRG32Float, "rg32f (8B)"}, {MTLPixelFormatRGBA32Float, "rgba32f (16B)"},
         {MTLPixelFormatBC1_RGBA, "bc1 (8B blk)"}, {MTLPixelFormatBC3_RGBA, "bc3 (16B blk)"}, {MTLPixelFormatBC7_RGBAUnorm, "bc7 (16B blk)"},
         {MTLPixelFormatBC4_RUnorm, "bc4 (8B blk)"}, {MTLPixelFormatRG11B10Float, "rg11b10 (4B)"}, {MTLPixelFormatRGB9E5Float, "rgb9e5 (4B)"}};
      for (unsigned i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++) {
         MTLSize t = [dev sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:fmts[i].f sampleCount:1 sparsePageSize:MTLSparsePageSize64];
         MTLSize t3 = [dev sparseTileSizeWithTextureType:MTLTextureType3D pixelFormat:fmts[i].f sampleCount:1 sparsePageSize:MTLSparsePageSize64];
         printf("%-16s 2D %lux%lu  3D %lux%lux%lu\n", fmts[i].n, t.width, t.height, t3.width, t3.height, t3.depth);
      }
   }
   return 0;
}
