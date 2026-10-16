#if !defined(__aarch64__)
#error This protection probe requires native Windows ARM64.
#endif
/* ARM64 neighbour-protection probe; assemble attempt below as one store and a return. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
static volatile LONG stop, successes, faults;
static unsigned char *neighbour;
static int (*code)(void);
static volatile LONG bad_code;
extern void attempt(unsigned char *, unsigned);
static LONG CALLBACK on_fault(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        e->ExceptionRecord->ExceptionInformation[1] != (ULONG_PTR)neighbour ||
        e->ContextRecord->Pc != (ULONG_PTR)attempt)
        return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&faults);
    e->ContextRecord->Pc += 4;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static DWORD WINAPI writer(void *p)
{
    while (!InterlockedCompareExchange(&stop, 0, 0))
    {
        if (code && code() != 42)
            InterlockedIncrement(&bad_code);
        LONG before = InterlockedCompareExchange(&faults, 0, 0);
        attempt(p, 0xa5);
        if (before == InterlockedCompareExchange(&faults, 0, 0))
            InterlockedIncrement(&successes);
    }
    return 0;
}
int main(int argc, char **argv)
{
    (void)argv;
    DWORD protection = argc > 1 ? PAGE_EXECUTE_READ : PAGE_READONLY;
    unsigned char *p = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old;
    if (!p)
        return 1;
    memset(p, 0x5a, 65536);
    neighbour = p + 16;
    if (argc > 1)
    {
        unsigned int insns[] = {0x52800540, 0xd65f03c0};
        memcpy(p, insns, sizeof(insns));
        code = (void *)p;
        FlushInstructionCache(GetCurrentProcess(), p, sizeof(insns));
    }
    PVOID handler = AddVectoredExceptionHandler(1, on_fault);
    if (!handler)
        return 7;
    if (!VirtualProtect(p, 65536, protection, &old))
        return 2;
    HANDLE thread = CreateThread(NULL, 0, writer, neighbour, 0, NULL);
    if (!thread)
        return 3;
    for (int wait = 0; !faults && wait < 1000; wait++)
        Sleep(1);
    if (!faults)
        return 8;
    for (int i = 0; i < 100; i++)
    {
        if (!VirtualFree(p + 4096, 4096, MEM_DECOMMIT))
            return 4;
        if (VirtualAlloc(p + 4096, 4096, MEM_COMMIT, protection) != p + 4096)
            return 5;
    }
    InterlockedExchange(&stop, 1);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    printf("protected writes succeeded=%ld faults=%ld neighbour=%02x bad_code=%ld\n", successes, faults, *neighbour,
           bad_code);
    int result = successes || *neighbour != 0x5a || bad_code;
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(p, 0, MEM_RELEASE);
    return result ? 10 : 0;
}
