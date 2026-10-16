/* Time decommit + recommit + touch of 4 KB pages that do and don't fill a 16 KB host page. */
#include <windows.h>
#include <stdio.h>

/* alone: only the measured range is committed, so its host page has nothing else in use. */
static double run(unsigned offset, unsigned length, int iterations, int alone)
{
    LARGE_INTEGER f, a, b;
    unsigned char *p = VirtualAlloc(NULL, 65536, alone ? MEM_RESERVE : MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (alone) VirtualAlloc(p + offset, length, MEM_COMMIT, PAGE_READWRITE);
    else memset(p, 0x5a, 65536);
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    for (int i = 0; i < iterations; i++)
    {
        VirtualFree(p + offset, length, MEM_DECOMMIT);
        VirtualAlloc(p + offset, length, MEM_COMMIT, PAGE_READWRITE);
        p[offset] = 1;
    }
    QueryPerformanceCounter(&b);
    VirtualFree(p, 0, MEM_RELEASE);
    return (double)(b.QuadPart - a.QuadPart) * 1e6 / f.QuadPart / iterations;
}

int main(void)
{
    const int n = 20000;
    run(4096, 4096, 1000, 0); /* warm up */
    for (int r = 0; r < 3; r++)
        printf("4 KB inside a host page: %.2f us   16 KB whole host page: %.2f us   36 KB unaligned: %.2f us   4 KB alone in its host page: %.2f us\n",
               run(4096, 4096, n, 0), run(16384, 16384, n, 0), run(4096, 36864, n, 0), run(4096, 4096, n, 1));
    return 0;
}
