/* A program with a writable shared section, as GameMaker runners have (issue #74), must load and
 * see its initial data; it then starts a second instance and reports what that one sees. Built with
 * 16 KB section alignment the section is shared (exit 0); at the usual 4 KB it gets a private copy
 * on a 16 KB host (exit 2). Build:
 *   i686-w64-mingw32-clang -O2 shared-section.c -Xlinker -Xlink=-section:.shr,RWS [-Wl,--section-alignment,0x4000]
 * Linked with -Xlink=-merge:.idata=.shr as well, the import table is inside the shared section.
 * Usage: shared-section.exe */
#include <windows.h>
#include <stdio.h>
#include <string.h>

__attribute__((section(".shr"))) volatile LONG shared_value = 0x1234;

int main(int argc, char **argv)
{
    char cmd[MAX_PATH + 16];
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    DWORD code;

    if (argc > 1 && !strcmp(argv[1], "child")) return shared_value;

    if (shared_value != 0x1234) { printf("FAIL initial value %lx\n", shared_value); return 1; }
    shared_value = 0x5678;
    if (shared_value != 0x5678) { printf("FAIL write\n"); return 1; }

    cmd[0] = '"';
    GetModuleFileNameA(NULL, cmd + 1, MAX_PATH);
    strcat(cmd, "\" child");
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    { printf("FAIL CreateProcess %lu\n", GetLastError()); return 1; }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    printf("PASS loaded and writable; a second instance sees %s\n",
           code == 0x5678 ? "the first one's write (shared)" : code == 0x1234 ? "the initial value (private copy)" : "something else");
    return code == 0x5678 ? 0 : code == 0x1234 ? 2 : 3;
}
