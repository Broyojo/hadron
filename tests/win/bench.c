/* CPU micro-benchmark: same source compiled native and x86, to measure translation overhead. */
#define _WIN32_WINNT 0x0A00
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <windows.h>

static double now(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &c );
    return (double)c.QuadPart / f.QuadPart;
}

/* integer: xorshift + hashing */
static uint64_t bench_int(uint64_t n)
{
    uint64_t x = 88172645463325252ull, h = 0;
    for (uint64_t i = 0; i < n; i++)
    {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        h = (h ^ x) * 0x100000001b3ull;
    }
    return h;
}

/* floating point: n-body style inner loop */
static double bench_fp(int n)
{
    double px[64], py[64], vx[64] = {0}, vy[64] = {0}, e = 0;
    for (int i = 0; i < 64; i++) { px[i] = sin(i); py[i] = cos(i * 1.3); }
    for (int s = 0; s < n; s++)
        for (int i = 0; i < 64; i++)
            for (int j = 0; j < 64; j++)
            {
                if (i == j) continue;
                double dx = px[j] - px[i], dy = py[j] - py[i];
                double d2 = dx * dx + dy * dy + 0.01, inv = 1.0 / (d2 * sqrt(d2));
                vx[i] += dx * inv * 1e-4; vy[i] += dy * inv * 1e-4;
            }
    for (int i = 0; i < 64; i++) e += vx[i] * vx[i] + vy[i] * vy[i];
    return e;
}

/* memory: strided sum over 64MB */
static uint64_t bench_mem(int passes)
{
    static uint32_t buf[16 << 20];
    uint64_t sum = 0;
    for (size_t i = 0; i < ARRAYSIZE(buf); i++) buf[i] = (uint32_t)i;
    for (int p = 0; p < passes; p++)
        for (size_t i = 0; i < ARRAYSIZE(buf); i += 16) sum += buf[i];
    return sum;
}

/* threads: contended atomic increments */
static volatile LONG64 counter;
static DWORD WINAPI worker(void *arg)
{
    for (int i = 0; i < 2000000; i++) InterlockedIncrement64( &counter );
    return 0;
}

int main(void)
{
    double t; HANDLE th[8];
    setvbuf( stdout, NULL, _IONBF, 0 );
#if defined(__x86_64__)
    const char *arch = "x86_64";
#elif defined(__aarch64__)
    const char *arch = "aarch64";
#else
    const char *arch = "i386";
#endif
    t = now(); uint64_t r1 = bench_int(400000000);  printf("%-8s int    %7.3fs (%llx)\n", arch, now() - t, (unsigned long long)r1);
    t = now(); double r2 = bench_fp(4000);           printf("%-8s fp     %7.3fs (%g)\n", arch, now() - t, r2);
    t = now(); uint64_t r3 = bench_mem(60);          printf("%-8s mem    %7.3fs (%llu)\n", arch, now() - t, (unsigned long long)r3);
    t = now();
    for (int i = 0; i < 8; i++) th[i] = CreateThread( NULL, 0, worker, NULL, 0, NULL );
    WaitForMultipleObjects( 8, th, TRUE, INFINITE );
    printf("%-8s atomic %7.3fs (%lld)\n", arch, now() - t, (long long)counter);
    return 0;
}
