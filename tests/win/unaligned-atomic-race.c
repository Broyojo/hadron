/* Runs the same x86 functions from many threads at once while they make memory accesses that
 * cross a 16-byte boundary, the way Geometry Dash with Geode's threads do at startup
 * (docs/findings.md #80): FEX emulates x86 memory ordering with load-acquire and store-release
 * instructions, which fault on such an access, and its handler rewrites the instruction in the
 * shared JIT code (an ordinary load or store and a barrier) while other threads may be running
 * it. Thread 0x190's "Handled unaligned atomic" was the last thing FEX logged before Geometry
 * Dash's third kind of failure: an illegal instruction at the start of one of Geode.dll's x86
 * functions, executed as host code.
 *
 * Each function loads, stores and adds across 16-byte and cache line boundaries, makes a locked
 * add across a cache line, and returns an address only its own code can produce; callers call
 * eight of them with direct calls (linked by FEX). What makes the rewrites race:
 *   - a patcher hooks and unhooks the functions' first instruction, as smc-hook-race does, so FEX
 *     discards and recompiles them and every recompile brings fresh sites to rewrite;
 *   - rounds: a fresh copy of all functions and callers is written into new memory, a batch of new
 *     threads is released at once to run them all in the same order (each site faults in several
 *     threads at nearly the same time, and the callers are linked to fresh blocks), the copy is
 *     freed when they are done;
 *   - workers that run the hooked functions all the time.
 * A wrong result (garbage), a thread that stops (hang) or a crash ends the run. With FEX 0001-0011
 * it passes (a build that logs the rewrites logs about 19,000 a second, some 3,000 of them by a
 * thread that faulted on a site another thread had just rewritten), so the rewrite was not shown to
 * be what Geometry Dash hits.
 * Exit codes: 0 ok, 2 garbage, 3 hang, other: crash (Wine's "invalid frame" and "unable to dispatch
 * exception" when FEX passes on a fault in its own code).
 * Build (x86_64 only):
 *   x86_64-w64-mingw32-clang -O2 -Wl,--section-alignment,16384 unaligned-atomic-race.c -o unaligned-atomic-race.exe
 * Usage: unaligned-atomic-race.exe [seconds=10] [workers=4] [round threads=8] [flags]
 *   flags: p = no patcher, r = no rounds, w = no workers */
#define _WIN32_WINNT 0x0A00
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define NSLOTS 256
#define SLOTSZ 128
#define NCALLERS 64
#define CALLS 8
#define STR_(x) #x
#define STR(x) STR_(x)

/* The accesses through rcx, a 128-byte buffer aligned to 64; each crosses a 16-byte boundary
 * (the locked add and the 4-byte store cross a cache line). */
#define BODY                                   \
    "mov 12(%rcx), %rax\n"   /* 12..19 */      \
    "mov %rax, 28(%rcx)\n"   /* 28..35 */      \
    "movl 46(%rcx), %edx\n"  /* 46..49 */      \
    "add %rdx, %rax\n"                         \
    "movl %eax, 62(%rcx)\n"  /* 62..65 */      \
    "movw 79(%rcx), %dx\n"   /* 79..80 */      \
    "movw %dx, 95(%rcx)\n"   /* 95..96 */      \
    "mov $1, %edx\n"                           \
    "lock xadd %rdx, 124(%rcx)\n" /* 124..131: the buffer's last word and the next line */ \
    "mov 108(%rcx), %rdx\n"  /* 108..115 */    \
    "mov %rdx, 4(%rcx)\n"    /* 4..11, inside 16: no fault */

/* slot i: an 8-byte nop (the hook site), the accesses, lea rax, [rip] (returns slot + off);
 * alt i: the accesses, lea rax, [rip]. Caller j calls CALLS slots, each result to [rdx + 8 * m]. */
