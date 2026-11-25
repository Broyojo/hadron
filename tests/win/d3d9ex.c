/* Direct3D 9Ex smoke test: what GameMaker's D3D9 runner needs (issue #88, findings #69).
 * Direct3DCreate9Ex, CreateDeviceEx on a window, GetDisplayModeEx, CheckDeviceState, a few frames
 * of Clear and PresentEx read back through GetRenderTargetData, ResetEx to another size, and the
 * 9Ex rule that a D3DPOOL_MANAGED texture is refused.
 * Usage: d3d9ex.exe [frames]   (default 120)
 * Build: i686-w64-mingw32-clang -O2 d3d9ex.c -o d3d9ex.exe -ld3d9 */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d9.h>

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_DESTROY) PostQuitMessage( 0 );
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static int failures;

static void check( const char *what, HRESULT hr, HRESULT want )
{
    printf( "%s: hr %#lx%s\n", what, hr, hr == want ? "" : "  <-- unexpected" );
    if (hr != want) failures++;
}

/* Clear and PresentEx `frames` times, then read the colour back from the back buffer. */
static void frames_of( IDirect3DDevice9Ex *device, int frames, D3DCOLOR color )
{
    IDirect3DSurface9 *bb, *copy;
    D3DSURFACE_DESC desc;
    D3DLOCKED_RECT lr;
    DWORD got = 0;
    HRESULT hr = S_OK;
    MSG msg;
    int f;

    for (f = 0; f < frames && SUCCEEDED(hr); f++)
    {
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        IDirect3DDevice9Ex_Clear( device, 0, NULL, D3DCLEAR_TARGET, color, 1.0f, 0 );
        IDirect3DDevice9Ex_BeginScene( device );
        IDirect3DDevice9Ex_EndScene( device );
        hr = IDirect3DDevice9Ex_PresentEx( device, NULL, NULL, NULL, NULL, 0 );
    }
    check( "PresentEx", hr, S_OK );
    IDirect3DDevice9Ex_Clear( device, 0, NULL, D3DCLEAR_TARGET, color, 1.0f, 0 );
    IDirect3DDevice9Ex_GetBackBuffer( device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb );
    IDirect3DSurface9_GetDesc( bb, &desc );
    hr = IDirect3DDevice9Ex_CreateOffscreenPlainSurface( device, desc.Width, desc.Height, desc.Format,
                                                         D3DPOOL_SYSTEMMEM, &copy, NULL );
    if (SUCCEEDED(hr))
    {
        if (SUCCEEDED(hr = IDirect3DDevice9Ex_GetRenderTargetData( device, bb, copy ))
            && SUCCEEDED(hr = IDirect3DSurface9_LockRect( copy, &lr, NULL, D3DLOCK_READONLY )))
        {
            got = ((DWORD *)((BYTE *)lr.pBits + lr.Pitch * (desc.Height / 2)))[desc.Width / 2] & 0xffffff;
            IDirect3DSurface9_UnlockRect( copy );
        }
        IDirect3DSurface9_Release( copy );
    }
    IDirect3DSurface9_Release( bb );
    printf( "%d frames at %ux%u, middle pixel %06lx (want %06lx), hr %#lx\n", f, desc.Width, desc.Height, got,
            color & 0xffffff, hr );
    if (FAILED(hr) || got != (color & 0xffffff)) failures++;
}

int main( int argc, char **argv )
{
    WNDCLASSA wc = { 0, wndproc, 0, 0, GetModuleHandleA( NULL ), NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "d3d9ex" };
    int frames = argc > 1 ? atoi( argv[1] ) : 120;
    D3DDISPLAYMODEEX mode = { sizeof(mode) };
    D3DPRESENT_PARAMETERS pp = { 0 };
    D3DDISPLAYROTATION rotation;
    IDirect3DDevice9Ex *device;
    IDirect3DTexture9 *texture;
    IDirect3D9Ex *d3d;
    HRESULT hr;
    HWND hwnd;

    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "d3d9ex", "d3d9ex", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 800, 600,
                          NULL, NULL, wc.hInstance, NULL );
    hr = Direct3DCreate9Ex( D3D_SDK_VERSION, &d3d );
    check( "Direct3DCreate9Ex", hr, S_OK );
    if (FAILED(hr)) return 1;
    hr = IDirect3D9Ex_GetAdapterDisplayModeEx( d3d, D3DADAPTER_DEFAULT, &mode, &rotation );
    printf( "GetAdapterDisplayModeEx: hr %#lx, %ux%u at %u Hz, format %u, rotation %u\n", hr, mode.Width,
            mode.Height, mode.RefreshRate, mode.Format, rotation );
    if (FAILED(hr)) failures++;

    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    hr = IDirect3D9Ex_CreateDeviceEx( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, NULL, &device );
    check( "CreateDeviceEx", hr, S_OK );
    if (FAILED(hr)) return 1;
    check( "CheckDeviceState", IDirect3DDevice9Ex_CheckDeviceState( device, hwnd ), S_OK );
    check( "SetMaximumFrameLatency(2)", IDirect3DDevice9Ex_SetMaximumFrameLatency( device, 2 ), S_OK );
    check( "CreateTexture(D3DPOOL_MANAGED)",
           IDirect3DDevice9Ex_CreateTexture( device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL ),
           D3DERR_INVALIDCALL );
    frames_of( device, frames, 0x3080e0 );

    pp.BackBufferWidth = 1024;
    pp.BackBufferHeight = 640;
    check( "ResetEx to 1024x640", IDirect3DDevice9Ex_ResetEx( device, &pp, NULL ), S_OK );
    frames_of( device, frames, 0xe08030 );

    printf( "%s: %d failures\n", failures ? "FAIL" : "PASS", failures );
    IDirect3DDevice9Ex_Release( device );
    IDirect3D9Ex_Release( d3d );
    DestroyWindow( hwnd );
    return failures != 0;
}
