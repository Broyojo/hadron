/* Branches to address 0 from a thread that has just started, the way a call through a null
 * function pointer does, and checks that each raises an access violation at 0 that the program can
 * handle, as on Windows (docs/findings.md #80). A vectored handler resumes at the caller.
 *
 * FEX's fast paths for indirect branches compare the target with a guest address read from a
 * cache entry, and an empty entry reads as address 0: an indirect call or jump through the lookup
 * cache's first entry and a return predicted from an empty call-ret stack entry branched natively
 * to the entry's host address, 0, on FEX's own stack, and the process ended with "invalid frame"
 * and "unable to dispatch exception". Each case runs in a new thread, whose caches are empty, and
 * again in a thread that has run code (warm).
 * Usage: null-branch.exe [case]: runs all three cases, or only the one given (0 to 2).
 * Exit codes: 0 ok, 1 a case didn't raise the exception, anything else: crash.
 * Build: x86_64-w64-mingw32-clang -O2 null-branch.c -o null-branch.exe */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static volatile LONG raised;

/* call *%rax, jmp *%rax and ret with 0 as the target; each leaves its return address (the
 * address to resume at) on the stack top when the access violation is raised at 0. */
__asm__(".text\n"
        ".globl call_null\n"
        "call_null:\n"
        "  xor %eax, %eax\n"
        "  call *%rax\n"
        "  ret\n"
        ".globl jmp_null\n"
        "jmp_null:\n"
        "  xor %eax, %eax\n"
        "  jmp *%rax\n"
        ".globl ret_null\n"
        "ret_null:\n"
        "  push $0\n"
        "  ret\n");
extern void call_null(void), jmp_null(void), ret_null(void);

static LONG CALLBACK handler(EXCEPTION_POINTERS *e)
{
    CONTEXT *c = e->ContextRecord;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || c->Rip != 0) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&raised);
    /* resume at the return address on the stack top, as if the branch target had returned */
    c->Rip = *(DWORD64 *)c->Rsp;
    c->Rsp += 8;
    c->Rax = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int compare(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static void warm_up(void)
{
    int v[64], i;
    for (i = 0; i < 64; i++) v[i] = (i * 37) % 64;
    for (i = 0; i < 100; i++) qsort(v, 64, sizeof(int), compare);
}

struct test
{
    int warm;
    int (*run)(void);
};

static int run_call(void) { call_null(); return 1; }
static int run_jmp(void) { jmp_null(); return 1; }
static int run_ret(void) { ret_null(); return 1; }

static DWORD WINAPI thread(void *arg)
{
    struct test *t = arg;
    if (t->warm) warm_up();
    return t->run();
}

int main(int argc, char **argv)
{
    static const struct { const char *name; int (*run)(void); } cases[] = {
        { "indirect call", run_call },
        { "indirect jump", run_jmp },
        { "return", run_ret },
    };
    int i, warm, failed = 0, only = argc > 1 ? atoi(argv[1]) : -1;

    AddVectoredExceptionHandler(1, handler);
    for (i = 0; i < 3; i++)
    {
        if (only >= 0 && i != only) continue;
        for (warm = 0; warm < 2; warm++)
        {
            struct test t = { warm, cases[i].run };
            LONG before = raised;
            HANDLE h = CreateThread(NULL, 0, thread, &t, 0, NULL);
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
            printf("%-16s %-10s %s\n", cases[i].name, warm ? "warm" : "new thread",
                   raised != before ? "access violation at 0, handled" : "no exception");
            fflush(stdout);
            if (raised == before) failed = 1;
        }
    }
    printf(failed ? "FAILED\n" : "ok\n");
    fflush(stdout);
    return failed;
}
