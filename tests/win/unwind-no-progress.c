/* An exception whose stack cannot be unwound must end the program, not hang it. Code without
 * unwind information is taken for a leaf function, and a leaf unwinds to its link register with
 * the stack pointer unchanged; when the link register points back into that code, every further
 * unwind gives the same pc and stack pointer again. Wine's ARM64 and ARM64EC dispatchers looped on
 * that frame forever (docs/findings.md #79: Subnautica, where an emulator bug sent the main thread
 * to a bad address). The child process runs such code: it sets the link register to its own last
 * instruction and faults. Exit 0 when the child ends within 20 seconds after faulting, 1 when it
 * hangs or did not run the test. Build:
 *   arm64ec-w64-mingw32-clang -O2 unwind-no-progress.c -o unwind-no-progress-ec.exe
 *   aarch64-w64-mingw32-clang -O2 unwind-no-progress.c -o unwind-no-progress-arm64.exe
 * Usage: unwind-no-progress.exe */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef MEM_EXTENDED_PARAMETER_EC_CODE
#define MEM_EXTENDED_PARAMETER_EC_CODE 0x00000040
#endif

static void *alloc_code(void)
{
#ifdef __arm64ec__
    /* code an ARM64EC program runs natively has to be marked as EC code */
    PVOID (WINAPI *pVirtualAlloc2)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
    MEM_EXTENDED_PARAMETER param = {0};

    pVirtualAlloc2 = (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "VirtualAlloc2");
    if (!pVirtualAlloc2) return NULL;
    param.Type = MemExtendedParameterAttributeFlags;
    param.ULong64 = MEM_EXTENDED_PARAMETER_EC_CODE;
    return pVirtualAlloc2(GetCurrentProcess(), NULL, 0x1000, MEM_RESERVE | MEM_COMMIT,
                          PAGE_EXECUTE_READWRITE, &param, 1);
#else
    return VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#endif
}

static int child(void)
{
    static const DWORD code[] =
    {
        0x1000007e, /* adr x30, #12  link register = the ret below */
        0xd2800000, /* mov x0, #0 */
        0xf9400000, /* ldr x0, [x0]  access violation */
        0xd65f03c0, /* ret */
    };
    void *mem = alloc_code();

    if (!mem) { printf("child: no executable memory (%lu)\n", GetLastError()); return 2; }
    memcpy(mem, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), mem, sizeof(code));
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    printf("child: faulting at %p with lr %p\n", (char *)mem + 8, (char *)mem + 12);
    fflush(stdout);
    ((void (*)(void))mem)();
    printf("child: returned\n");
    return 3;
}

int main(int argc, char **argv)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char cmdline[MAX_PATH + 16];
    DWORD code;

    if (argc > 1 && !strcmp(argv[1], "child")) return child();

    snprintf(cmdline, sizeof(cmdline), "\"%s\" child", argv[0]);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("FAIL CreateProcess %lu\n", GetLastError());
        return 1;
    }
    if (WaitForSingleObject(pi.hProcess, 20000) == WAIT_TIMEOUT)
    {
        printf("FAIL the child still runs after 20 s: the dispatcher loops on the frame\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    if (!GetExitCodeProcess(pi.hProcess, &code))
    {
        printf("FAIL GetExitCodeProcess %lu\n", GetLastError());
        return 1;
    }
    /* 2: the child could not set up its code, 3: the code returned instead of faulting. Either way
     * the unwinder was never exercised. */
    if (code == 2 || code == 3)
    {
        printf("FAIL the child did not run the test (exit %lu)\n", code);
        return 1;
    }
    printf("child exited with %#lx\nPASS\n", code);
    return 0;
}
