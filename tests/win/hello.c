/* Minimal Windows test program: reports its architecture and exercises a few APIs. */
#define _WIN32_WINNT 0x0A00
#include <stdio.h>
#include <windows.h>

int main(void)
{
    SYSTEM_INFO si;
    USHORT process_machine, native_machine;
    char buf[MAX_PATH];

    GetNativeSystemInfo( &si );
    IsWow64Process2( GetCurrentProcess(), &process_machine, &native_machine );
    GetModuleFileNameA( NULL, buf, sizeof(buf) );
#if defined(__x86_64__)
    const char *arch = "x86_64";
#elif defined(__i386__)
    const char *arch = "i386";
#elif defined(__aarch64__)
    const char *arch = "aarch64";
#endif
    printf( "hello from %s code: %s\n", arch, buf );
    printf( "native machine %04x, tick count %lu, %lu CPUs\n", native_machine, GetTickCount(), si.dwNumberOfProcessors );
    return 0;
}
