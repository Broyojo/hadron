/* hadron-icon: write the icon of a Windows executable as a Mac .icns file.
 *
 * Steam's shortcut for a game (~/Applications/<name>.app, what Spotlight and Launchpad show)
 * carries Steam's own icon when the game has no Mac icon, which is every Windows game. The icon
 * Windows shows for a program is the first icon group in its resources: this takes the largest
 * image of that group and writes it at the sizes an .icns holds.
 *
 * Usage: hadron-icon <game.exe> <out.icns>
 *        hadron-icon --launch-exe <appinfo.vdf> <appid>
 *
 * The second form prints the executable Steam launches for a game on Windows, relative to the
 * game's directory, from Steam's cache of application data (appcache/appinfo.vdf): which of a
 * game's executables is the game is Steam's knowledge, not something to guess from file names.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define RT_ICON 3
#define RT_GROUP_ICON 14

struct pe {
    const uint8_t *data;
    size_t size;
    const uint8_t *sections;
    unsigned section_count;
    size_t rsrc; /* file offset of the resource directory */
};

static int in_range(const struct pe *pe, size_t off, size_t len)
{
    return off <= pe->size && len <= pe->size - off;
}

static uint16_t u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* File offset of a relative virtual address, or 0 when no section holds it. */
static size_t rva_offset(const struct pe *pe, uint32_t rva)
{
    for (unsigned i = 0; i < pe->section_count; i++) {
        const uint8_t *s = pe->sections + i * 40;
        uint32_t vsize = u32(s + 8), va = u32(s + 12), raw_size = u32(s + 16), raw = u32(s + 20);
        if (rva >= va && rva - va < (vsize > raw_size ? vsize : raw_size))
            return rva - va < raw_size ? (size_t)raw + (rva - va) : 0;
    }
    return 0;
}

static int pe_open(struct pe *pe)
{
    if (!in_range(pe, 0, 0x40) || u16(pe->data) != 0x5a4d) return 0;
    size_t nt = u32(pe->data + 0x3c);
    if (!in_range(pe, nt, 24) || u32(pe->data + nt) != 0x4550) return 0;
    pe->section_count = u16(pe->data + nt + 6);
    size_t opt = nt + 24, opt_size = u16(pe->data + nt + 20);
    if (!in_range(pe, opt, opt_size) || opt_size < 2) return 0;
    uint16_t magic = u16(pe->data + opt);
    size_t dirs = opt + (magic == 0x20b ? 112 : 96); /* PE32+ or PE32 */
    if (dirs + 3 * 8 > opt + opt_size) return 0;
    if (!in_range(pe, opt + opt_size, (size_t)pe->section_count * 40)) return 0;
    pe->sections = pe->data + opt + opt_size;
    pe->rsrc = rva_offset(pe, u32(pe->data + dirs + 2 * 8));
    return pe->rsrc != 0;
}

/* The entry of a resource directory with the given id, or its first entry when id is negative:
 * the offset its entry points at, relative to the resource directory, with the high bit set for a
 * subdirectory. Returns 0 when there is none. */
static uint32_t dir_entry(const struct pe *pe, uint32_t dir, int id)
{
    size_t base = pe->rsrc + dir;
    if (!in_range(pe, base, 16)) return 0;
    unsigned named = u16(pe->data + base + 12), ids = u16(pe->data + base + 14);
    if (!in_range(pe, base + 16, (size_t)(named + ids) * 8)) return 0;
    for (unsigned i = 0; i < named + ids; i++) {
        const uint8_t *e = pe->data + base + 16 + i * 8;
        if (id < 0 || (i >= named && u32(e) == (uint32_t)id)) return u32(e + 4);
    }
    return 0;
}

/* The data of resource type/id in its first language; id < 0 takes the type's first resource. */
static const uint8_t *resource(const struct pe *pe, int type, int id, uint32_t *size)
{
    uint32_t e = dir_entry(pe, 0, type);
    if (!(e & 0x80000000)) return NULL;
    e = dir_entry(pe, e & 0x7fffffff, id);
    if (!(e & 0x80000000)) return NULL;
    e = dir_entry(pe, e & 0x7fffffff, -1);
    if (!e || (e & 0x80000000) || !in_range(pe, pe->rsrc + e, 16)) return NULL;
    const uint8_t *entry = pe->data + pe->rsrc + e;
    size_t off = rva_offset(pe, u32(entry));
    *size = u32(entry + 4);
    if (!off || !in_range(pe, off, *size)) return NULL;
    return pe->data + off;
}

