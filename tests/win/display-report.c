/* What a game learns about the displays, without opening a window: each monitor's rectangle, its
 * current mode and its largest mode, the primary screen's size, and the DPI, as a DPI-aware game
 * sees them. Compare a run with HADRON_RETINA=1 against one without (findings #45).
 * Usage: display-report.exe */
#define _WIN32_WINNT 0x0A00
#include <stdio.h>
#include <windows.h>

static BOOL CALLBACK report_monitor( HMONITOR monitor, HDC hdc, RECT *rect, LPARAM param )
{
    MONITORINFOEXA info = {.cbSize = sizeof(info)};
    DEVMODEA mode = {.dmSize = sizeof(mode)};
    DWORD i, count = 0, max_w = 0, max_h = 0;

    GetMonitorInfoA( monitor, (MONITORINFO *)&info );
    printf( "%s: monitor %ldx%ld at %ld,%ld", info.szDevice, info.rcMonitor.right - info.rcMonitor.left,
            info.rcMonitor.bottom - info.rcMonitor.top, info.rcMonitor.left, info.rcMonitor.top );
    if (EnumDisplaySettingsA( info.szDevice, ENUM_CURRENT_SETTINGS, &mode ))
        printf( ", current mode %lux%lu @ %lu Hz", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency );
    for (i = 0; EnumDisplaySettingsA( info.szDevice, i, &mode ); i++, count++)
        if (mode.dmPelsWidth * mode.dmPelsHeight > max_w * max_h) { max_w = mode.dmPelsWidth; max_h = mode.dmPelsHeight; }
    printf( ", %lu modes up to %lux%lu\n", count, max_w, max_h );
    return TRUE;
}

int main( void )
{
    HDC hdc;

    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    EnumDisplayMonitors( NULL, NULL, report_monitor, 0 );
    hdc = GetDC( NULL );
    printf( "primary screen %dx%d, HORZRES %d, system DPI %u\n", GetSystemMetrics( SM_CXSCREEN ),
            GetSystemMetrics( SM_CYSCREEN ), GetDeviceCaps( hdc, HORZRES ), GetDpiForSystem() );
    ReleaseDC( NULL, hdc );
    return 0;
}
