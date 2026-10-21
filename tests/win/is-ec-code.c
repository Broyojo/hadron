/* RtlIsEcCode must answer for any address, as an unwinder asks it about whatever value it finds
 * as a return address (issue #95: Hogwarts Legacy unwinds through frames holding non-canonical
 * values, and the fault inside RtlIsEcCode repeats until the stack overflows). Checks that this
 * program's own x64 code is not EC code and that addresses at the top of the 47-bit user space and
 * beyond it return FALSE without faulting. Exit 0 when all hold; a fault ends the program with an
 * exception. Build:
 *   x86_64-w64-mingw32-clang -O2 is-ec-code.c -o is-ec-code.exe
 * Usage: is-ec-code.exe */
#include <windows.h>
#include <stdio.h>

static BOOLEAN (WINAPI *pRtlIsEcCode)(ULONG_PTR);

static int check(const char *name, ULONG_PTR addr, BOOLEAN expected)
{
    BOOLEAN ret;

    /* a fault in RtlIsEcCode ends the program; the line says which address caused it */
    printf("%s (%#llx)...\n", name, (unsigned long long)addr);
    fflush(stdout);
    ret = pRtlIsEcCode(addr);
    printf("%s %s (%#llx): %d\n", ret == expected ? "ok  " : "FAIL", name, (unsigned long long)addr, ret);
    return ret != expected;
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    int failed = 0;

    pRtlIsEcCode = (void *)GetProcAddress(ntdll, "RtlIsEcCode");
    if (!pRtlIsEcCode) { printf("SKIP no RtlIsEcCode: not an ARM64EC ntdll\n"); return 0; }

    failed |= check("this program's main", (ULONG_PTR)main, FALSE);
    failed |= check("last page of user space", 0x7ffffffff000ull, FALSE);
    failed |= check("1 << 47", 1ull << 47, FALSE);
    failed |= check("value from #95", 0x24f24f0d7007e400ull, FALSE);
    failed |= check("all bits set", ~0ull, FALSE);
    printf(failed ? "FAIL\n" : "PASS\n");
    return failed;
}
