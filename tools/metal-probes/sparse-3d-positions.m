#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* 3D placement-sparse tail footprint. args w h d. env: FMT=R8|RGBA8|RGBA32, WIDTH=tail region width (tiles),
 * X0=tail region origin.x, HOFF=heapOffset, LEVEL=write only this level, MAPALL=1 map non-tail levels too
 * (placed before the tail), P16=1 16K pages. Every step in its own command buffer. */
int main(int argc, char **argv) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    int w = atoi(argv[1]), h = atoi(argv[2]), d = atoi(argv[3]);
    const char *f = getenv("FMT") ?: "RGBA8"; MTLPixelFormat fmt = MTLPixelFormatRGBA8Uint; int bpp = 4;
    if (!strcmp(f, "R8")) { fmt = MTLPixelFormatR8Uint; bpp = 1; } else if (!strcmp(f, "RGBA32")) { fmt = MTLPixelFormatRGBA32Uint; bpp = 16; }
    MTLSparsePageSize psz = getenv("P16") ? MTLSparsePageSize16 : MTLSparsePageSize64;
    NSUInteger PG = [dev sparseTileSizeInBytesForSparsePageSize:psz];
    MTLTextureDescriptor *td = [MTLTextureDescriptor new];
    td.textureType = MTLTextureType3D; td.pixelFormat = fmt; td.width = w; td.height = h; td.depth = d; td.storageMode = MTLStorageModePrivate;
    int levels = 1; { int m = w; if (h > m) m = h; if (d > m) m = d; while (m > 1) { m >>= 1; levels++; } }
    td.mipmapLevelCount = levels; td.usage = MTLTextureUsageShaderRead; td.placementSparsePageSize = psz; if (getenv("NOOPT")) td.allowGPUOptimizedContents = NO;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType3D pixelFormat:fmt sampleCount:1 sparsePageSize:psz];
    NSUInteger tail = t.firstMipmapInTail, pages = (t.tailSizeInBytes + PG - 1) / PG;
    NSUInteger width = getenv("WIDTH") ? atoi(getenv("WIDTH")) : pages, hoff = getenv("HOFF") ? atoi(getenv("HOFF")) : 0;
    NSUInteger x0 = getenv("X0") ? atoi(getenv("X0")) : 0;
    /* non-tail levels */
    MTL4UpdateSparseTextureMappingOperation ops[4096]; int n = 0; NSUInteger hp = 0;
    if (getenv("MAPALL")) for (NSUInteger l = 0; l < tail; l++) {
        NSUInteger lw = MAX(w >> l, 1), lh = MAX(h >> l, 1), ld = MAX(d >> l, 1);
        NSUInteger tx = (lw + tile.width - 1) / tile.width, ty = (lh + tile.height - 1) / tile.height, tz = (ld + tile.depth - 1) / tile.depth;
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0,0,0,tx,ty,tz), .textureLevel = l, .heapOffset = hp};
        hp += tx * ty * tz;
    }
    NSUInteger tailbase = hp + hoff, hpages = tailbase + MAX(width, pages) + 16;
    MTLHeapDescriptor *hd = [MTLHeapDescriptor new]; hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate;
    hd.size = hpages * PG; hd.maxCompatiblePlacementSparsePageSize = psz;
    id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
    id<MTLBuffer> alias = [heap newBufferWithLength:hpages * PG options:MTLResourceStorageModePrivate offset:0];
    id<MTLBuffer> heapread = [dev newBufferWithLength:hpages * PG options:MTLResourceStorageModeShared];
    if (getenv("OPS")) { char *o = strdup(getenv("OPS")); for (char *tok = strtok(o, ","); tok; tok = strtok(NULL, ",")) { int x, hh; sscanf(tok, "%d:%d", &x, &hh);
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(x,0,0,1,1,1), .textureLevel = tail, .heapOffset = tailbase + hh}; } }
    else if (tail < (NSUInteger)levels)
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(x0,0,0,width,1,1), .textureLevel = tail, .heapOffset = tailbase};
    printf("%s %dx%dx%d, %d levels, tile %lux%lux%lu, tail from %lu, tailSizeInBytes %lu = %lu pages of %lu; tail mapped at heap page %lu width %lu\n", f, w, h, d, levels,
        (unsigned long)tile.width, (unsigned long)tile.height, (unsigned long)tile.depth, (unsigned long)tail, (unsigned long)t.tailSizeInBytes, (unsigned long)pages, (unsigned long)PG, (unsigned long)tailbase, (unsigned long)width);
    id<MTLCommandQueue> q3 = [dev newCommandQueue];
    MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new];
    id<MTLResidencySet> set = [dev newResidencySetWithDescriptor:rd error:nil];
    [set addAllocation:heap]; [set addAllocation:t]; [set commit]; [q3 addResidencySet:set];
    #define CB(body) { id<MTLCommandBuffer> cb = [q3 commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder]; body; [bl endEncoding]; [cb commit]; [cb waitUntilCompleted]; if (cb.error) printf("cb error %s\n", cb.error.description.UTF8String); }
    CB([bl fillBuffer:alias range:NSMakeRange(0, alias.length) value:0]);
    id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
    [q updateTextureMappings:t heap:heap operations:ops count:n];
    id<MTLSharedEvent> ev = [dev newSharedEvent]; [q signalEvent:ev value:1]; [ev waitUntilSignaledValue:1 timeoutMS:5000];
    size_t total = 0, off[32]; for (int l = 0; l < levels; l++) { off[l] = total; total += (size_t)(w >> l ?: 1) * (h >> l ?: 1) * (d >> l ?: 1) * bpp; }
    id<MTLBuffer> src = [dev newBufferWithLength:total options:MTLResourceStorageModeShared], dst = [dev newBufferWithLength:total options:MTLResourceStorageModeShared];
    uint8_t *sp = src.contents; for (size_t i = 0; i < total; i++) sp[i] = (uint8_t)(i * 7 + i / 251 + 1) | 1;
    int only = getenv("LEVEL") ? atoi(getenv("LEVEL")) : -1;
    int lo = getenv("MAPALL") ? 0 : (int)tail;
    for (int l = lo; l < levels; l++) if (only < 0 || only == l) { int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
        CB([bl copyFromBuffer:src sourceOffset:off[l] sourceBytesPerRow:lw * bpp sourceBytesPerImage:lw * lh * bpp sourceSize:MTLSizeMake(lw, lh, ld) toTexture:t destinationSlice:0 destinationLevel:l destinationOrigin:MTLOriginMake(0, 0, 0)]); }
    for (int l = lo; l < levels; l++) if (only < 0 || only == l) { int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
        CB([bl copyFromTexture:t sourceSlice:0 sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, ld) toBuffer:dst destinationOffset:off[l] destinationBytesPerRow:lw * bpp destinationBytesPerImage:lw * lh * bpp]); }
    CB([bl copyFromBuffer:alias sourceOffset:0 toBuffer:heapread destinationOffset:0 size:alias.length]);
    int used = 0, last = -1, first = -1;
    printf("  heap: ");
    for (NSUInteger p = 0; p < hpages; p++) { const uint8_t *pg = (const uint8_t *)heapread.contents + p * PG; int any = 0;
        for (NSUInteger i = 0; i < PG; i++) if (pg[i]) { any = 1; break; }
        if (any) { used++; last = (int)p; if (first < 0 && p >= tailbase) first = (int)p; }
        printf("%c", p == tailbase ? '|' : 0); printf("%c", any ? '#' : '.'); }
    printf("\n  %d pages written, first tail-area page %d, last %d (tail area %lu..%lu); levels right:", used, first, last, (unsigned long)tailbase, (unsigned long)(tailbase + pages - 1));
    for (int l = lo; l < levels; l++) if (only < 0 || only == l) { size_t sz = (size_t)(w >> l ?: 1) * (h >> l ?: 1) * (d >> l ?: 1) * bpp; if (!memcmp(sp + off[l], (uint8_t *)dst.contents + off[l], sz)) printf(" %d", l); else printf(" !%d", l); }
    printf("\n");
} return 0; }
