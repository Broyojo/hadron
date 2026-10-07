#include <windows.h>
#include <stdio.h>

static volatile LONG stop;
static LARGE_INTEGER frequency;

static double elapsed_ms(LARGE_INTEGER start, LARGE_INTEGER end)
{
    return (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
}

static DWORD WINAPI query_worker(void *unused)
{
    MEMORY_BASIC_INFORMATION info;
    LARGE_INTEGER start, end;
    double worst = 0;
    unsigned int calls = 0;
    while (!InterlockedCompareExchange(&stop, 0, 0))
    {
        QueryPerformanceCounter(&start);
        if (!VirtualQuery((void *)&stop, &info, sizeof(info))) return 1;
        QueryPerformanceCounter(&end);
        if (elapsed_ms(start,end) > worst) worst = elapsed_ms(start,end);
        calls++;
        Sleep(1);
    }
    printf("VirtualQuery calls=%u worst_ms=%.3f\n", calls, worst);
    return 0;
}

static DWORD WINAPI short_worker(void *unused)
{
    Sleep(20);
    return 0;
}

static int check_address_requirements(void)
{
    typedef void *(WINAPI *alloc2_fn)(HANDLE, void *, SIZE_T, ULONG, ULONG,
                                     MEM_EXTENDED_PARAMETER *, ULONG);
    alloc2_fn alloc2 = (alloc2_fn)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "VirtualAlloc2");
    MEM_ADDRESS_REQUIREMENTS limits = {0};
    MEM_EXTENDED_PARAMETER parameter = {0};
    void *ptr;
    SIZE_T size = 65536;
    if (!alloc2) return 1;
    parameter.Type = MemExtendedParameterAddressRequirements;
    parameter.Pointer = &limits;
    limits.Alignment = 2 * 1024 * 1024;

    for (unsigned int top_down = 0; top_down < 2; top_down++)
    {
        /* A bounded gap above the macOS guard, outside Wine's high reservation. */
        limits.LowestStartingAddress = (void *)0x7000000000ULL;
        limits.HighestEndingAddress = (void *)0x73ffffffffULL;
        ptr = alloc2(GetCurrentProcess(), NULL, size,
                     MEM_RESERVE | MEM_COMMIT | (top_down ? MEM_TOP_DOWN : 0),
                     PAGE_READWRITE, &parameter, 1);
        if (!ptr || (ULONG_PTR)ptr < (ULONG_PTR)limits.LowestStartingAddress ||
            (ULONG_PTR)ptr + size - 1 > (ULONG_PTR)limits.HighestEndingAddress ||
            ((ULONG_PTR)ptr & (limits.Alignment - 1)))
        { printf("Bounded allocation failed direction=%u error=%lu ptr=%p\n", top_down, GetLastError(), ptr); return 1; }
        *(volatile DWORD *)ptr = 0xabcdef;
        if (*(volatile DWORD *)ptr != 0xabcdef || !VirtualFree(ptr, 0, MEM_RELEASE)) return 1;

        /* A window wholly inside the occupied guard must fail within its bounds. */
        limits.LowestStartingAddress = (void *)0x1000200000ULL;
        limits.HighestEndingAddress = (void *)0x10005fffffULL;
        ptr = alloc2(GetCurrentProcess(), NULL, size,
                     MEM_RESERVE | (top_down ? MEM_TOP_DOWN : 0),
                     PAGE_NOACCESS, &parameter, 1);
        if (ptr)
        { printf("Allocation escaped occupied bounds direction=%u ptr=%p\n", top_down, ptr); return 1; }
    }
    printf("PASS bounded allocations: both directions, 2 MiB alignment, occupied windows\n");
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE queries[2], thread;
    LARGE_INTEGER start, end;
    unsigned int i;
    DWORD code;
    QueryPerformanceFrequency(&frequency);
    /* Reproduce exhaustion of Wine's small high-address reservation without
       committing RAM. Keep the regions alive until this process exits. */
    if (argc > 1)
    {
        ULONG_PTR cursor = 0x7ffff0000000ULL, limit = 0x7ffffe000000ULL;
        MEMORY_BASIC_INFORMATION info;
        SIZE_T reserved = 0;
        while (cursor < limit)
        {
            ULONG_PTR end;
            if (!VirtualQuery((void *)cursor, &info, sizeof(info))) return 1;
            end = (ULONG_PTR)info.BaseAddress + info.RegionSize;
            if (end > limit) end = limit;
            if (end <= cursor) return 1;
            if (info.State == MEM_FREE)
            {
                ULONG_PTR aligned = (cursor + 65535) & ~(ULONG_PTR)65535;
                ULONG_PTR chunk_end = end & ~(ULONG_PTR)65535;
                if (chunk_end > aligned + 1048576) chunk_end = aligned + 1048576;
                if (chunk_end > aligned)
                {
                    if (VirtualAlloc((void *)aligned, chunk_end-aligned, MEM_RESERVE, PAGE_NOACCESS))
                        reserved += chunk_end-aligned;
                    else printf("reservation skipped base=%p size=%zu error=%lu\n",
                                (void *)aligned, chunk_end-aligned, GetLastError());
                    end = chunk_end;
                }
            }
            cursor = end;
        }
        printf("Reserved high-address gaps bytes=%zu, with no committed RAM\n", reserved);
        fflush(stdout);
    }
    if (check_address_requirements()) return 1;
    for (i = 0; i < 2; i++)
        if (!(queries[i] = CreateThread(NULL, 0, query_worker, NULL, 0, NULL))) return 1;
    for (i = 0; i < (argc > 1 ? 3 : 12); i++)
    {
        QueryPerformanceCounter(&start);
        thread = CreateThread(NULL, 0, short_worker, NULL, 0, NULL);
        QueryPerformanceCounter(&end);
        if (!thread) { printf("CreateThread failed=%lu\n", GetLastError()); return 1; }
        printf("CreateThread iteration=%u elapsed_ms=%.3f\n", i, elapsed_ms(start,end));
        fflush(stdout);
        if (WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0) return 1;
        GetExitCodeThread(thread, &code);
        CloseHandle(thread);
        if (code) return 1;
    }
    InterlockedExchange(&stop, 1);
    if (WaitForMultipleObjects(2, queries, TRUE, 30000) != WAIT_OBJECT_0) return 1;
    for (i = 0; i < 2; i++)
    {
        GetExitCodeThread(queries[i], &code);
        CloseHandle(queries[i]);
        if (code) return 1;
    }
    return 0;
}
