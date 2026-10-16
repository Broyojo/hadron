/* Decommit one 4 KB page of a host page whose other pages are read-only or executable:
 * the cleared page must read back as zero and the neighbours keep their bytes and protection. */
#include <windows.h>
#include <stdio.h>

static int check(DWORD neighbour, const char *name)
{
    unsigned char *p = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    MEMORY_BASIC_INFORMATION info;
    DWORD old;
    memset(p, 0x5a, 65536);
    if (!VirtualProtect(p, 65536, neighbour, &old)) return 1;
    if (!VirtualFree(p + 4096, 4096, MEM_DECOMMIT)) return 2;
    if (VirtualAlloc(p + 4096, 4096, MEM_COMMIT, PAGE_READWRITE) != p + 4096) return 3;
    for (unsigned i = 0; i < 16384; i++)
    {
        unsigned char want = i >= 4096 && i < 8192 ? 0 : 0x5a;
        if (p[i] != want) { printf("FAIL %s: byte %u is %02x\n", name, i, p[i]); return 4; }
    }
    VirtualQuery(p, &info, sizeof(info));
    if (info.Protect != neighbour) { printf("FAIL %s: neighbour protection %lx\n", name, info.Protect); return 5; }
    p[4096] = 1; /* the recommitted page is writable */
    VirtualFree(p, 0, MEM_RELEASE);
    printf("PASS %s\n", name);
    return 0;
}

int main(void)
{
    return check(PAGE_READONLY, "read-only neighbours") || check(PAGE_EXECUTE_READ, "executable neighbours")
        || check(PAGE_NOACCESS, "no-access neighbours");
}
