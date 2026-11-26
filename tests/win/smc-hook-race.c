/* Installs and removes hooks in code that other threads keep running, the way a mod loader such as
 * Geode does at startup (docs/findings.md #53, #80): the main thread makes a function's page
 * writable with VirtualProtect, writes a jump over the function's first instruction as one aligned
 * 8-byte store, restores the protection and flushes the instruction cache, while worker threads call
 * the functions directly and through callers that call them with direct calls (which FEX links
 * block to block). Threads can be created and exit meanwhile, and memory allocated, run and freed.
 *
 * Each function returns an address only one version can produce, so a call that runs neither the
 * original nor the hooked version is caught (garbage), and, with a sequence count per function, so
 * is a call made entirely after a patch completed that still runs the old version (stale). A
 * watchdog reports a thread that stops making progress, and what it was doing.
 *
 * On FEX-2611.1 with FEX 0001-0007 and the disk cache on, most 10 s runs crash or run garbage,
 * every run sees stale code within its first patches, and with thread churn a few hang or end
 * with Wine's "does not unwind"; FEX 0008, 0009 and 0010 fix these.
 * Exit codes: 0 ok, 2 garbage result, 3 hang, 4 stale code, 5 (0xc0000005) or other: crash.
 * Build (x86_64 only, the functions are x86-64 code; the section alignment puts the hooked code in
 * its own 16K pages, apart from the test's own code):
 *   x86_64-w64-mingw32-clang -O2 -Wl,--section-alignment,16384 smc-hook-race.c -o smc-hook-race.exe
 *   add -DSHARE_PAGE to put the callers in the same 16K page as the hooked functions
 * Usage: smc-hook-race.exe [seconds=20] [workers=6] [thread churners=2] [alloc churners=1] [flags]
 *   flags: s = don't check for stale code (only garbage, hangs and crashes), d = workers make no
 *   direct calls, c = workers don't use the callers */
#define _WIN32_WINNT 0x0A00
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifdef SHARE_PAGE
#define NSLOTS 256 /* 8K of functions, the callers follow in the same 16K page */
#else
#define NSLOTS 1024 /* 32K: callers start in the next 16K page */
#endif
#define NCALLERS 256
#define CALLS 8
#define SLOTSZ 32
#define LOOPS 40
#define STR_(x) #x
#define STR(x) STR_(x)

/* slot i: an 8-byte nop (the patch site), a short loop so that threads are often inside the
 * block when it is patched, then lea rax, [rip] (returns slot + 24); alt i returns alt + 7;
 * caller j calls CALLS slots with direct calls and stores each result to [rcx + 8 * m]. */
__asm__(".section .smc,\"xr\"\n"
        ".balign 4096\n"
        ".globl slots_begin\n"
        "slots_begin:\n"
        ".rept " STR(NSLOTS) "\n"
        ".balign " STR(SLOTSZ) "\n"
        ".byte 0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00\n"
        "mov $" STR(LOOPS) ", %ecx\n"
        "1: dec %ecx\n"
        "jnz 1b\n"
        "lea 0(%rip), %rax\n"
        "ret\n"
        ".endr\n"
        ".balign 128\n"
        ".globl callers_begin\n"
        "callers_begin:\n"
        ".set j, 0\n"
        ".rept " STR(NCALLERS) "\n"
        ".balign 128\n"
        "push %rbx\n"
        "mov %rcx, %rbx\n"
        "sub $32, %rsp\n"
        ".set m, 0\n"
        ".rept " STR(CALLS) "\n"
        "call slots_begin + " STR(SLOTSZ) " * ((j * 37 + m * 101) % " STR(NSLOTS) ")\n"
        "mov %rax, 8 * m(%rbx)\n"
        ".set m, m + 1\n"
        ".endr\n"
        "add $32, %rsp\n"
        "pop %rbx\n"
        "ret\n"
        ".set j, j + 1\n"
        ".endr\n"
        ".balign 16384\n"
        ".globl alts_begin\n"
        "alts_begin:\n"
        ".rept " STR(NSLOTS) "\n"
        ".balign 16\n"
        "lea 0(%rip), %rax\n"
        "ret\n"
        ".endr\n"
        ".balign 4096\n"
        ".text\n");

extern unsigned char slots_begin[], callers_begin[], alts_begin[];

typedef uint64_t (*slot_fn)(void);
typedef void (*caller_fn)(uint64_t *out);

static uint64_t orig_qword, patch_qword[NSLOTS];
static volatile LONG seq[NSLOTS];  /* odd while slot i is being patched */
static volatile LONG state[NSLOTS]; /* 0 original, 1 hooked */
static volatile LONG stop;

