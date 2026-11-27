/* Builds FEX's allocator, rpmalloc, with the settings of FEX's Windows modules and checks that no
 * thread frees blocks into another thread's heap as if it were its own (docs/findings.md #80).
 *
 * rpmalloc frees a block without atomics when its heap's owner_thread is the current thread's id,
 * and through atomic deferred lists otherwise. In FEX's Windows build the id is the TEB address. A
 * thread that exits puts its heap on a queue for the next thread, and kept its id as the owner:
 * Wine reuses a TEB for a later thread, which may take another heap from the queue, and that thread
 * then freed the released heap's blocks as its own while whichever thread took that heap allocated
 * from the same pages. With a delay in that path the free lists were corrupted at once.
 *
 * Threads start, allocate, free blocks that other threads allocated, and exit, 8 at a time; each
 * free is checked first, from rpmalloc's own state, and each block's contents when it is freed.
 * Usage: rpmalloc-thread-heaps.exe [seconds=10] [threads at a time=8]
 * Exit codes: 0 ok, 1 a free was taken as local into another heap, or a block was overwritten.
 * Build (from tests/win, with src/fex fetched):
 *   aarch64-w64-mingw32-clang -O2 -I../../src/fex/External/rpmalloc/rpmalloc -DENABLE_OVERRIDE=0
 *     -DENABLE_DYNAMIC_LINK=0 -DENABLE_STATISTICS=0 -DENABLE_DECOMMIT=1 -DENABLE_ASSERTS=0
 *     -DDYNAMIC_TLS_NO_FLS=1 rpmalloc-thread-heaps.c -o rpmalloc-thread-heaps.exe */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "rpmalloc.c"

#define RING 4096
static void *ring[RING];
static SRWLOCK ring_lock = SRWLOCK_INIT;
static volatile LONG foreign_local, overwritten, live;

static unsigned next_random(unsigned *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return *seed >> 8;
}

/* a block holds its length in words, then a pattern made from its address */
static void fill(uint64_t *block, size_t words)
{
    size_t i;
    block[0] = words;
    for (i = 1; i < words; i++) block[i] = (uintptr_t)block ^ i;
}

static void check_and_free(uint64_t *block)
{
    span_t *span = (span_t *)((uintptr_t)block & SPAN_MASK);
    page_t *page = span_get_page_from_block(span, block);
    size_t i, words = block[0];

    /* the free below would push the block onto the page's local list without atomics */
    if (page->heap->owner_thread == get_thread_id() && page->heap != get_thread_heap())
        InterlockedIncrement(&foreign_local);
    if (words < 3 || words > 33) InterlockedIncrement(&overwritten);
    else
        for (i = 1; i < words; i++)
            if (block[i] != ((uintptr_t)block ^ i))
            {
                InterlockedIncrement(&overwritten);
                break;
            }
    rpfree(block);
}

static uint64_t *swap_block(unsigned slot, uint64_t *block)
{
    uint64_t *old;
    AcquireSRWLockExclusive(&ring_lock);
    old = ring[slot];
    ring[slot] = block;
    ReleaseSRWLockExclusive(&ring_lock);
    return old;
}

static DWORD WINAPI worker(void *arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg * 2654435761u;
    int i, count = 200 + next_random(&seed) % 400;
    uint64_t *block;

    /* what FEX's InitCRTThread and DeinitCRTThread do around a thread */
    rpmalloc_thread_initialize();
    for (i = 0; i < count; i++)
    {
        size_t words = 3 + next_random(&seed) % 31; /* 24 to 264 bytes */
        uint64_t *mine = rpmalloc(words * sizeof(uint64_t));
        fill(mine, words);
        if ((block = swap_block(next_random(&seed) % RING, mine))) check_and_free(block);
    }
    rpmalloc_thread_finalize();
    /* FEX's own threads still free after that: ThreadTerm runs after DeinitCRTThread */
    for (i = 0; i < 16; i++)
        if ((block = swap_block(next_random(&seed) % RING, NULL))) check_and_free(block);
    InterlockedDecrement(&live);
    return 0;
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 10, at_a_time = argc > 2 ? atoi(argv[2]) : 8;
    DWORD start = GetTickCount();
    unsigned threads = 0;
    HANDLE thread;

    rpmalloc_initialize(NULL);
    while (GetTickCount() - start < seconds * 1000u)
    {
        while (live >= at_a_time) Sleep(0);
        InterlockedIncrement(&live);
        if ((thread = CreateThread(NULL, 0, worker, (void *)(uintptr_t)++threads, 0, NULL))) CloseHandle(thread);
        else InterlockedDecrement(&live);
    }
    while (live) Sleep(1);
    printf("%u threads, %ld frees taken as local into another thread's heap, %ld blocks overwritten\n", threads,
           foreign_local, overwritten);
    printf(foreign_local || overwritten ? "FAILED\n" : "ok\n");
    fflush(stdout);
    return foreign_local || overwritten ? 1 : 0;
}
