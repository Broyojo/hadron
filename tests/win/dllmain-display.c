/* A DLL whose DllMain uses the display, so that the display driver is loaded under the loader lock
 * (Forza Horizon 4, findings #76), and an executable that imports it. The DllMain gets the screen
 * size, a screen DC and a window, prints how long that took, and with DM_SLEEP set waits 7 seconds
 * more: a thread that winemac.drv starts in its own DllMain when SteamGameId is set then reports
 * that it waits for the loader lock, as it does in the game's log.
 * Build: x86_64-w64-mingw32-clang -O1 -DDLL -shared dllmain-display.c -o dllmain-display.dll \
 *            -luser32 -lgdi32 -Wl,--out-implib,libdllmain-display.a
 *        x86_64-w64-mingw32-clang -O1 dllmain-display.c -o dllmain-display.exe \
 *            -L. -ldllmain-display
 * Usage: [SteamGameId=1] [DM_SLEEP=1] wine dllmain-display.exe */
#include <stdio.h>
#include <windows.h>

#ifdef DLL

BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, void *reserved )
{
    DWORD start = GetTickCount();
    HWND hwnd;
    HDC dc;

    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    fprintf( stderr, "dllmain: thread %04lx\n", GetCurrentThreadId() );
    fprintf( stderr, "dllmain: screen %d x %d\n", GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) );
    dc = GetDC( 0 );
    fprintf( stderr, "dllmain: %d bits per pixel\n", GetDeviceCaps( dc, BITSPIXEL ) );
    ReleaseDC( 0, dc );
    if ((hwnd = CreateWindowExA( 0, "static", "dllmain", WS_POPUP, 0, 0, 10, 10, 0, 0, 0, 0 )))
        DestroyWindow( hwnd );
    fprintf( stderr, "dllmain: window %p, done after %lu ms\n", hwnd, GetTickCount() - start );
    if (GetEnvironmentVariableA( "DM_SLEEP", NULL, 0 )) Sleep( 7000 );
    return TRUE;
}

__declspec(dllexport) int dllmain_display(void)
{
    return 1;
}

#else

__declspec(dllimport) int dllmain_display(void);

int main(void)
{
    fprintf( stderr, "main: thread %04lx, %d\n", GetCurrentThreadId(), dllmain_display() );
    return 0;
}

#endif
