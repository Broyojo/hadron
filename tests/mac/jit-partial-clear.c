#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static atomic_int done, count, bad;
static int (*code)(void);
static void *execute(void *unused)
{
    (void)unused;
    pthread_jit_write_protect_np(1);
    while (!atomic_load(&done))
    {
        if (code() != 42)
            atomic_store(&bad, 1);
        atomic_fetch_add(&count, 1);
    }
    return NULL;
}
int main(void)
{
    size_t n = getpagesize();
    if (n < 16384)
        return 77;
    char *p = mmap(NULL, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
    if (p == MAP_FAILED)
        return 1;
    pthread_jit_write_protect_np(0);
    memset(p, 0x5a, n);
    unsigned int insns[] = {0x52800540, 0xd65f03c0};
    memcpy(p, insns, sizeof(insns));
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(p, n);
    code = (void *)p;
    pthread_t t;
    if (pthread_create(&t, NULL, execute, NULL))
        return 2;
    while (atomic_load(&count) < 100)
        usleep(100);
    for (int i = 0; i < 10; i++)
    {
        pthread_jit_write_protect_np(0);
        memset(p + 4096, 0, 4096);
        usleep(10000);
        pthread_jit_write_protect_np(1);
        sys_icache_invalidate(p + 4096, 4096);
    }
    atomic_store(&done, 1);
    pthread_join(t, NULL);
    for (size_t i = 4096; i < 8192; i++)
        if (p[i])
            return 3;
    for (size_t i = 8192; i < n; i++)
        if ((unsigned char)p[i] != 0x5a)
            return 4;
    printf("MAP_JIT: concurrent executions=%d bad=%d; cleared bytes and neighbours verified\n", atomic_load(&count),
           atomic_load(&bad));
    munmap(p, n);
    return atomic_load(&bad) ? 5 : 0;
}