#define MAXT 64
static volatile LONG64 progress[MAXT];
static volatile LONG phase[MAXT];
static const char *names[MAXT];
static LONG nthreads;
static HANDLE out;

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    DWORD n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    WriteFile(out, buf, n, &n, NULL);
}

static void die(int code)
{
    TerminateProcess(GetCurrentProcess(), code);
}

static int register_thread(const char *name)
{
    LONG id = InterlockedIncrement(&nthreads) - 1;
    names[id] = name;
    return id;
}

static uint64_t slot_a(int i)
{
    return (uint64_t)(slots_begin + SLOTSZ * i + 24);
}

static uint64_t slot_b(int i)
{
    return (uint64_t)(alts_begin + 16 * i + 7);
}

static int slot_of(int j, int m)
{
    return (j * 37 + m * 101) % NSLOTS;
}

static uint32_t rnd(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static int no_stale_check, no_direct, no_callers;

/* Checks a result; s1 is seq[i] read before the call, via is the caller or -1 for a direct call. */
static void check(const char *who, int via, int i, uint64_t r, LONG s1)
{
    LONG s2;
    if (r != slot_a(i) && r != slot_b(i))
    {
        say("GARBAGE: %s slot %d (caller %d) returned %llx (original %llx, hooked %llx)\n", who, i, via, (unsigned long long)r,
            (unsigned long long)slot_a(i), (unsigned long long)slot_b(i));
        die(2);
    }
    MemoryBarrier();
    s2 = seq[i];
    if (!no_stale_check && !(s1 & 1) && s1 == s2)
    {
        uint64_t want = (s1 >> 1) & 1 ? slot_b(i) : slot_a(i);
        if (r != want)
        {
            say("STALE: %s slot %d (caller %d) returned the %s version after patch %ld completed\n", who, i, via,
                r == slot_a(i) ? "original" : "hooked", s1);
            die(4);
        }
    }
}

static void run_callers(const char *who, uint32_t *s, int count)
{
    uint64_t res[CALLS];
    LONG s1[CALLS];
    int c, m;
    for (c = 0; c < count; c++)
    {
        int j = rnd(s) % NCALLERS;
        for (m = 0; m < CALLS; m++) s1[m] = seq[slot_of(j, m)];
        MemoryBarrier();
        ((caller_fn)(callers_begin + 128 * j))(res);
        for (m = 0; m < CALLS; m++) check(who, j, slot_of(j, m), res[m], s1[m]);
    }
}

static DWORD WINAPI worker(void *arg)
{
    int id = register_thread("worker");
    uint32_t s = 0x9e3779b9u * (id + 1);
    while (!stop)
    {
        int i = rnd(&s) % NSLOTS;
        LONG s1 = seq[i];
        MemoryBarrier();
        phase[id] = 1;
        if (!no_direct) check("worker", -1, i, ((slot_fn)(slots_begin + SLOTSZ * i))(), s1);
        phase[id] = 2;
        if (!no_callers) run_callers("worker", &s, 4);
        phase[id] = 0;
        progress[id]++;
    }
    return 0;
}

static DWORD WINAPI short_thread(void *arg)
{
    uint32_t s = (uint32_t)(uintptr_t)arg | 1;
    run_callers("short thread", &s, 8);
    return 0;
}

static DWORD WINAPI thread_churner(void *arg)
{
    int id = register_thread("thread churner");
    uint32_t n = 1;
    while (!stop)
    {
        HANDLE h;
        phase[id] = 1;
        h = CreateThread(NULL, 0, short_thread, (void *)(uintptr_t)(n++ * 2654435761u), 0, NULL);
        if (!h) { say("CreateThread failed %lu\n", GetLastError()); die(6); }
        phase[id] = 2;
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
        phase[id] = 0;
        progress[id]++;
    }
    return 0;
}

/* Allocates memory, writes a function into it, runs it and frees it again. */
static DWORD WINAPI alloc_churner(void *arg)
{
    int id = register_thread("alloc churner");
    static const unsigned char fn[] = { 0x48, 0x8d, 0x05, 0, 0, 0, 0, 0xc3 }; /* lea rax, [rip]; ret */
    while (!stop)
    {
        unsigned char *p, *q;
        DWORD old;
        phase[id] = 1;
        p = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        memset(p, 1, 0x10000);
        phase[id] = 2;
        q = VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        memcpy(q + 0x100, fn, sizeof(fn));
        VirtualProtect(q, 0x4000, PAGE_EXECUTE_READ, &old);
        FlushInstructionCache(GetCurrentProcess(), q, 0x4000);
        phase[id] = 3;
        if (((slot_fn)(q + 0x100))() != (uint64_t)(q + 0x107))
        {
            say("GARBAGE: fresh code at %p returned the wrong address\n", q + 0x100);
            die(2);
        }
        phase[id] = 4;
        VirtualFree(q, 0, MEM_RELEASE);
        VirtualFree(p, 0, MEM_RELEASE);
        phase[id] = 0;
        progress[id]++;
    }
    return 0;
}

static DWORD WINAPI watchdog(void *arg)
{
    LONG64 last[MAXT] = { 0 };
    DWORD since[MAXT] = { 0 };
    DWORD limit = (DWORD)(uintptr_t)arg;
    int i;
    for (;;)
    {
        Sleep(500);
        for (i = 0; i < nthreads; i++)
        {
            if (progress[i] != last[i]) { last[i] = progress[i]; since[i] = 0; continue; }
            if ((since[i] += 500) < limit) continue;
            say("HANG: %s (thread %d) stuck for %lu ms in phase %ld\n", names[i], i, since[i], phase[i]);
            for (i = 0; i < nthreads; i++)
                say("  thread %d %s: progress %lld phase %ld\n", i, names[i], progress[i], phase[i]);
            die(3);
        }
    }
    return 0;
}

static void patch(int i, int hook)
{
    unsigned char *p = slots_begin + SLOTSZ * i;
    DWORD old, tmp;
    InterlockedIncrement(&seq[i]); /* odd: patch in progress */
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) { say("VirtualProtect failed %lu\n", GetLastError()); die(6); }
    *(volatile uint64_t *)p = hook ? patch_qword[i] : orig_qword;
    VirtualProtect(p, 5, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), p, 8);
    state[i] = hook;
    InterlockedIncrement(&seq[i]); /* even again: every patch toggles, so bit 1 is the version */
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    int workers = argc > 2 ? atoi(argv[2]) : 6;
    int tchurn = argc > 3 ? atoi(argv[3]) : 2;
    int achurn = argc > 4 ? atoi(argv[4]) : 1;
    no_stale_check = argc > 5 && strchr(argv[5], 's');
    no_direct = argc > 5 && strchr(argv[5], 'd');
    no_callers = argc > 5 && strchr(argv[5], 'c');
    int me, i;
    uint32_t s = 12345;
    uint64_t patches = 0;
    DWORD start;

    out = GetStdHandle(STD_OUTPUT_HANDLE);
    memcpy(&orig_qword, slots_begin, 8);
    for (i = 0; i < NSLOTS; i++)
    {
        unsigned char q[8] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
        int32_t rel = (int32_t)((alts_begin + 16 * i) - (slots_begin + SLOTSZ * i + 5));
        memcpy(q + 1, &rel, 4);
        memcpy(&patch_qword[i], q, 8);
    }
    say("smc-hook-race: %d s, %d workers, %d thread churners, %d alloc churners, %d slots%s\n", seconds, workers,
        tchurn, achurn, NSLOTS,
#ifdef SHARE_PAGE
        " (callers share their page)"
#else
        ""
#endif
    );

    me = register_thread("patcher");
    CreateThread(NULL, 0, watchdog, (void *)(uintptr_t)15000, 0, NULL);
    for (i = 0; i < workers; i++) CreateThread(NULL, 0, worker, NULL, 0, NULL);
    for (i = 0; i < tchurn; i++) CreateThread(NULL, 0, thread_churner, NULL, 0, NULL);
    for (i = 0; i < achurn; i++) CreateThread(NULL, 0, alloc_churner, NULL, 0, NULL);

    start = GetTickCount();
    while (GetTickCount() - start < (DWORD)seconds * 1000)
    {
        int k = rnd(&s) % NSLOTS;
        LONG s1;
        phase[me] = 1;
        patch(k, !state[k]);
        patches++;
        /* the patching thread itself must see the new version at once */
        phase[me] = 2;
        s1 = seq[k];
        check("patcher", -1, k, ((slot_fn)(slots_begin + SLOTSZ * k))(), s1);
        phase[me] = 3;
        run_callers("patcher", &s, 1);
        phase[me] = 0;
        progress[me]++;
    }
    stop = 1;
    Sleep(200);
    {
        LONG64 total = 0;
        for (i = 0; i < nthreads; i++) total += progress[i];
        say("ok: %llu patches, %lld loop iterations over %ld threads\n", (unsigned long long)patches, total, nthreads);
    }
    die(0);
    return 0;
}
