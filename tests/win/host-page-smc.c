/* x86 JIT engines can patch code without calling FlushInstructionCache.
 * Exercise tracking when several Windows pages share one native host page. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void initialize(unsigned char *p, int value)
{
    const unsigned char code[] = {0xb8, 0, 0, 0, 0, 0xc3}; /* mov eax, value; ret */
    memcpy(p, code, sizeof(code));
    p[1] = value;
}

static int invoke(unsigned char *p)
{
    return ((int (*)(void))p)();
}

int main(void)
{
    unsigned char *p = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_EXECUTE_READWRITE);
    DWORD old;
    MEMORY_BASIC_INFORMATION info;
    if (!p) return 1;
    for (int page = 0; page < 8; ++page) initialize(p + page * 4096, 1);
    for (int page = 0; page < 8; ++page) if (invoke(p + page * 4096) != 1) return 2;

    /* The first update unprotects a host page; sibling translations must go too. */
    for (int page = 3; page >= 0; --page)
        *(volatile unsigned char *)(p + page * 4096 + 1) = 2;
    for (int page = 0; page < 4; ++page)
        if (invoke(p + page * 4096) != 2)
        {
            printf("FAIL: sibling page %d retained old code\n", page);
            return 3;
        }
    for (int value = 3; value < 100; ++value)
    {
        *(volatile unsigned char *)(p + 1) = value;
        if (invoke(p) != value)
        {
            printf("FAIL: update %d retained old code\n", value);
            return 4;
        }
    }

    /* Exercise an instruction which crosses a native 16K page boundary. */
    initialize(p + 16381, 1);
    if (invoke(p + 16381) != 1) return 5;
    *(volatile unsigned char *)(p + 16382) = 2;
    if (invoke(p + 16381) != 2) return 6;

    /* Genuine protection changes must survive temporary tracking protections. */
    if (!VirtualProtect(p + 4096, 4096, PAGE_EXECUTE_READ, &old)) return 7;
    *(volatile unsigned char *)(p + 1) = 100;
    if (invoke(p) != 100) return 8;
    if (!VirtualQuery(p + 4096, &info, sizeof(info)) || info.Protect != PAGE_EXECUTE_READ)
    {
        puts("FAIL: changed neighbour protection");
        return 9;
    }
    VirtualFree(p, 0, MEM_RELEASE);

    p = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!p) return 10;
    initialize(p, 1);
    if (invoke(p) != 1) return 11;
    *(volatile unsigned char *)(p + 1) = 2;
    if (invoke(p) != 2) return 12;
    VirtualFree(p, 0, MEM_RELEASE);
    puts("PASS: updates, siblings, crossing instruction, neighbour protection and small view");
    return 0;
}
