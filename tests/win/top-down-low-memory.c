/* A 64-bit program's MEM_TOP_DOWN reservations must not take the address space below 4 GB, which
 * code that needs 32-bit addresses depends on (issue #105: BELOW's LuaJIT asks for 128 MB below
 * 2 GB with ZeroBits after FEX's allocator has reserved large blocks top-down). Windows gives a
 * top-down allocation the highest free address. Reserves eight 512 MB blocks top-down, checks that
 * each lies above 4 GB, then allocates 128 MB below 2 GB as LuaJIT does. A top-down reservation
 * whose upper bound is below 4 GB must still be placed there. Exit 0 when all hold. Build:
 *   x86_64-w64-mingw32-clang -O2 top-down-low-memory.c -o top-down-low-memory.exe
 * Usage: top-down-low-memory.exe */
#include <windows.h>
#include <stdio.h>

NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE, PVOID *, ULONG_PTR, SIZE_T *, ULONG, ULONG);

#define BLOCK (512u << 20)
#define LOW_4G 0x100000000ull

int main(void)
{
    MEM_ADDRESS_REQUIREMENTS req = {0};
    MEM_EXTENDED_PARAMETER param = {0};
    SIZE_T size = 128u << 20;
    void *low = NULL, *p;
    NTSTATUS status;
    int i, failed = 0;

    for (i = 0; i < 8; i++)
    {
        p = VirtualAlloc(NULL, BLOCK, MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
        printf("top-down %d: %p\n", i, p);
        if (!p) { printf("FAIL top-down reservation %d: error %lu\n", i, GetLastError()); return 1; }
        if ((ULONG_PTR)p < LOW_4G) { printf("FAIL top-down reservation %d below 4 GB\n", i); failed = 1; }
    }

    /* LuaJIT's allocator: ZeroBits 1 keeps the block below 2 GB. */
    status = NtAllocateVirtualMemory(GetCurrentProcess(), &low, 1, &size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    printf("ZeroBits 1, 128 MB: status %#lx, %p\n", status, low);
    if (status || (ULONG_PTR)low + size > 0x80000000ull) { printf("FAIL allocation below 2 GB\n"); failed = 1; }

    /* A bound below 4 GB still applies to a top-down request. */
    req.HighestEndingAddress = (void *)0xffffffffull;
    param.Type = MemExtendedParameterAddressRequirements;
    param.Pointer = &req;
    p = VirtualAlloc2(NULL, NULL, 64u << 20, MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS, &param, 1);
    printf("top-down below 4 GB: %p\n", p);
    if (!p || (ULONG_PTR)p + (64u << 20) > LOW_4G) { printf("FAIL bounded top-down reservation\n"); failed = 1; }

    printf(failed ? "FAIL\n" : "PASS\n");
    return failed;
}
