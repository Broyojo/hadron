/* Fullscreen at a display mode, as a game does: change a monitor's mode, cover the monitor with a
 * popup window, and print what Win32 reports. Usage: fsmode.exe <\\.\DISPLAYn> <width> <height> [seconds] */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static const char *display;

static void report( const char *when, HWND hwnd )
{
    DEVMODEA cur = { .dmSize = sizeof(cur) };
    MONITORINFOEXA mi = { .cbSize = sizeof(mi) };
    RECT wr = {0}, cr = {0};
    POINT cursor;
    EnumDisplaySettingsA( display, ENUM_CURRENT_SETTINGS, &cur );
    if (hwnd)
    {
        GetMonitorInfoA( MonitorFromWindow( hwnd, MONITOR_DEFAULTTONEAREST ), (MONITORINFO *)&mi );
        GetWindowRect( hwnd, &wr ); GetClientRect( hwnd, &cr );
    }
    GetCursorPos( &cursor );
    printf( "%s: %s mode %lux%lu at %ld,%ld; window on %s (%ld,%ld)-(%ld,%ld); window (%ld,%ld)-(%ld,%ld) client %ldx%ld; cursor %ld,%ld\n",
            when, display, cur.dmPelsWidth, cur.dmPelsHeight, cur.dmPosition.x, cur.dmPosition.y, mi.szDevice,
            mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
            wr.left, wr.top, wr.right, wr.bottom, cr.right, cr.bottom, cursor.x, cursor.y );
}

int main( int argc, char **argv )
{
    DEVMODEA dm = { .dmSize = sizeof(dm), .dmFields = DM_PELSWIDTH | DM_PELSHEIGHT };
    DEVMODEA cur = { .dmSize = sizeof(cur) };
    int seconds = argc > 4 ? atoi( argv[4] ) : 4;
    LONG ret;
    HWND hwnd;
    MSG msg;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc < 4) { printf( "usage: fsmode.exe <display> <width> <height> [seconds]\n" ); return 2; }
    display = argv[1];
    dm.dmPelsWidth = atoi( argv[2] );
    dm.dmPelsHeight = atoi( argv[3] );
    report( "before", NULL );
    ret = ChangeDisplaySettingsExA( display, &dm, NULL, CDS_FULLSCREEN, NULL );
    printf( "ChangeDisplaySettingsEx(%lux%lu) = %ld\n", dm.dmPelsWidth, dm.dmPelsHeight, ret );
    EnumDisplaySettingsA( display, ENUM_CURRENT_SETTINGS, &cur );
    hwnd = CreateWindowExA( WS_EX_TOPMOST, "STATIC", "fsmode", WS_POPUP | WS_VISIBLE, cur.dmPosition.x, cur.dmPosition.y,
                            cur.dmPelsWidth, cur.dmPelsHeight, NULL, NULL, NULL, NULL );
    SetForegroundWindow( hwnd );
    for (int i = 0; i < seconds * 10; i++)
    {
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        Sleep( 100 );
        if (i == 10 || i == seconds * 10 - 1) report( i == 10 ? "1 s later" : "at the end", hwnd );
    }
    DestroyWindow( hwnd );
    ChangeDisplaySettingsExA( display, NULL, NULL, 0, NULL );
    report( "restored", NULL );
    return 0;
}
