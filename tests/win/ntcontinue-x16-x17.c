/* Continues to contexts whose x16 and x17 aren't their pc and sp, from several threads at once, and
 * checks that the code continued to sees the context's x16 and x17 (docs/findings.md #80).
 *
 * Wine's arm64 syscall dispatcher returns to such a context through a signal: it raises SIGUSR2,
 * whose handler switches to the context. Had the handler not run by the time pthread_kill()
 * returned, its fast return went ahead and left x16 = pc and x17 = sp. That happened in Geometry
 * Dash with Geode a few times per start under lldb and about once in 40 starts without it; FEX's
 * ARM64EC JIT keeps its call-ret stack pointer in x17 and resumes from its exceptions with
 * NtContinue, and ran on with x17 pointing at its own stack. Wine 0057 keeps signaling until the
 * handler has run. This test hasn't caught the late signal itself (not even under lldb); it checks
 * that the return still restores x16 and x17, with the fix as without it.
 * Native ARM64 code (an x86 program would continue through FEX's emulated contexts).
 * Usage: ntcontinue-x16-x17.exe [seconds=5] [threads=4]
 * Exit codes: 0 ok, 1 a continue arrived with other x16 or x17.
 * Build: aarch64-w64-mingw32-clang -O2 ntcontinue-x16-x17.c -o ntcontinue-x16-x17.exe -lntdll */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winternl.h>

#ifndef __aarch64__
#error "native ARM64 code"
#endif

NTSTATUS WINAPI NtContinue(CONTEXT *, BOOLEAN);

static volatile LONG stop;
static volatile LONG64 iters, bad;

/* where each continue goes back to, and what it saw */
struct rec { uint64_t got16, got17, resume_pc, resume_sp; };

/* the context's pc: x0 = the rec; stores x16 and x17, then goes back into do_continue() */
__asm__(".text\n"
        ".globl landing\n"
        "landing:\n"
        "stp x16, x17, [x0]\n"
        "ldr x1, [x0, #16]\n"
        "ldr x2, [x0, #24]\n"
        "mov sp, x2\n"
        "br x1\n");
extern void landing(void);

/* saves the callee-saved registers and where to come back to in the rec, then continues to c */
__asm__(".text\n"
        ".globl do_continue\n"
        "do_continue:\n"
        "stp x29, x30, [sp, #-0x60]!\n"
        "stp x19, x20, [sp, #0x10]\n"
        "stp x21, x22, [sp, #0x20]\n"
        "stp x23, x24, [sp, #0x30]\n"
        "stp x25, x26, [sp, #0x40]\n"
        "stp x27, x28, [sp, #0x50]\n"
        "adr x2, 1f\n"
        "str x2, [x0, #16]\n"
        "mov x2, sp\n"
        "str x2, [x0, #24]\n"
        "mov x0, x1\n"
        "mov x1, #0\n"
        "bl NtContinue\n"
        "brk #1\n"
        "1:\n"
        "ldp x19, x20, [sp, #0x10]\n"
        "ldp x21, x22, [sp, #0x20]\n"
        "ldp x23, x24, [sp, #0x30]\n"
        "ldp x25, x26, [sp, #0x40]\n"
        "ldp x27, x28, [sp, #0x50]\n"
        "ldp x29, x30, [sp], #0x60\n"
        "ret\n");
extern void do_continue(struct rec *r, CONTEXT *c);

static __attribute__((aligned(16))) char stacks[16][8192];

static DWORD WINAPI worker(void *arg)
{
    uint64_t k = (uintptr_t)arg << 40;
    char *stack = stacks[(uintptr_t)arg & 15] + sizeof(stacks[0]);
    struct rec r;
    CONTEXT c;

    while (!stop)
    {
        uint64_t a = 0x1600000000000000ull | k, b = 0x1700000000000000ull | k;

        RtlCaptureContext(&c);
        c.ContextFlags = CONTEXT_FULL;
        c.Pc = (DWORD64)landing;
        c.Sp = (DWORD64)stack;
        c.X0 = (DWORD64)&r;
        c.X16 = a;
        c.X17 = b;
        do_continue(&r, &c);
        if (r.got16 != a || r.got17 != b)
        {
            if (InterlockedIncrement64(&bad) < 10)
                printf("BAD: x16 %llx (want %llx), x17 %llx (want %llx)\n", (unsigned long long)r.got16,
                       (unsigned long long)a, (unsigned long long)r.got17, (unsigned long long)b);
        }
        InterlockedIncrement64(&iters);
        k++;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 5, threads = argc > 2 ? atoi(argv[2]) : 4, i;

    if (threads > 16) threads = 16;
    for (i = 0; i < threads; i++) CreateThread(NULL, 0, worker, (void *)(uintptr_t)(i + 1), 0, NULL);
    Sleep(seconds * 1000);
    stop = 1;
    Sleep(200);
    printf("%s: %lld continues, %lld with other x16 or x17\n", bad ? "FAIL" : "ok", iters, bad);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), bad ? 1 : 0);
    return 0;
}
