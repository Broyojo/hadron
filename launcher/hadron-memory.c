/* Sum the memory footprint of Hadron processes, including Metal allocations and compressed
 * pages that ps's RSS omits. Arguments are pid:rss_kb pairs from scripts/watchdog; when macOS
 * cannot report a process's footprint (for example, it just exited), use its last RSS value. */
#include <libproc.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

int main(int argc, char **argv)
{
    unsigned long long total = 0;
    for (int i = 1; i < argc; i++) {
        char *end;
        if (argv[i][0] < '0' || argv[i][0] > '9') return 1;
        errno = 0;
        long pid = strtol(argv[i], &end, 10);
        if (errno || pid <= 0 || pid > INT_MAX || *end++ != ':') return 1;
        if (*end < '0' || *end > '9') return 1;
        errno = 0;
        unsigned long long rss_kb = strtoull(end, &end, 10);
        if (errno || *end || rss_kb > ULLONG_MAX / 1024) return 1;

        struct rusage_info_v0 usage = {0};
        unsigned long long bytes = rss_kb * 1024;
        if (proc_pid_rusage((int)pid, RUSAGE_INFO_V0, (rusage_info_t *)&usage) == 0 &&
            usage.ri_phys_footprint) bytes = usage.ri_phys_footprint;
        if (bytes > ULLONG_MAX - total) return 1;
        total += bytes;
    }
    printf("%llu\n", total / 1048576 + (total % 1048576 != 0));
    return 0;
}
