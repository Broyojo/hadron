/* Raises exceptions in native code running on a stack of the program's own, outside the thread's
 * stack limits, where the exception frame doesn't fit, and checks that the process then ends with
 * the exception's code, as on Windows, instead of hanging (docs/findings.md #80).
 *
 * FEX's ARM64EC frontend runs its JIT and its exception handling on such a stack, a 256K "emulator
 * stack" of its own. In a start of Geometry Dash with Geode, FEX's handler faulted again from
 * inside itself, a level deeper on that stack each time, until the stack ran out. Wine then wrote
 * the next exception frame below the stack from its signal handler; that store faulted with the
 * same signal, which is blocked while its handler runs, and macOS doesn't deliver it but runs the
 * store again: the thread stayed on it at full CPU and the game hung. When the frame's fault was a
 * different signal, Wine caught the nested fault and ended only the thread, and the other threads
 * waited for it for good. Windows ends the process when it can't write an exception frame.
 *
 * The program runs itself as a child for each case. A child starts an idle thread first, as a
 * process that ends when its last thread does would hide a thread that is ended alone.
 *   frame     fault with sp 256 bytes above the bottom of the stack: the frame doesn't fit
 *   nested    a vectored handler that faults again, on the stack, until it runs out (as FEX did)
 *   overflow  recursion that runs off the bottom of the stack (Geometry Dash's last fault)
 * A case fails when its child is still running after 20 s (it is then ended), returns, or ends
 * with another code than STATUS_ACCESS_VIOLATION.
 * Usage: fault-off-stack.exe [case], or fault-off-stack.exe child <case>.
 * Exit codes: 0 ok, 1 a case failed.
 * Build: aarch64-w64-mingw32-clang -O2 fault-off-stack.c -o fault-off-stack.exe */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifndef __aarch64__
#error "native ARM64 code: an x86 program's exceptions take other paths (FEX's, then Wine's emulated ones)"
#endif

#define STACK_SIZE 0x40000 /* the size of FEX's emulator stack */
#define GAP_SIZE 0x10000   /* reserved, not committed, below the stack: a write there faults */

/* Calls func on the stack whose top is sp; func doesn't return. */
__asm__(".text\n"
        ".globl run_on_stack\n"
        "run_on_stack:\n"
        "  mov sp, x0\n"
        "  mov x29, #0\n"
        "  blr x1\n"
        "  brk #0\n");
extern void run_on_stack(void *sp, void (*func)(void));

static volatile int sink;

static void fault(void)
{
    *(volatile int *)(uintptr_t)8 = 1;
}

static LONG CALLBACK nested_handler(EXCEPTION_POINTERS *e)
{
    /* runs on the faulting stack, below the frame of the exception it was called for */
    fault();
    return EXCEPTION_CONTINUE_SEARCH;
}

static __attribute__((noinline)) void recurse(int n)
{
    volatile char buf[512];
    buf[0] = n;
    recurse(n + 1);
    sink += buf[0];
}

static void overflow(void)
{
    recurse(0);
}

static DWORD WINAPI idle(void *arg)
{
    Sleep(INFINITE);
    return 0;
}

static int child(const char *name)
{
    char *base = VirtualAlloc(NULL, GAP_SIZE + STACK_SIZE, MEM_RESERVE, PAGE_NOACCESS), *stack;

    if (!base || !VirtualAlloc(base + GAP_SIZE, STACK_SIZE, MEM_COMMIT, PAGE_READWRITE))
    {
        printf("VirtualAlloc failed: %lu\n", GetLastError());
        return 2;
    }
    stack = base + GAP_SIZE;
    CloseHandle(CreateThread(NULL, 0, idle, NULL, 0, NULL));
    if (!strcmp(name, "frame")) run_on_stack(stack + 256, fault);
    else if (!strcmp(name, "nested"))
    {
        AddVectoredExceptionHandler(1, nested_handler);
        run_on_stack(stack + STACK_SIZE, fault);
    }
    else if (!strcmp(name, "overflow")) run_on_stack(stack + STACK_SIZE, overflow);
    else return 3;
    return 4;
}

static int run_case(const char *self, const char *name)
{
    char cmd[MAX_PATH + 64];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD wait, code = 0;
    int failed;

    snprintf(cmd, sizeof(cmd), "\"%s\" child %s", self, name);
    if (!CreateProcessA(self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("%-8s CreateProcess failed: %lu\n", name, GetLastError());
        return 1;
    }
    wait = WaitForSingleObject(pi.hProcess, 20000);
    if (wait == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    failed = wait == WAIT_TIMEOUT || code != EXCEPTION_ACCESS_VIOLATION;
    if (wait == WAIT_TIMEOUT) printf("%-8s HANG: still running after 20 s\n", name);
    else printf("%-8s ended with %08lx%s\n", name, code, failed ? ", not the access violation's code" : "");
    fflush(stdout);
    return failed;
}

int main(int argc, char **argv)
{
    static const char *cases[] = { "frame", "nested", "overflow" };
    char self[MAX_PATH];
    int i, failed = 0;

    if (argc > 2 && !strcmp(argv[1], "child")) return child(argv[2]);
    GetModuleFileNameA(NULL, self, sizeof(self));
    for (i = 0; i < 3; i++)
    {
        if (argc > 1 && strcmp(argv[1], cases[i])) continue;
        failed |= run_case(self, cases[i]);
    }
    printf(failed ? "FAILED\n" : "ok\n");
    fflush(stdout);
    return failed;
}
