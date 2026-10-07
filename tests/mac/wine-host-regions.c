#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>

typedef uintptr_t ULONG_PTR;
#define min(a,b) ((a) < (b) ? (a) : (b))
#include "wine-address-search.inc"

static kern_return_t try_fixed(uintptr_t candidate, size_t size)
{
    mach_vm_address_t address = candidate;
    return mach_vm_map(mach_task_self(), &address, size, 0, VM_FLAGS_FIXED,
                       MEMORY_OBJECT_NULL, 0, 0, VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_NONE);
}

int main(void)
{
    mach_vm_address_t arena = 0;
    size_t total = 64 * 1024 * 1024, page = sysconf(_SC_PAGESIZE);
    size_t head = 8 * 1024 * 1024 + page, tail = 8 * 1024 * 1024 + 2 * page;
    uintptr_t low, high, candidate, next, expected;
    const size_t alignments[] = {65536, 2 * 1024 * 1024};
    const size_t sizes[] = {65536, 1114112};
    unsigned int tests = 0;

    assert(mach_vm_allocate(mach_task_self(), &arena, total, VM_FLAGS_ANYWHERE) == KERN_SUCCESS);
    low = arena + head;
    high = arena + total - tail;
    assert(mach_vm_deallocate(mach_task_self(), arena, head) == KERN_SUCCESS);
    assert(mach_vm_deallocate(mach_task_self(), high, tail) == KERN_SUCCESS);
    *(volatile unsigned char *)low = 0x51;
    *(volatile unsigned char *)(high - 1) = 0xa3;

    for (unsigned int a = 0; a < 2; a++)
    for (unsigned int s = 0; s < 2; s++)
    {
        size_t align = alignments[a], size = sizes[s], mask = align - 1;
        candidate = (low + mask) & ~mask;
        assert(try_fixed(candidate, size) == KERN_NO_SPACE);
        next = (uintptr_t)next_mmap_candidate((void *)candidate, size, align);
        expected = (high + mask) & ~mask;
        assert(next == expected && !(next & mask));
        assert(try_fixed(next, size) == KERN_SUCCESS);
        assert(mach_vm_deallocate(mach_task_self(), next, size) == KERN_SUCCESS);
        tests++;

        candidate = (high - size) & ~mask;
        assert(try_fixed(candidate, size) == KERN_NO_SPACE);
        next = (uintptr_t)next_mmap_candidate((void *)candidate, size, -(ptrdiff_t)align);
        expected = (low - size) & ~mask;
        assert(next == expected && !(next & mask));
        assert(try_fixed(next, size) == KERN_SUCCESS);
        assert(mach_vm_deallocate(mach_task_self(), next, size) == KERN_SUCCESS);
        tests++;
    }

    /* The candidate begins in a hole, but the requested mapping hits its next region. */
    candidate = (low - 1) & ~(uintptr_t)65535;
    assert(try_fixed(candidate, 131072) == KERN_NO_SPACE);
    next = (uintptr_t)next_mmap_candidate((void *)candidate, 131072, 65536);
    assert(next == ((high + 65535) & ~(uintptr_t)65535));
    next = (uintptr_t)next_mmap_candidate((void *)candidate, 131072, -65536);
    assert(next == ((low - 131072) & ~(uintptr_t)65535));
    tests += 2;

    /* Do not jump over a hole when the next mapping is beyond the requested extent. */
    candidate = (low & ~(uintptr_t)65535) - 4 * 65536;
    assert(next_mmap_candidate((void *)candidate, 65536, 65536) == (void *)(candidate + 65536));
    assert(next_mmap_candidate((void *)candidate, 65536, -65536) == (void *)(candidate - 65536));
    tests += 2;
    assert(*(volatile unsigned char *)low == 0x51);
    assert(*(volatile unsigned char *)(high - 1) == 0xa3);

    /* Simulate an unmap between the failed fixed-map attempt and the region query. */
    candidate = (low + 65535) & ~(uintptr_t)65535;
    assert(try_fixed(candidate, 65536) == KERN_NO_SPACE);
    assert(mach_vm_deallocate(mach_task_self(), low, high-low) == KERN_SUCCESS);
    assert(next_mmap_candidate((void *)candidate, 65536, 65536) == (void *)(candidate + 65536));
    assert(next_mmap_candidate((void *)candidate, 65536, -65536) == (void *)(candidate - 65536));
    assert(try_fixed(candidate, 65536) == KERN_SUCCESS);
    assert(mach_vm_deallocate(mach_task_self(), candidate, 65536) == KERN_SUCCESS);
    tests += 2;
    printf("PASS %u host-region checks: both directions, alignment, overlap, mapping preservation, unmap race\n", tests);
    return 0;
}
