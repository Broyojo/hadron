/* A display mode whose size has no common factor with the screen's: Teardown on a Sidecar iPad
 * aborted in win32u's make_ratio ("num / d < 65536") when it asked for the monitor's DPI after
 * switching to such a mode (findings #54). For each monitor, sets every mode it lists (Hadron
 * emulates the switch; the screen itself does not change), asks for the raw DPI and maps a
 * window's rectangle, then restores the mode. A Wine without the fix aborts on the first such mode.
 * Usage: odd-mode-dpi.exe [width height]   (only that mode, if given) */
#define _WIN32_WINNT 0x0A00
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <shellscalingapi.h>

static int only_w, only_h, tried, failed;

static void try_mode( HMONITOR monitor, const char *device, DEVMODEA *mode )
{
    UINT dpi_x = 0, dpi_y = 0;
    LONG ret;
    HWND hwnd;
    RECT rect;
    MONITORINFO info = {.cbSize = sizeof(info)};

    mode->dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    ret = ChangeDisplaySettingsExA( device, mode, NULL, CDS_FULLSCREEN, NULL );
    printf( "  %lux%lu: switch %ld", mode->dmPelsWidth, mode->dmPelsHeight, ret );
    fflush( stdout );
    if (ret == DISP_CHANGE_SUCCESSFUL)
    {
        tried++;
        GetDpiForMonitor( monitor, MDT_RAW_DPI, &dpi_x, &dpi_y );
        GetMonitorInfoA( monitor, &info );
        hwnd = CreateWindowA( "static", "odd-mode-dpi", WS_POPUP, info.rcMonitor.left, info.rcMonitor.top,
                              mode->dmPelsWidth, mode->dmPelsHeight, NULL, NULL, NULL, NULL );
        GetWindowRect( hwnd, &rect );
        printf( ", raw DPI %ux%u, monitor %ldx%ld, window %ldx%ld", dpi_x, dpi_y, info.rcMonitor.right - info.rcMonitor.left,
                info.rcMonitor.bottom - info.rcMonitor.top, rect.right - rect.left, rect.bottom - rect.top );
        if (rect.right - rect.left != (LONG)mode->dmPelsWidth || rect.bottom - rect.top != (LONG)mode->dmPelsHeight) failed++;
        DestroyWindow( hwnd );
    }
    printf( "\n" );
    fflush( stdout );
}

static BOOL CALLBACK each_monitor( HMONITOR monitor, HDC hdc, RECT *rect, LPARAM param )
{
    MONITORINFOEXA info = {.cbSize = sizeof(info)};
    DEVMODEA mode = {.dmSize = sizeof(mode)}, current = {.dmSize = sizeof(current)};
    DWORD i, last_w = 0, last_h = 0;

    GetMonitorInfoA( monitor, (MONITORINFO *)&info );
    EnumDisplaySettingsA( info.szDevice, ENUM_CURRENT_SETTINGS, &current );
    printf( "%s: current %lux%lu\n", info.szDevice, current.dmPelsWidth, current.dmPelsHeight );
    if (only_w)
    {
        mode.dmPelsWidth = only_w;
        mode.dmPelsHeight = only_h;
        try_mode( monitor, info.szDevice, &mode );
    }
    else for (i = 0; EnumDisplaySettingsA( info.szDevice, i, &mode ); i++)
    {
        if (mode.dmPelsWidth == last_w && mode.dmPelsHeight == last_h) continue;  /* other rates and depths */
        last_w = mode.dmPelsWidth;
        last_h = mode.dmPelsHeight;
        try_mode( monitor, info.szDevice, &mode );
        mode.dmSize = sizeof(mode);
    }
    ChangeDisplaySettingsExA( info.szDevice, NULL, NULL, 0, NULL );
    return TRUE;
}

int main( int argc, char **argv )
{
    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    if (argc == 3)
    {
        only_w = atoi( argv[1] );
        only_h = atoi( argv[2] );
    }
    EnumDisplayMonitors( NULL, NULL, each_monitor, 0 );
    printf( "%d modes set, %d with a window of the wrong size: %s\n", tried, failed, failed ? "FAIL" : "PASS" );
    return failed ? 1 : 0;
}
