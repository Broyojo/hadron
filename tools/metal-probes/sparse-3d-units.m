#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
/* WHOLE=1 maps the whole tail in one operation instead.
 * How many heap pages each mip tail position of a placement-sparse 3D texture covers: map one
 * position at heap page 0, write every level of the texture, then read the heap back through a
 * buffer placed over the same pages and count the pages that changed. Also which tail levels read
 * back right with only that position mapped. */
static MTLPixelFormat fmt = MTLPixelFormatRGBA8Uint;
static int bpp = 4;

static void probe(id<MTLDevice> dev, int w, int h, int d)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor new];
    td.textureType = MTLTextureType3D; td.pixelFormat = fmt;
    td.width = w; td.height = h; td.depth = d; td.storageMode = MTLStorageModePrivate;
    int levels = 1; { int m = w; if (h > m) m = h; if (d > m) m = d; while (m > 1) { m >>= 1; levels++; } }
    td.mipmapLevelCount = levels; td.usage = MTLTextureUsageShaderRead;
    td.placementSparsePageSize = MTLSparsePageSize64;
    id<MTLTexture> probe_tex = [dev newTextureWithDescriptor:td];
    NSUInteger tail = probe_tex.firstMipmapInTail, pages = (probe_tex.tailSizeInBytes + 65535) / 65536;
    MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType3D pixelFormat:fmt sampleCount:1 sparsePageSize:MTLSparsePageSize64];
    printf("%dx%dx%d, %d levels, tile %lux%lux%lu, tail from level %lu, %lu tail pages\n", w, h, d, levels,
           (unsigned long)tile.width, (unsigned long)tile.height, (unsigned long)tile.depth, (unsigned long)tail, (unsigned long)pages);
    if (tail >= (NSUInteger)levels) return;

    size_t total = 0, level_off[32];
    for (int l = 0; l < levels; l++) { level_off[l] = total; total += (size_t)(w >> l ?: 1) * (h >> l ?: 1) * (d >> l ?: 1) * bpp; }
    id<MTLBuffer> src = [dev newBufferWithLength:total options:MTLResourceStorageModeShared];
    uint8_t *sp = src.contents; for (size_t i = 0; i < total; i++) sp[i] = (uint8_t)(i * 7 + i / 251 + 1) | 1;

    for (NSUInteger pos = getenv("WHOLE") ? pages : 0; pos <= pages; pos++) {
        MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
        hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = (pages + 4) * 65536;
        hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
        id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
        id<MTLBuffer> alias = [heap newBufferWithLength:(pages + 4) * 65536 options:MTLResourceStorageModePrivate offset:0];
        id<MTLTexture> t = [dev newTextureWithDescriptor:td];
        id<MTLBuffer> heapread = [dev newBufferWithLength:(pages + 4) * 65536 options:MTLResourceStorageModeShared];
        id<MTLBuffer> dst = [dev newBufferWithLength:total options:MTLResourceStorageModeShared];
        memset(dst.contents, 0, total);

        id<MTLCommandQueue> q3 = [dev newCommandQueue];
        MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new];
        id<MTLResidencySet> set = [dev newResidencySetWithDescriptor:rd error:nil];
        [set addAllocation:heap]; [set addAllocation:t]; [set addAllocation:src]; [set addAllocation:dst]; [set addAllocation:heapread];
        [set commit];
        [q3 addResidencySet:set];

        id<MTLCommandBuffer> cb0 = [q3 commandBuffer];
        id<MTLBlitCommandEncoder> b0 = [cb0 blitCommandEncoder];
        [b0 fillBuffer:alias range:NSMakeRange(0, alias.length) value:0];
        [b0 endEncoding]; [cb0 commit]; [cb0 waitUntilCompleted];

        id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
        MTL4UpdateSparseTextureMappingOperation op = {.mode = MTLSparseTextureMappingModeMap,
            .textureRegion = pos == pages ? MTLRegionMake3D(0, 0, 0, pages, 1, 1) : MTLRegionMake3D(pos, 0, 0, 1, 1, 1),
            .textureLevel = tail, .textureSlice = 0, .heapOffset = 0};
        [q updateTextureMappings:t heap:heap operations:&op count:1];
        id<MTLSharedEvent> ev = [dev newSharedEvent];
        [q signalEvent:ev value:1];

        id<MTLCommandBuffer> cb = [q3 commandBuffer];
        [cb encodeWaitForEvent:ev value:1];
        id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
        for (int l = (int)tail; l < levels; l++) {
            int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
            [bl copyFromBuffer:src sourceOffset:level_off[l] sourceBytesPerRow:lw * bpp sourceBytesPerImage:lw * lh * bpp
                    sourceSize:MTLSizeMake(lw, lh, ld) toTexture:t destinationSlice:0 destinationLevel:l destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        for (int l = (int)tail; l < levels; l++) {
            int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
            [bl copyFromTexture:t sourceSlice:0 sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, ld)
                       toBuffer:dst destinationOffset:level_off[l] destinationBytesPerRow:lw * bpp destinationBytesPerImage:lw * lh * bpp];
        }
        [bl copyFromBuffer:alias sourceOffset:0 toBuffer:heapread destinationOffset:0 size:alias.length];
        [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];

        int used = 0, last = -1;
        for (NSUInteger p = 0; p < pages + 4; p++) {
            const uint8_t *pg = (const uint8_t *)heapread.contents + p * 65536;
            int any = 0; for (int i = 0; i < 65536; i++) if (pg[i]) { any = 1; break; }
            if (any) { used++; last = (int)p; }
            if (getenv("PAGES")) printf("%c", any ? '#' : '.');
        }
        if (getenv("PAGES")) printf("\n");
        if (pos == pages) printf("  whole tail  :"); else printf("  position %2lu:", (unsigned long)pos);
        printf(" %d pages written (last page %d), levels right:", used, last);
        for (int l = (int)tail; l < levels; l++) {
            size_t sz = (size_t)(w >> l ?: 1) * (h >> l ?: 1) * (d >> l ?: 1) * bpp;
            if (!memcmp(sp + level_off[l], (uint8_t *)dst.contents + level_off[l], sz)) printf(" %d", l);
        }
        printf("%s\n", cb.error ? " (command buffer error)" : "");
    }
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (getenv("BPP16")) { fmt = MTLPixelFormatRGBA32Uint; bpp = 16; }
        if (getenv("BPP1")) { fmt = MTLPixelFormatR8Uint; bpp = 1; }
        if (argc == 4) { probe(dev, atoi(argv[1]), atoi(argv[2]), atoi(argv[3])); return 0; }
        probe(dev, 1024, 128, 8);
        probe(dev, 256, 256, 256);
        probe(dev, 128, 128, 32);
        probe(dev, 64, 64, 64);
    }
    return 0;
}