/* The largest image of the executable's first icon group, as an .ico file holding just it. */
static CFDataRef best_icon(const struct pe *pe, unsigned *width)
{
    uint32_t group_size, icon_size;
    const uint8_t *group = resource(pe, RT_GROUP_ICON, -1, &group_size);
    if (!group || group_size < 6) return NULL;
    unsigned count = u16(group + 4);
    if (group_size < 6 + count * 14) return NULL;

    const uint8_t *best = NULL;
    unsigned best_width = 0, best_bits = 0;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = group + 6 + i * 14;
        unsigned w = e[0] ? e[0] : 256, bits = u16(e + 6);
        if (w > best_width || (w == best_width && bits > best_bits)) {
            best = e; best_width = w; best_bits = bits;
        }
    }
    if (!best) return NULL;
    const uint8_t *icon = resource(pe, RT_ICON, u16(best + 12), &icon_size);
    if (!icon) return NULL;

    /* ICONDIR, then one ICONDIRENTRY: the group's entry with the image's offset for its id. */
    uint8_t header[22] = { 0, 0, 1, 0, 1, 0 };
    memcpy(header + 6, best, 12);
    header[14] = icon_size; header[15] = icon_size >> 8; header[16] = icon_size >> 16; header[17] = icon_size >> 24;
    header[18] = sizeof(header);
    CFMutableDataRef ico = CFDataCreateMutable(NULL, 0);
    CFDataAppendBytes(ico, header, sizeof(header));
    CFDataAppendBytes(ico, icon, icon_size);
    *width = best_width;
    return ico;
}

static CGImageRef scaled(CGImageRef image, size_t size)
{
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(NULL, size, size, 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (!ctx) return NULL;
    /* A small icon enlarged by a whole factor keeps its pixels; anything else is filtered. */
    size_t from = CGImageGetWidth(image);
    CGContextSetInterpolationQuality(ctx, size > from && size % from == 0 ? kCGInterpolationNone : kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, size, size), image);
    CGImageRef out = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return out;
}

static const uint8_t *map_file(const char *path, size_t *size)
{
    struct stat st;
    int fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &st) < 0) { perror(path); return NULL; }
    const uint8_t *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) { perror(path); return NULL; }
    *size = st.st_size;
    return data;
}

/* A string of at most `end - p` bytes with its terminator, or NULL. */
static const char *cstring(const uint8_t *p, const uint8_t *end)
{
    return p < end && memchr(p, 0, end - p) ? (const char *)p : NULL;
}

/* appinfo.vdf (version 29): a header, then one record per application, each holding a binary
 * key-value tree whose keys are indices into a string table at the end of the file. The launch
 * options are appinfo/config/launch/<n>, each with an "executable" and, when it is for some
 * systems only, config/oslist. Prints the first one that is for Windows. */