__asm__(".section .uar,\"xr\"\n"
        ".balign 16384\n"
        ".globl uslots_begin\n"
        "uslots_begin:\n"
        ".rept " STR(NSLOTS) "\n"
        ".balign " STR(SLOTSZ) "\n"
        ".byte 0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00\n"
        BODY
        "lea 0(%rip), %rax\n"
        "ret\n"
        ".endr\n"
        ".balign 128\n"
        ".globl ucallers_begin\n"
        "ucallers_begin:\n"
        ".set j, 0\n"
        ".rept " STR(NCALLERS) "\n"
        ".balign 256\n"
        "push %rbx\n"
        "push %rsi\n"
        "mov %rdx, %rbx\n"
        "mov %rcx, %rsi\n"
        "sub $40, %rsp\n"
        ".set m, 0\n"
        ".rept " STR(CALLS) "\n"
        "mov %rsi, %rcx\n"
        "call uslots_begin + " STR(SLOTSZ) " * ((j * 37 + m * 101) % " STR(NSLOTS) ")\n"
        "mov %rax, 8 * m(%rbx)\n"
        ".set m, m + 1\n"
        ".endr\n"
        "add $40, %rsp\n"
        "pop %rsi\n"
        "pop %rbx\n"
        "ret\n"
        ".set j, j + 1\n"
        ".endr\n"
        ".balign 16384\n"
        ".globl ucode_end\n"
        "ucode_end:\n"
        ".globl ualts_begin\n"
        "ualts_begin:\n"
        ".rept " STR(NSLOTS) "\n"
        ".balign " STR(SLOTSZ) "\n"
        BODY
        "lea 0(%rip), %rax\n"
        "ret\n"
        ".endr\n"
        ".balign 16384\n"
        ".text\n");

extern unsigned char uslots_begin[], ucallers_begin[], ucode_end[], ualts_begin[];

typedef uint64_t (*slot_fn)(unsigned char *buf);
typedef void (*caller_fn)(unsigned char *buf, uint64_t *out);

static size_t ret_off, alt_ret_off; /* offset of the address lea returns, in a slot and an alt */
static uint64_t orig_qword, patch_qword[NSLOTS];
static volatile LONG state[NSLOTS];
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

