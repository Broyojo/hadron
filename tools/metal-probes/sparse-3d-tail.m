#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
/* Placement-sparse 3D texture: map every tile, the mip tail page by page with a distinct heap
 * page each, by one of three origin rules, then write all levels through blits and read them back. */
static void run(id<MTLDevice> dev, int w, int h, int d, int rule)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor new];
    td.textureType = MTLTextureType3D; td.pixelFormat = MTLPixelFormatRGBA8Uint;
    td.width = w; td.height = h; td.depth = d; td.storageMode = MTLStorageModePrivate;
    int levels = 1; { int m = w; if (h > m) m = h; if (d > m) m = d; while (m > 1) { m >>= 1; levels++; } }
    td.mipmapLevelCount = levels; td.usage = MTLTextureUsageShaderRead;
    td.placementSparsePageSize = MTLSparsePageSize64;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
    hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.size = 64 << 20;
    hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
    id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
    id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
    MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType3D pixelFormat:td.pixelFormat sampleCount:1 sparsePageSize:MTLSparsePageSize64];
    NSUInteger tail = t.firstMipmapInTail, pages = (t.tailSizeInBytes + 65535) / 65536, heap_page = 0;
    MTL4UpdateSparseTextureMappingOperation ops[4096]; int n = 0;
    for (NSUInteger l = 0; l < tail && l < (NSUInteger)levels; l++) {
        int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
        for (int z = 0; z < (ld + (int)tile.depth - 1) / (int)tile.depth; z++)
        for (int y = 0; y < (lh + (int)tile.height - 1) / (int)tile.height; y++)
        for (int x = 0; x < (lw + (int)tile.width - 1) / (int)tile.width; x++)
            ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap,
                .textureRegion = MTLRegionMake3D(x, y, z, 1, 1, 1), .textureLevel = l, .textureSlice = 0, .heapOffset = heap_page++};
    }
    if (rule == 3 && tail < (NSUInteger)levels) {
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap,
            .textureRegion = MTLRegionMake3D(0, 0, 0, pages, 1, 1), .textureLevel = tail, .textureSlice = 0, .heapOffset = heap_page};
        heap_page += pages;
    } else if (rule >= 10 && tail < (NSUInteger)levels) {
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap,
            .textureRegion = MTLRegionMake3D(rule - 10, 0, 0, 1, 1, 1), .textureLevel = tail, .textureSlice = 0, .heapOffset = heap_page};
        heap_page += 1;
    } else if (rule == 4 && tail < (NSUInteger)levels) {
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap,
            .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = tail, .textureSlice = 0, .heapOffset = heap_page};
        heap_page += pages;
    } else
    for (NSUInteger p = 0; p < pages && tail < (NSUInteger)levels; p++)
        ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap,
            .textureRegion = rule == 0 ? MTLRegionMake3D(p, 0, 0, 1, 1, 1) : rule == 1 ? MTLRegionMake3D(0, 0, p, 1, 1, 1) : MTLRegionMake3D(0, p, 0, 1, 1, 1),
            .textureLevel = tail, .textureSlice = 0, .heapOffset = heap_page++};
    [q updateTextureMappings:t heap:heap operations:ops count:n];
    id<MTLSharedEvent> ev = [dev newSharedEvent];
    [q signalEvent:ev value:1];
    size_t total = 0; for (int l = 0; l < levels; l++) total += (size_t)(w >> l ?: 1) * (h >> l ?: 1) * (d >> l ?: 1) * 4;
    id<MTLBuffer> src = [dev newBufferWithLength:total options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst = [dev newBufferWithLength:total options:MTLResourceStorageModeShared];
    uint8_t *sp = src.contents; for (size_t i = 0; i < total; i++) sp[i] = (uint8_t)(i * 7 + i / 251 + 1);
    memset(dst.contents, 0, total);
    id<MTLCommandQueue> q3 = [dev newCommandQueue];
    MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new];
    id<MTLResidencySet> set = [dev newResidencySetWithDescriptor:rd error:nil];
    [set addAllocation:heap]; [set addAllocation:t]; [set addAllocation:src]; [set addAllocation:dst]; [set commit];
    [q3 addResidencySet:set];
    id<MTLCommandBuffer> cb = [q3 commandBuffer];
    [cb encodeWaitForEvent:ev value:1];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    size_t off = 0;
    for (int l = 0; l < levels; l++) {
        int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
        [bl copyFromBuffer:src sourceOffset:off sourceBytesPerRow:lw * 4 sourceBytesPerImage:lw * lh * 4 sourceSize:MTLSizeMake(lw, lh, ld) toTexture:t destinationSlice:0 destinationLevel:l destinationOrigin:MTLOriginMake(0, 0, 0)];
        off += (size_t)lw * lh * ld * 4;
    }
    off = 0;
    for (int l = 0; l < levels; l++) {
        int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
        [bl copyFromTexture:t sourceSlice:0 sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, ld) toBuffer:dst destinationOffset:off destinationBytesPerRow:lw * 4 destinationBytesPerImage:lw * lh * 4];
        off += (size_t)lw * lh * ld * 4;
    }
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    if (0) {
        size_t o1 = (size_t)w * h * d * 4; int lw = w >> 1, lh = h >> 1, ld = d >> 1;
        for (int z = 0; z < ld; z++) {
            int badx[64] = {0};
            for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) {
                size_t i = o1 + (((size_t)z * lh + y) * lw + x) * 4;
                if (memcmp((uint8_t *)src.contents + i, (uint8_t *)dst.contents + i, 4)) badx[x / 128]++;
            }
            printf("  level 1 z %d bad texels per 128-wide column:", z);
            for (int c = 0; c < lw / 128; c++) printf(" %d", badx[c]);
            printf("\n");
        }
    }
    if (rule >= 10) {
        printf("page %d ok:", rule - 10);
        size_t o = 0;
        for (int l = 0; l < levels; l++) {
            int lw = w >> l ?: 1, lh = h >> l ?: 1, ld = d >> l ?: 1;
            if (l >= (int)tail) {
                int cols = (lw + 127) / 128;
                for (int z = 0; z < ld; z++) for (int c = 0; c < cols; c++) {
                    int good = 1;
                    for (int y = 0; y < lh && good; y++) for (int x = c * 128; x < lw && x < c * 128 + 128; x++) {
                        size_t i = o + (((size_t)z * lh + y) * lw + x) * 4;
                        if (memcmp((uint8_t *)src.contents + i, (uint8_t *)dst.contents + i, 4)) { good = 0; break; }
                    }
                    if (good) printf(" L%d(z%d,c%d)", l, z, c);
                }
            }
            o += (size_t)lw * lh * ld * 4;
        }
        printf("\n");
        return;
    }
    int bad = 0; off = 0;
    for (int l = 0; l < levels; l++) {
        size_t sz = (size_t)(w >> l ?: 1) * (h >> l ?: 1) * (d >> l ?: 1) * 4;
        if (memcmp((uint8_t *)src.contents + off, (uint8_t *)dst.contents + off, sz)) bad |= 1 << l;
        off += sz;
    }
    printf("%4dx%-4dx%-2d tile %lux%lux%lu tail level %lu of %d, %lu tail pages, rule %d: %s (bad levels 0x%x) %s\n", w, h, d,
           (unsigned long)tile.width, (unsigned long)tile.height, (unsigned long)tile.depth, (unsigned long)tail, levels, (unsigned long)pages, rule,
           bad ? "WRONG" : "ok", bad, cb.error ? cb.error.localizedDescription.UTF8String : "");
}
int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        for (int rule = 10; rule < 20; rule++) run(dev, 1024, 128, 8, rule);
    }
    return 0;
}
