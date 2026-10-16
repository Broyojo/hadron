/* Recommitting guest pages must zero them without changing committed neighbours. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int check(unsigned size, unsigned offset, unsigned length, DWORD protection, int whole)
{
    unsigned char *p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old;
    if (!p) return 1;
    memset(p, 0x5a, size);
    if (!VirtualProtect(p + offset, length, protection, &old)) return 2;
    if (!VirtualFree(p + offset, whole ? 0 : length, MEM_DECOMMIT)) return 3;
    if (VirtualAlloc(p + offset, length, MEM_COMMIT, PAGE_READWRITE) != p + offset) return 4;
    for (unsigned i = 0; i < size; ++i)
    {
        unsigned char want = i >= offset && i < offset + length ? 0 : 0x5a;
        if (p[i] != want)
        {
            printf("FAIL: size=%u offset=%u length=%u byte=%u got=%02x expected=%02x\n",
                   size, offset, length, i, p[i], want);
            VirtualFree(p, 0, MEM_RELEASE);
            return 5;
        }
    }
    /* Committing pages which are already committed must preserve their contents. */
    memset(p + offset, 0x3c, length);
    if (!VirtualAlloc(p + offset, length, MEM_COMMIT, PAGE_READWRITE)) return 6;
    for (unsigned i = offset; i < offset + length; ++i) if (p[i] != 0x3c) return 7;
    VirtualFree(p, 0, MEM_RELEASE);
    return 0;
}

int main(void)
{
    const unsigned ranges[][2] = {{0,4096}, {4096,4096}, {12288,4096}, {16384,16384},
                                  {4096,20480}, {12288,8192}, {61440,4096}, {0,65536}};
    for (unsigned j = 0; j < 2; ++j)
        for (unsigned i = 0; i < sizeof(ranges)/sizeof(ranges[0]); ++i)
        {
            int ret = check(65536, ranges[i][0], ranges[i][1], j ? PAGE_READONLY : PAGE_READWRITE, 0);
            if (ret) return ret;
        }
    if (check(4096, 0, 4096, PAGE_READWRITE, 1)) return 8;
    if (check(4096, 0, 4096, PAGE_READONLY, 1)) return 9;
    if (check(4096, 0, 4096, PAGE_EXECUTE_READ, 1)) return 10;
    puts("PASS: 19 decommit/recommit cases preserve neighbours and committed data");
    return 0;
}
