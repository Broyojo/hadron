/* A window drawn with GDI only, in fullscreen at a display mode of another shape than the screen's:
 * a mode change, a popup covering the monitor, and the present rect Wine's own D3D sets for exclusive
 * fullscreen (D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE). The window is filled with one colour and its
 * top-left quarter with another, so a capture shows where the window surface lands: scaled into the
 * letterbox, with black around it (findings #66). Without a present rect ("norect") the window is an
 * ordinary fullscreen popup, the case that has no letterbox bars of its own.
 * Usage: letterbox-gdi.exe <width> <height> [seconds] [norect]
 * Build: x86_64-w64-mingw32-clang -O2 letterbox-gdi.c -o letterbox-gdi.exe -lgdi32 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* Wine's escape (include/ddk/d3dkmthk.h), and the D3DKMT_ESCAPE that D3DKMTEscape takes. */
#define D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE 0x80000001
typedef struct
{
    UINT hAdapter, hDevice, Type, Flags;
    void *pPrivateDriverData;
    UINT PrivateDriverDataSize, hContext;
} D3DKMT_ESCAPE;
LONG WINAPI D3DKMTEscape( const D3DKMT_ESCAPE *desc );

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT client, quarter;
        HDC hdc = BeginPaint( hwnd, &ps );
        HBRUSH fill = CreateSolidBrush( RGB( 40, 160, 220 ) ), mark = CreateSolidBrush( RGB( 220, 40, 40 ) );

        GetClientRect( hwnd, &client );
        FillRect( hdc, &client, fill );
        SetRect( &quarter, 0, 0, client.right / 2, client.bottom / 2 );
        FillRect( hdc, &quarter, mark );
        DeleteObject( fill );
        DeleteObject( mark );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcA( hwnd, msg, wparam, lparam );
}

static BOOL set_present_rect( HWND hwnd, RECT rect )
{
    D3DKMT_ESCAPE escape = {0};

    escape.Type = D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE;
    escape.hContext = HandleToULong( hwnd );
    escape.pPrivateDriverData = &rect;
    escape.PrivateDriverDataSize = sizeof(rect);
    return !D3DKMTEscape( &escape );
}

static void report( const char *when, HWND hwnd )
{
    RECT rect;
    MONITORINFO info = {.cbSize = sizeof(info)};

    GetWindowRect( hwnd, &rect );
    GetMonitorInfoA( MonitorFromWindow( hwnd, MONITOR_DEFAULTTONEAREST ), &info );
    printf( "%s: window (%ld,%ld)-(%ld,%ld), monitor (%ld,%ld)-(%ld,%ld)\n", when, rect.left, rect.top, rect.right,
            rect.bottom, info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom );
}

int main( int argc, char **argv )
{
    WNDCLASSA cls = {.lpfnWndProc = wndproc, .lpszClassName = "letterbox-gdi"};
    DEVMODEA mode = {.dmSize = sizeof(mode)};
    MONITORINFO info = {.cbSize = sizeof(info)};
    RECT empty = {0};
    int seconds, use_rect;
    DWORD start;
    LONG ret;
    HWND hwnd;
    MSG msg;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc < 3) { printf( "usage: letterbox-gdi.exe <width> <height> [seconds] [norect]\n" ); return 2; }
    seconds = argc > 3 ? atoi( argv[3] ) : 6;
    use_rect = !(argc > 4 && !strcmp( argv[4], "norect" ));

    mode.dmPelsWidth = atoi( argv[1] );
    mode.dmPelsHeight = atoi( argv[2] );
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    ret = ChangeDisplaySettingsExA( NULL, &mode, NULL, CDS_FULLSCREEN, NULL );
    printf( "ChangeDisplaySettingsEx %lux%lu: %ld\n", mode.dmPelsWidth, mode.dmPelsHeight, ret );
    if (ret != DISP_CHANGE_SUCCESSFUL) return 1;

    RegisterClassA( &cls );
    GetMonitorInfoA( MonitorFromPoint( (POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY ), &info );
    hwnd = CreateWindowExA( WS_EX_TOPMOST, "letterbox-gdi", "letterbox-gdi", WS_POPUP, info.rcMonitor.left,
                            info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
                            info.rcMonitor.bottom - info.rcMonitor.top, NULL, NULL, NULL, NULL );
    if (use_rect) printf( "present rect: %d\n", set_present_rect( hwnd, info.rcMonitor ) );
    ShowWindow( hwnd, SW_SHOW );
    SetWindowPos( hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED );
    UpdateWindow( hwnd );
    report( "shown", hwnd );

    start = GetTickCount();
    while (GetTickCount() - start < (DWORD)seconds * 1000)
    {
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        Sleep( 10 );
    }
    if (use_rect) set_present_rect( hwnd, empty );
    DestroyWindow( hwnd );
    ChangeDisplaySettingsExA( NULL, NULL, NULL, 0, NULL );
    report( "restored", GetDesktopWindow() );
    return 0;
}
