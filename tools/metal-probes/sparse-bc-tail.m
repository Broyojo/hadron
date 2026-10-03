#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static MTLPixelFormat FMT; static int BB;
/* Upload every level of a 51x65 BC1 texture with a full mip chain, read every level back. */
static void run(id<MTLDevice> dev, int sparse, int w, int h)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:FMT width:w height:h mipmapped:YES];
    td.storageMode = MTLStorageModePrivate; td.usage = MTLTextureUsageShaderRead;
    if (sparse) td.placementSparsePageSize = getenv("P16") ? MTLSparsePageSize16 : getenv("P256") ? MTLSparsePageSize256 : MTLSparsePageSize64;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    id<MTLCommandQueue> q3 = [dev newCommandQueue];
    id<MTLSharedEvent> ev = [dev newSharedEvent];
    id<MTLHeap> heap = nil;
    if (sparse) {
        MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
        hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; 
        hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize256; hd.size = 16 << 20;
        heap = [dev newHeapWithDescriptor:hd];
        id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
        NSUInteger psz = getenv("P16") ? 16384 : getenv("P256") ? 262144 : 65536; NSUInteger pages = (t.tailSizeInBytes + psz - 1) / psz;
        MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:td.pixelFormat sampleCount:1 sparsePageSize:(getenv("P16") ? MTLSparsePageSize16 : getenv("P256") ? MTLSparsePageSize256 : MTLSparsePageSize64)];
        static MTL4UpdateSparseTextureMappingOperation ops[8192]; int n = 0; NSUInteger hp = 0;
        for (NSUInteger l = 0; l < t.firstMipmapInTail && l < t.mipmapLevelCount; l++) {
            NSUInteger lw = MAX(w >> l, 1), lh = MAX(h >> l, 1);
            ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap,
                .textureRegion = MTLRegionMake3D(0, 0, 0, (lw + tile.width - 1) / tile.width, (lh + tile.height - 1) / tile.height, 1), .textureLevel = l, .textureSlice = 0, .heapOffset = hp};
            hp += ((lw + tile.width - 1) / tile.width) * ((lh + tile.height - 1) / tile.height);
        }
        if (t.firstMipmapInTail < t.mipmapLevelCount)
            ops[n++] = (MTL4UpdateSparseTextureMappingOperation){.mode = MTLSparseTextureMappingModeMap,
                .textureRegion = MTLRegionMake3D(0, 0, 0, pages, 1, 1), .textureLevel = t.firstMipmapInTail, .textureSlice = 0, .heapOffset = hp};
        [q updateTextureMappings:t heap:heap operations:ops count:n];
        [q signalEvent:ev value:1];

    }
    MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new];
    id<MTLResidencySet> set = [dev newResidencySetWithDescriptor:rd error:nil];
    if (heap) [set addAllocation:heap];
    [set addAllocation:t]; [set commit]; [q3 addResidencySet:set];
    int levels = (int)t.mipmapLevelCount; size_t total = 0, offs[16];
    for (int l = 0; l < levels; l++) { int lw = w >> l ?: 1, lh = h >> l ?: 1; offs[l] = total; total += (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * BB; }
    id<MTLBuffer> src = [dev newBufferWithLength:total options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst = [dev newBufferWithLength:total options:MTLResourceStorageModeShared];
    for (size_t i = 0; i < total; i++) ((uint8_t *)src.contents)[i] = (uint8_t)(i * 13 + 7);
    memset(dst.contents, 0, total);
    id<MTLCommandBuffer> cb = [q3 commandBuffer];
    if (sparse) [cb encodeWaitForEvent:ev value:1];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    for (int k = 0; k < levels; k++) { int l = getenv("REV") ? levels - 1 - k : k;
        int lw = w >> l ?: 1, lh = h >> l ?: 1, bpr = (lw + 3) / 4 * BB;
        [bl copyFromBuffer:src sourceOffset:offs[l] sourceBytesPerRow:bpr sourceBytesPerImage:0 sourceSize:MTLSizeMake(lw, lh, 1) toTexture:t destinationSlice:0 destinationLevel:l destinationOrigin:MTLOriginMake(0, 0, 0)];
    }
    for (int l = 0; l < levels; l++) {
        int lw = w >> l ?: 1, lh = h >> l ?: 1, bpr = (lw + 3) / 4 * BB;
        [bl copyFromTexture:t sourceSlice:0 sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, 1) toBuffer:dst destinationOffset:offs[l] destinationBytesPerRow:bpr destinationBytesPerImage:0];
    }
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    int any = 0; char line[512]; int pos = snprintf(line, sizeof(line), "%s %dx%d (tail from %lu):", sparse ? "sparse" : "plain ", w, h, (unsigned long)t.firstMipmapInTail);
    for (int l = 0; l < levels; l++) {
        int lw = w >> l ?: 1, lh = h >> l ?: 1; size_t n = (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * BB;
        if (0) { int bw = (lw + 3) / 4, bh = (lh + 3) / 4; for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) if (memcmp((uint8_t *)src.contents + (by * bw + bx) * 8, (uint8_t *)dst.contents + (by * bw + bx) * 8, 8)) printf(" [L0 block %d,%d]", bx, by); }
        if (memcmp((uint8_t *)src.contents + offs[l], (uint8_t *)dst.contents + offs[l], n)) { any = 1; pos += snprintf(line + pos, sizeof(line) - pos, " L%d", l); }
    }
    if (any) printf("%s\n", line);
}
int main(void) { @autoreleasepool { id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); const char *f = getenv("FMT") ?: "BC1";
if (!strcmp(f, "BC1")) { FMT = MTLPixelFormatBC1_RGBA; BB = 8; }
else if (!strcmp(f, "BC7")) { FMT = MTLPixelFormatBC7_RGBAUnorm; BB = 16; }
else if (!strcmp(f, "ETC2")) { FMT = MTLPixelFormatETC2_RGB8; BB = 8; }
else if (!strcmp(f, "EAC")) { FMT = MTLPixelFormatEAC_R11Unorm; BB = 8; }
else if (!strcmp(f, "ASTC")) { FMT = MTLPixelFormatASTC_4x4_LDR; BB = 16; }
int bad = 0, total = 0;
for (int h = 4; h <= 140; h += 4) for (int w = 4; w <= 140; w += 4) { total++; run(dev, 1, w, h); }
(void)bad; (void)total; } return 0; }
