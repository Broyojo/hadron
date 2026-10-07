/* Test-only host allocations, invisible to Wine's view tree. Never use for games. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <crt_externs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((constructor)) static void reserve_host_gaps(void)
{
    mach_vm_address_t cursor = 0x100000000ULL, limit = 0x4100000000ULL;
    unsigned long long bytes = 0;
    int target = 0;
    const char *enabled = getenv("WINE_VM_INJECT_HOST_GAPS");
    if (!enabled || strcmp(enabled, "1")) return;
    for (int i = 1; i < *_NSGetArgc(); i++)
        if (strstr((*_NSGetArgv())[i], "thread-arm64.exe") ||
            strstr((*_NSGetArgv())[i], "thread-x64.exe")) target = 1;
    if (!target) return;
    while (cursor < limit)
    {
        mach_vm_address_t next = cursor;
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        kern_return_t ret = mach_vm_region(mach_task_self(), &next, &size,
            VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
        if (object) mach_port_deallocate(mach_task_self(), object);
        if (ret) next = limit;
        if (next > limit) next = limit;
        if (next > cursor)
        {
            mach_vm_address_t address = cursor;
            kern_return_t mapped = mach_vm_map(mach_task_self(), &address, next-cursor,
                0, VM_FLAGS_FIXED, MEMORY_OBJECT_NULL, 0, 0, VM_PROT_NONE,
                VM_PROT_ALL, VM_INHERIT_NONE);
            if (mapped) { fprintf(stderr, "HOSTPROBE map failed=%d\n", mapped); return; }
            bytes += next-cursor;
        }
        if (next == limit) break;
        if (next + size <= cursor) break;
        cursor = next + size;
    }
    fprintf(stderr, "HOSTPROBE reserved_bytes=%llu committed_bytes=0 end=%#llx\n",
            bytes, (unsigned long long)limit);
}
