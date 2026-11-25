/* A 32-bit program's VirtualAlloc2 with an address requirement that names no highest address must
 * get memory below its own limit, as on 64-bit Windows: a 32-bit pointer cannot hold any other.
 * snmalloc, the allocator of mtld3d's d3d9.dll, reserves its memory so, with only an alignment
 * (findings #78). Wine's WoW64 layer passed the empty limit on to the 64-bit ntdll, which took it
 * as no limit, so the reservation could land above 4 GB and the program was handed its low 32 bits.
 * Wine 11.22 made that the rule once the process has started enough threads: each takes some
 * 64-bit memory above 4 GB, and once Wine has had to reserve more for it, bottom-up searches
 * without a limit start in the new reservation.
 * Starts 128 threads first, then reserves with alignment only, with an empty requirement, and
 * top-down, and checks that each block is where the returned pointer says and can be committed.
 * Exit 0 when all hold. Build:
 *   i686-w64-mingw32-clang -O2 wow64-address-requirements.c -o wow64-address-requirements.exe
 * Usage: wow64-address-requirements.exe */
#include <windows.h>
#include <stdio.h>

typedef void *(WINAPI *alloc2_fn)(HANDLE, void *, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);

#define THREADS 128

static HANDLE release;

static DWORD WINAPI waiter(void *unused)
{
    WaitForSingleObject(release, INFINITE);
    return 0;
}

static int check(alloc2_fn alloc2, const char *name, SIZE_T size, SIZE_T alignment, ULONG top_down)
{
    MEM_ADDRESS_REQUIREMENTS req = {0};
    MEM_EXTENDED_PARAMETER param = {0};
    MEMORY_BASIC_INFORMATION info;
    char *ptr;

    req.Alignment = alignment;
    param.Type = MemExtendedParameterAddressRequirements;
    param.Pointer = &req;
    ptr = alloc2(GetCurrentProcess(), NULL, size, MEM_RESERVE | top_down, PAGE_READWRITE, &param, 1);
    if (!ptr)
    {
        printf("FAIL %s: VirtualAlloc2 error %lu\n", name, GetLastError());
        return 1;
    }
    if (!VirtualQuery(ptr, &info, sizeof(info)) || info.AllocationBase != ptr || info.State != MEM_RESERVE)
    {
        printf("FAIL %s: got %p, but Wine has no reservation there (state %#lx, base %p)\n",
               name, ptr, info.State, info.AllocationBase);
        return 1;
    }
    if (alignment && ((ULONG_PTR)ptr & (alignment - 1)))
    {
        printf("FAIL %s: %p is not aligned to %#lx\n", name, ptr, (ULONG)alignment);
        return 1;
    }
    if (!VirtualAlloc(ptr + size - 4096, 4096, MEM_COMMIT, PAGE_READWRITE))
    {
        printf("FAIL %s: committing the end of %p failed, error %lu\n", name, ptr, GetLastError());
        return 1;
    }
    ptr[size - 1] = 1;
    VirtualFree(ptr, 0, MEM_RELEASE);
    printf("ok %s: %p\n", name, ptr);
    return 0;
}

int main(void)
{
    alloc2_fn alloc2 = (alloc2_fn)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "VirtualAlloc2");
    HANDLE threads[THREADS];
    int i, failed = 0;

    if (!alloc2) { printf("FAIL: no VirtualAlloc2\n"); return 1; }
    release = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < THREADS; i++)
        if (!(threads[i] = CreateThread(NULL, 0, waiter, NULL, 0, NULL)))
        {
            printf("FAIL: CreateThread %d error %lu\n", i, GetLastError());
            return 1;
        }

    failed |= check(alloc2, "aligned 4 MB, no highest address", 4 << 20, 4 << 20, 0);
    failed |= check(alloc2, "aligned 64 KB, no highest address", 1 << 16, 1 << 16, 0);
    failed |= check(alloc2, "empty requirement", 1 << 20, 0, 0);
    failed |= check(alloc2, "top-down, no highest address", 1 << 20, 0, MEM_TOP_DOWN);

    SetEvent(release);
    WaitForMultipleObjects(THREADS / 2, threads, TRUE, 30000);
    WaitForMultipleObjects(THREADS - THREADS / 2, threads + THREADS / 2, TRUE, 30000);
    printf("%s\n", failed ? "FAIL" : "PASS");
    return failed;
}