static uint32_t rnd(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static int slot_of(int j, int m)
{
    return (j * 37 + m * 101) % NSLOTS;
}

/* A thread's buffer: 128 bytes from a 64-byte boundary, then the word the locked add spills into. */
struct buf
{
    unsigned char *p;
    unsigned char mem[256 + 64];
};

static void buf_init(struct buf *b, uint32_t seed)
{
    int i;
    b->p = (unsigned char *)(((uintptr_t)b->mem + 63) & ~(uintptr_t)63);
    for (i = 0; i < 136; i++) b->p[i] = (unsigned char)(rnd(&seed) | 1);
}

/* What one call must have done to the buffer, given a copy of it from before the call. */
static int buf_check(const unsigned char *before, const unsigned char *after)
{
    uint64_t a, s;
    uint32_t d, e;
    uint16_t w;
    memcpy(&a, before + 12, 8);
    memcpy(&s, after + 28, 8);
    if (s != a) return 0;
    memcpy(&d, before + 46, 4);
    memcpy(&e, after + 62, 4);
    if (e != (uint32_t)(a + d)) return 0;
    memcpy(&w, before + 79, 2);
    if (memcmp(after + 95, &w, 2)) return 0;
    {
        uint64_t x, y;
        memcpy(&x, before + 124, 8);
        memcpy(&y, after + 124, 8);
        if (y != x + 1) return 0;
    }
    if (memcmp(after + 4, before + 108, 8)) return 0;
    return 1;
}

static void call_check(const char *who, unsigned char *base, int i, struct buf *b, int hooked_ok)
{
    unsigned char before[136];
    uint64_t r;
    memcpy(before, b->p, 136);
    r = ((slot_fn)(base + SLOTSZ * i))(b->p);
    if (r != (uint64_t)(base + SLOTSZ * i + ret_off) &&
        !(hooked_ok && r == (uint64_t)(ualts_begin + SLOTSZ * i + alt_ret_off)))
    {
        say("GARBAGE: %s slot %d at %p returned %llx\n", who, i, base + SLOTSZ * i, (unsigned long long)r);
        die(2);
    }
    if (!buf_check(before, b->p))
    {
        say("GARBAGE: %s slot %d at %p left the wrong values in its buffer\n", who, i, base + SLOTSZ * i);
        die(2);
    }
}

/* Runs caller j of the functions at base (the static ones, which may be hooked, or a copy). */
static void run_caller(const char *who, unsigned char *base, int j, struct buf *b)
{
    uint64_t res[CALLS];
    int m;
    ((caller_fn)(base + (ucallers_begin - uslots_begin) + 256 * j))(b->p, res);
    for (m = 0; m < CALLS; m++)
    {
        int i = slot_of(j, m);
        if (res[m] != (uint64_t)(base + SLOTSZ * i + ret_off) &&
            !(base == uslots_begin && res[m] == (uint64_t)(ualts_begin + SLOTSZ * i + alt_ret_off)))
        {
            say("GARBAGE: %s caller %d slot %d returned %llx\n", who, j, i, (unsigned long long)res[m]);
            die(2);
        }
    }
}

static DWORD WINAPI worker(void *arg)
{
    int id = register_thread("worker");
    uint32_t s = 0x9e3779b9u * (id + 1);
    struct buf b;
    buf_init(&b, s);
    while (!stop)
    {
        phase[id] = 1;
        call_check("worker", uslots_begin, rnd(&s) % NSLOTS, &b, 1);
        phase[id] = 2;
        run_caller("worker", uslots_begin, rnd(&s) % NCALLERS, &b);
        phase[id] = 0;
        progress[id]++;
    }
    return 0;
}

/* Rounds: a fresh copy of the functions, run by a batch of new threads released together. */
static unsigned char *volatile round_code;
static HANDLE round_go;
static volatile LONG round_ready;

static DWORD WINAPI round_thread(void *arg)
{
    uint32_t s = (uint32_t)(uintptr_t)arg | 1;
    unsigned char *code = round_code;
    struct buf b;
    int i, n;
    buf_init(&b, s);
    InterlockedIncrement(&round_ready);
    WaitForSingleObject(round_go, INFINITE);
    for (n = 0; n < 2; n++)
    {
        for (i = 0; i < NSLOTS; i++) call_check("round thread", code, i, &b, 0);
        for (i = 0; i < NCALLERS; i++) run_caller("round thread", code, i, &b);
    }
    /* and the hooked static functions, through their callers */
    for (i = 0; i < 8; i++) run_caller("round thread", uslots_begin, rnd(&s) % NCALLERS, &b);
    return 0;
}

static DWORD WINAPI rounds(void *arg)
{
    int id = register_thread("rounds");
    int count = (int)(uintptr_t)arg, i;
    HANDLE th[MAXT];
    uint32_t n = 1;
    size_t size = ucode_end - uslots_begin; /* the functions and their callers */
    round_go = CreateEventW(NULL, TRUE, FALSE, NULL);
    while (!stop)
    {
        unsigned char *p;
        DWORD old;
        phase[id] = 1;
        p = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!p) { say("VirtualAlloc failed %lu\n", GetLastError()); die(6); }
        memcpy(p, uslots_begin, size);
        for (i = 0; i < NSLOTS; i++) memcpy(p + SLOTSZ * i, &orig_qword, 8); /* unhooked */
        VirtualProtect(p, size, PAGE_EXECUTE_READ, &old);
        FlushInstructionCache(GetCurrentProcess(), p, size);
        round_code = p;
        round_ready = 0;
        ResetEvent(round_go);
        phase[id] = 2;
        for (i = 0; i < count; i++)
        {
            th[i] = CreateThread(NULL, 0, round_thread, (void *)(uintptr_t)(n++ * 2654435761u), 0, NULL);
            if (!th[i]) { say("CreateThread failed %lu\n", GetLastError()); die(6); }
        }
        while (round_ready < count) SwitchToThread();
        SetEvent(round_go);
        phase[id] = 3;
        WaitForMultipleObjects(count, th, TRUE, INFINITE);
        for (i = 0; i < count; i++) CloseHandle(th[i]);
        phase[id] = 4;
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
    unsigned char *p = uslots_begin + SLOTSZ * i;
    DWORD old, tmp;
    if (!VirtualProtect(p, 8, PAGE_EXECUTE_READWRITE, &old)) { say("VirtualProtect failed %lu\n", GetLastError()); die(6); }
    *(volatile uint64_t *)p = hook ? patch_qword[i] : orig_qword;
    VirtualProtect(p, 8, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), p, 8);
    state[i] = hook;
}

