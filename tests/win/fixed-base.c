/* An x64 executable that can only load at its preferred base: base 0x140000000, about 0x1bcc2000
 * bytes like Hogwarts Legacy's (issue #95), no relocations. Started with an argument it starts
 * itself that many times as a child, the way a launcher starts a game, and counts how many of
 * the children's loads failed (CreateProcess's error; c0000018 when the range is taken).
 * Build: x86_64-w64-mingw32-clang -O1 fixed-base.c -o fixed-base.exe \
 *        -Wl,--image-base,0x140000000 -Wl,--disable-dynamicbase -Wl,--disable-reloc-section */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* Uninitialised data makes the image big without making the file big. */
static char image_bulk[0x1bc00000];

int main( int argc, char **argv )
{
    char cmd[MAX_PATH + 16], self[MAX_PATH];
    int i, count, failed = 0;

    image_bulk[argc] = 1;
    if (argc < 2) return image_bulk[1] ? 0 : 1;  /* a child: loading was the test */
    count = atoi( argv[1] );
    GetModuleFileNameA( NULL, self, sizeof(self) );
    snprintf( cmd, sizeof(cmd), "\"%s\"", self );
    printf( "image at %p, size %#lx\n", GetModuleHandleA( NULL ), (unsigned long)sizeof(image_bulk) );
    for (i = 0; i < count; i++)
    {
        STARTUPINFOA si = {.cb = sizeof(si)};
        PROCESS_INFORMATION pi;

        if (!CreateProcessA( self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            printf( "start %d: CreateProcess failed, error %lu\n", i, GetLastError() );
            failed++;
            continue;
        }
        WaitForSingleObject( pi.hProcess, INFINITE );
        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
    }
    printf( "%d of %d starts failed\n", failed, count );
    return failed ? 1 : 0;
}
