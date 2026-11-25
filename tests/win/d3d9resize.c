/* A windowed D3D9 device that binds its back buffer and depth surface by hand, as Portal 2 does,
 * then has its window resized while it keeps presenting (issue #177, findings #69). Through
 * mtld3d before 0.15 the resize freed the back buffer the saved binding still named, and the next
 * frame's submission faulted on mtld3d's submit thread: nothing was shown again.
 * Each frame clears to a colour that changes with every resize; after each resize the test reads
 * the back buffer's middle pixel back (GetRenderTargetData) and compares it.
 * Usage: d3d9resize.exe [frames per size]   (default 60)
 * Build: i686-w64-mingw32-clang -O2 d3d9resize.c -o d3d9resize.exe -ld3d9 */
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

/* The middle pixel of the back buffer, read through a system-memory copy. */
static HRESULT read_middle( IDirect3DDevice9 *device, D3DCOLOR *color, UINT *width, UINT *height )
{
    IDirect3DSurface9 *bb, *copy;
    D3DSURFACE_DESC desc;
    D3DLOCKED_RECT lr;
    HRESULT hr;

    if (FAILED(hr = IDirect3DDevice9_GetBackBuffer( device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb ))) return hr;
    IDirect3DSurface9_GetDesc( bb, &desc );
    *width = desc.Width;
    *height = desc.Height;
    hr = IDirect3DDevice9_CreateOffscreenPlainSurface( device, desc.Width, desc.Height, desc.Format,
                                                       D3DPOOL_SYSTEMMEM, &copy, NULL );
    if (SUCCEEDED(hr))
    {
        if (SUCCEEDED(hr = IDirect3DDevice9_GetRenderTargetData( device, bb, copy ))
            && SUCCEEDED(hr = IDirect3DSurface9_LockRect( copy, &lr, NULL, D3DLOCK_READONLY )))
        {
            *color = ((DWORD *)((BYTE *)lr.pBits + lr.Pitch * (desc.Height / 2)))[desc.Width / 2] & 0xffffff;
            IDirect3DSurface9_UnlockRect( copy );
        }
        IDirect3DSurface9_Release( copy );
    }
    IDirect3DSurface9_Release( bb );
    return hr;
}

int main( int argc, char **argv )
{
    static const SIZE sizes[] = { {800, 600}, {1024, 640}, {640, 480}, {1280, 720} };
    static const D3DCOLOR colors[] = { 0x2050c0, 0x20c050, 0xc05020, 0xc0c020 };
    WNDCLASSA wc = { 0, wndproc, 0, 0, GetModuleHandleA( NULL ), NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "d3d9resize" };
    int per_size = argc > 1 ? atoi( argv[1] ) : 60, failures = 0, s, f;
    D3DPRESENT_PARAMETERS pp = { 0 };
    IDirect3DSurface9 *bb, *depth;
    IDirect3DDevice9 *device;
    IDirect3D9 *d3d;
    HRESULT hr;
    HWND hwnd;
    MSG msg;

    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "d3d9resize", "d3d9resize", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          100, 100, sizes[0].cx, sizes[0].cy, NULL, NULL, wc.hInstance, NULL );
    if (!(d3d = Direct3DCreate9( D3D_SDK_VERSION ))) { printf( "Direct3DCreate9 failed\n" ); return 1; }
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    hr = IDirect3D9_CreateDevice( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                  D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device );
    printf( "CreateDevice hr %#lx\n", hr );
    if (FAILED(hr)) return 1;

    /* What the games do: bind the implicit surfaces by hand, then drop the references. */
    IDirect3DDevice9_GetBackBuffer( device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb );
    IDirect3DDevice9_GetDepthStencilSurface( device, &depth );
    printf( "SetRenderTarget hr %#lx, SetDepthStencilSurface hr %#lx\n",
            IDirect3DDevice9_SetRenderTarget( device, 0, bb ), IDirect3DDevice9_SetDepthStencilSurface( device, depth ) );
    IDirect3DSurface9_Release( bb );
    IDirect3DSurface9_Release( depth );

    for (s = 0; s < (int)(sizeof(sizes) / sizeof(*sizes)); s++)
    {
        D3DCOLOR got = 0;
        UINT width = 0, height = 0;
        RECT client;

        if (s)
        {
            RECT rect = { 0, 0, sizes[s].cx, sizes[s].cy };
            AdjustWindowRect( &rect, WS_OVERLAPPEDWINDOW, FALSE );
            SetWindowPos( hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER );
        }
        for (f = 0; f < per_size; f++)
        {
            while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
            IDirect3DDevice9_Clear( device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colors[s], 1.0f, 0 );
            IDirect3DDevice9_BeginScene( device );
            IDirect3DDevice9_EndScene( device );
            hr = IDirect3DDevice9_Present( device, NULL, NULL, NULL, NULL );
            if (FAILED(hr)) { printf( "size %d frame %d: Present hr %#lx\n", s, f, hr ); failures++; break; }
        }
        GetClientRect( hwnd, &client );
        /* The last Present left the back buffer undefined (DISCARD): clear it once more to read it. */
        IDirect3DDevice9_Clear( device, 0, NULL, D3DCLEAR_TARGET, colors[s], 1.0f, 0 );
        hr = read_middle( device, &got, &width, &height );
        printf( "size %d: client %ldx%ld, back buffer %ux%u, %d frames, middle pixel %06lx (want %06lx), hr %#lx\n",
                s, client.right, client.bottom, width, height, f, got, colors[s], hr );
        if (FAILED(hr) || got != colors[s]) failures++;
    }
    printf( "%s: %d failures\n", failures ? "FAIL" : "PASS", failures );
    IDirect3DDevice9_Release( device );
    IDirect3D9_Release( d3d );
    DestroyWindow( hwnd );
    return failures != 0;
}