static int launch_exe(const char *path, uint32_t appid)
{
    size_t size;
    const uint8_t *data = map_file(path, &size);
    if (!data) return 1;
    const uint8_t *end = data + size;
    if (size < 16 || u32(data) != 0x07564429) { fprintf(stderr, "%s: not an appinfo.vdf of version 29\n", path); return 1; }
    uint64_t table = u32(data + 8) | (uint64_t)u32(data + 12) << 32;
    if (table > size - 4) return 1;
    uint32_t key_count = u32(data + table);
    const char **keys = calloc(key_count ? key_count : 1, sizeof(*keys));
    const uint8_t *k = data + table + 4;
    for (uint32_t i = 0; i < key_count; i++) {
        if (!(keys[i] = cstring(k, end))) return 1;
        k += strlen(keys[i]) + 1;
    }

    const uint8_t *p = data + 16, *tree = NULL, *tree_end = NULL;
    while (p + 8 <= data + table && u32(p)) {
        uint32_t id = u32(p), len = u32(p + 4);
        if (len < 60 || len > (size_t)(data + table - p - 8)) return 1;
        if (id == appid) { tree = p + 8 + 60; tree_end = p + 8 + len; break; }
        p += 8 + len;
    }
    if (!tree) { fprintf(stderr, "%s: no application %u\n", path, appid); return 1; }

    const char *stack[32], *exe = NULL, *oslist = NULL;
    int depth = 0;
    for (p = tree; p < tree_end; ) {
        uint8_t type = *p++;
        if (type == 0x08 || type == 0x0b) {
            if (--depth < 0) break;
            /* Leaving appinfo/config/launch/<n>: take it if it is for Windows. */
            if (depth == 3 && !strcmp(stack[0], "appinfo") && !strcmp(stack[1], "config") && !strcmp(stack[2], "launch")) {
                if (exe && (!oslist || strstr(oslist, "windows"))) {
                    for (const char *c = exe; *c; c++) putchar(*c == '\\' ? '/' : *c);
                    putchar('\n');
                    return 0;
                }
                exe = oslist = NULL;
            }
            continue;
        }
        if (p + 4 > tree_end || u32(p) >= key_count) return 1;
        const char *key = keys[u32(p)];
        p += 4;
        switch (type) {
        case 0x00:
            if (depth >= 32) return 1;
            stack[depth++] = key;
            break;
        case 0x01: {
            const char *value = cstring(p, tree_end);
            if (!value) return 1;
            p += strlen(value) + 1;
            int in_launch = depth >= 4 && !strcmp(stack[0], "appinfo") && !strcmp(stack[1], "config") && !strcmp(stack[2], "launch");
            if (in_launch && depth == 4 && !strcmp(key, "executable")) exe = value;
            if (in_launch && depth == 5 && !strcmp(stack[4], "config") && !strcmp(key, "oslist")) oslist = value;
            break;
        }
        case 0x02: case 0x03: case 0x04: case 0x06: p += 4; break;
        case 0x07: case 0x0a: p += 8; break;
        case 0x05:
            while (p + 2 <= tree_end && (p[0] || p[1])) p += 2;
            p += 2;
            break;
        default: return 1;
        }
    }
    fprintf(stderr, "%s: application %u has no Windows executable\n", path, appid);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--launch-exe"))
        return launch_exe(argv[2], strtoul(argv[3], NULL, 10));
    if (argc != 3) {
        fprintf(stderr, "usage: %s <game.exe> <out.icns>\n       %s --launch-exe <appinfo.vdf> <appid>\n", argv[0], argv[0]);
        return 2;
    }
    struct pe pe = { 0 };
    pe.data = map_file(argv[1], &pe.size);
    if (!pe.data) return 1;

    unsigned width = 0;
    CFDataRef ico = pe_open(&pe) ? best_icon(&pe, &width) : NULL;
    if (!ico) { fprintf(stderr, "%s: no icon\n", argv[1]); return 1; }
    CGImageSourceRef source = CGImageSourceCreateWithData(ico, NULL);
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
    if (!image) { fprintf(stderr, "%s: icon cannot be read\n", argv[1]); return 1; }

    /* The sizes an .icns holds, up to the icon's own; a small icon is still enlarged to 128. */
    static const size_t sizes[] = { 16, 32, 128, 256, 512 };
    size_t count = 0;
    while (count < sizeof(sizes) / sizeof(sizes[0]) && (sizes[count] <= 128 || sizes[count] <= width)) count++;

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)argv[2], strlen(argv[2]), false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("com.apple.icns"), count, NULL);
    if (!dest) { fprintf(stderr, "%s: cannot be written\n", argv[2]); return 1; }
    for (size_t i = 0; i < count; i++) {
        CGImageRef s = scaled(image, sizes[i]);
        if (!s) return 1;
        CGImageDestinationAddImage(dest, s, NULL);
        CGImageRelease(s);
    }
    if (!CGImageDestinationFinalize(dest)) { fprintf(stderr, "%s: cannot be written\n", argv[2]); return 1; }
    return 0;
}