static size_t find_lea(const unsigned char *p)
{
    size_t k;
    for (k = 0; k < SLOTSZ - 7; k++)
        if (p[k] == 0x48 && p[k + 1] == 0x8d && p[k + 2] == 0x05 && !memcmp(p + k + 3, "\0\0\0\0", 4)) return k + 7;
    say("no lea in the slot\n");
    die(6);
    return 0;
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    int workers = argc > 2 ? atoi(argv[2]) : 4;
    int rthreads = argc > 3 ? atoi(argv[3]) : 8;
    const char *flags = argc > 4 ? argv[4] : "";
    int no_patcher = !!strchr(flags, 'p'), no_rounds = !!strchr(flags, 'r'), no_workers = !!strchr(flags, 'w');
    int me, i;
    uint32_t s = 12345;
    uint64_t patches = 0;
    DWORD start;
    struct buf b;

    out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (rthreads > MAXT - 16) rthreads = MAXT - 16;
    ret_off = find_lea(uslots_begin);
    alt_ret_off = find_lea(ualts_begin);
    memcpy(&orig_qword, uslots_begin, 8);
    for (i = 0; i < NSLOTS; i++)
    {
        unsigned char q[8] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
        int32_t rel = (int32_t)((ualts_begin + SLOTSZ * i) - (uslots_begin + SLOTSZ * i + 5));
        memcpy(q + 1, &rel, 4);
        memcpy(&patch_qword[i], q, 8);
    }
    say("unaligned-atomic-race: %d s, %d workers, rounds of %d threads%s%s%s\n", seconds, workers, rthreads,
        no_patcher ? ", no patcher" : "", no_rounds ? ", no rounds" : "", no_workers ? ", no workers" : "");

    me = register_thread("patcher");
    buf_init(&b, 777);
    CreateThread(NULL, 0, watchdog, (void *)(uintptr_t)15000, 0, NULL);
    if (!no_workers) for (i = 0; i < workers; i++) CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!no_rounds) CreateThread(NULL, 0, rounds, (void *)(uintptr_t)rthreads, 0, NULL);

    start = GetTickCount();
    while (GetTickCount() - start < (DWORD)seconds * 1000)
    {
        int k = rnd(&s) % NSLOTS;
        phase[me] = 1;
        if (!no_patcher)
        {
            patch(k, !state[k]);
            patches++;
        }
        else Sleep(1);
        phase[me] = 2;
        call_check("patcher", uslots_begin, k, &b, 1);
        phase[me] = 0;
        progress[me]++;
    }
    stop = 1;
    Sleep(300);
    {
        LONG64 total = 0;
        for (i = 0; i < nthreads; i++) total += progress[i];
        say("ok: %llu patches, %lld loop iterations over %ld threads\n", (unsigned long long)patches, total, nthreads);
    }
    die(0);
    return 0;
}
