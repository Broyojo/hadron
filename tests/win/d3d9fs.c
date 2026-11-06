/* A D3D9 device in exclusive fullscreen at a given size, clearing to a solid colour, then the
 * window's rectangle and the monitor's as Win32 reports them. Through mtld3d a 16:9 mode on a
 * 1.54 panel left Five Nights at Freddy's in the middle of the screen (findings #56).
 * Usage: d3d9fs.exe <width> <height> [seconds]
 * Build: i686-w64-mingw32-clang -O2 d3d9fs.c -o d3d9fs.exe -ld3d9 */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d9.h>

static void report( const char *when, HWND hwnd )
{
    RECT rect, client;
    MONITORINFO info = {.cbSize = sizeof(info)};

    GetWindowRect( hwnd, &rect );
    GetClientRect( hwnd, &client );
    GetMonitorInfoA( MonitorFromWindow( hwnd, MONITOR_DEFAULTTONEAREST ), &info );
    printf( "%s: window (%ld,%ld)-(%ld,%ld) client %ldx%ld, monitor (%ld,%ld)-(%ld,%ld)\n", when, rect.left, rect.top,
            rect.right, rect.bottom, client.right, client.bottom, info.rcMonitor.left, info.rcMonitor.top,
            info.rcMonitor.right, info.rcMonitor.bottom );
}

int main( int argc, char **argv )
{
    D3DPRESENT_PARAMETERS pp = {0};
    IDirect3DDevice9 *device;
    IDirect3D9 *d3d;
    HRESULT hr;
    HWND hwnd;
    MSG msg;
    int seconds, frames = 0;
    DWORD start;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc < 3) { printf( "usage: d3d9fs.exe <width> <height> [seconds]\n" ); return 2; }
    seconds = argc > 3 ? atoi( argv[3] ) : 6;
    hwnd = CreateWindowExA( 0, "STATIC", "d3d9fs", WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, NULL, NULL, NULL, NULL );
    d3d = Direct3DCreate9( D3D_SDK_VERSION );
    pp.BackBufferWidth = atoi( argv[1] );
    pp.BackBufferHeight = atoi( argv[2] );
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = FALSE;
    pp.FullScreen_RefreshRateInHz = 60;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    hr = IDirect3D9_CreateDevice( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device );
    printf( "CreateDevice %ux%u fullscreen: %#lx\n", pp.BackBufferWidth, pp.BackBufferHeight, hr );
    if (FAILED( hr )) return 1;
    report( "after CreateDevice", hwnd );
    start = GetTickCount();
    while (GetTickCount() - start < (DWORD)seconds * 1000)
    {
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        IDirect3DDevice9_Clear( device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB( 40, 160, 220 ), 1.0f, 0 );
        IDirect3DDevice9_Present( device, NULL, NULL, NULL, NULL );
        if (++frames == 60) report( "after 60 frames", hwnd );
    }
    report( "at the end", hwnd );
    IDirect3DDevice9_Release( device );
    IDirect3D9_Release( d3d );
    DestroyWindow( hwnd );
    return 0;
}
