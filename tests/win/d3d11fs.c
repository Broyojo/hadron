/* A D3D11 swapchain in exclusive fullscreen at a given size, clearing to a solid colour, and the
 * window's rectangle and the monitor's as Win32 reports them every two seconds: d3d9fs.c for DXGI.
 * It can go on presenting in a window after leaving fullscreen. A mode of another shape than the
 * screen's is shown letterboxed on Windows (findings #66).
 * Usage: d3d11fs.exe <width> <height> [seconds] [seconds windowed afterwards]
 * Build: x86_64-w64-mingw32-clang -O2 d3d11fs.c -o d3d11fs.exe -ld3d11 -ldxgi */
#define COBJMACROS
#define INITGUID
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d11.h>

static void report( const char *when, HWND hwnd )
{
    RECT rect, client;
    MONITORINFO info = {.cbSize = sizeof(info)};

    GetWindowRect( hwnd, &rect );
    GetClientRect( hwnd, &client );
    GetMonitorInfoA( MonitorFromWindow( hwnd, MONITOR_DEFAULTTONEAREST ), &info );
    printf( "%s: window (%ld,%ld)-(%ld,%ld) client %ldx%ld, monitor (%ld,%ld)-(%ld,%ld)%s%s\n", when, rect.left,
            rect.top, rect.right, rect.bottom, client.right, client.bottom, info.rcMonitor.left, info.rcMonitor.top,
            info.rcMonitor.right, info.rcMonitor.bottom, GetForegroundWindow() == hwnd ? ", foreground" : "",
            IsIconic( hwnd ) ? ", minimized" : "" );
}

int main( int argc, char **argv )
{
    static const float colour[4] = {40 / 255.0f, 160 / 255.0f, 220 / 255.0f, 1.0f};
    DXGI_SWAP_CHAIN_DESC desc = {0};
    ID3D11RenderTargetView *view;
    ID3D11DeviceContext *context;
    IDXGISwapChain *swapchain;
    ID3D11Texture2D *buffer;
    ID3D11Device *device;
    BOOL fullscreen;
    HRESULT hr;
    HWND hwnd;
    MSG msg;
    int seconds, windowed, frames = 0;
    DWORD start, last;
    char when[32];

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc < 3) { printf( "usage: d3d11fs.exe <width> <height> [seconds] [seconds windowed]\n" ); return 2; }
    seconds = argc > 3 ? atoi( argv[3] ) : 6;
    windowed = argc > 4 ? atoi( argv[4] ) : 0;
    hwnd = CreateWindowExA( 0, "STATIC", "d3d11fs", WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, NULL, NULL, NULL, NULL );
    desc.BufferDesc.Width = atoi( argv[1] );
    desc.BufferDesc.Height = atoi( argv[2] );
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = hwnd;
    desc.Windowed = FALSE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    hr = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &desc,
                                        &swapchain, &device, NULL, &context );
    printf( "D3D11CreateDeviceAndSwapChain %ux%u fullscreen: %#lx\n", desc.BufferDesc.Width, desc.BufferDesc.Height, hr );
    if (FAILED( hr )) return 1;
    IDXGISwapChain_GetFullscreenState( swapchain, &fullscreen, NULL );
    printf( "fullscreen state %d\n", fullscreen );
    IDXGISwapChain_GetBuffer( swapchain, 0, &IID_ID3D11Texture2D, (void **)&buffer );
    ID3D11Device_CreateRenderTargetView( device, (ID3D11Resource *)buffer, NULL, &view );
    ID3D11Texture2D_Release( buffer );
    report( "after creation", hwnd );
    start = last = GetTickCount();
    while (GetTickCount() - start < (DWORD)seconds * 1000)
    {
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        ID3D11DeviceContext_OMSetRenderTargets( context, 1, &view, NULL );
        ID3D11DeviceContext_ClearRenderTargetView( context, view, colour );
        IDXGISwapChain_Present( swapchain, 1, 0 );
        if (++frames == 60) report( "after 60 frames", hwnd );
        if (GetTickCount() - last >= 2000)
        {
            last = GetTickCount();
            sprintf( when, "at %lu s", (last - start) / 1000 );
            report( when, hwnd );
        }
    }
    report( "at the end", hwnd );
    IDXGISwapChain_SetFullscreenState( swapchain, FALSE, NULL );
    report( "after leaving fullscreen", hwnd );
    start = GetTickCount();
    while (GetTickCount() - start < (DWORD)windowed * 1000)
    {
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        ID3D11DeviceContext_OMSetRenderTargets( context, 1, &view, NULL );
        ID3D11DeviceContext_ClearRenderTargetView( context, view, colour );
        IDXGISwapChain_Present( swapchain, 1, 0 );
    }
    if (windowed) report( "windowed at the end", hwnd );
    ID3D11RenderTargetView_Release( view );
    IDXGISwapChain_Release( swapchain );
    ID3D11DeviceContext_Release( context );
    ID3D11Device_Release( device );
    DestroyWindow( hwnd );
    return 0;
}
