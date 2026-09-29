/* D3D9 smoke test: fixed-function spinning triangle, reports adapter, device creation and FPS.
 * Each frame rewrites a dynamic vertex buffer (Lock/DISCARD), the path games hit hardest.
 * Usage: d3d9tri.exe [seconds] [vsync]   (default 5 s, presentation interval immediate) */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <d3d9.h>

struct vertex { float x, y, z, rhw; DWORD color; };
#define VERTEX_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_DESTROY) PostQuitMessage( 0 );
    return DefWindowProcA( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    double seconds = argc > 1 ? atof( argv[1] ) : 5.0;
    BOOL vsync = argc > 2 && !strcmp( argv[2], "vsync" );
    WNDCLASSA wc = { 0, wndproc, 0, 0, GetModuleHandleA( NULL ), NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "d3d9tri" };
    static const float px[3] = { 0.0f, 0.6f, -0.6f }, py[3] = { 0.7f, -0.5f, -0.5f };
    static const DWORD colors[3] = { 0xffff3333, 0xff33ff33, 0xff3366ff };
    D3DPRESENT_PARAMETERS pp = { 0 };
    D3DADAPTER_IDENTIFIER9 id;
    IDirect3D9 *d3d;
    IDirect3DDevice9 *device;
    IDirect3DVertexBuffer9 *vb;
    LARGE_INTEGER freq, start, last, t;
    unsigned frames = 0, window_frames = 0, errors = 0;
    char module[MAX_PATH] = "";
    HRESULT hr;
    HWND hwnd;
    MSG msg;
    int i;

    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "d3d9tri", "Hadron D3D9 test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL );
    printf( "window %p\n", hwnd );

    if (!(d3d = Direct3DCreate9( D3D_SDK_VERSION ))) { printf( "Direct3DCreate9 failed\n" ); return 1; }
    GetModuleFileNameA( GetModuleHandleA( "d3d9.dll" ), module, sizeof(module) );
    printf( "d3d9 module %s\n", module );
    if (SUCCEEDED(IDirect3D9_GetAdapterIdentifier( d3d, D3DADAPTER_DEFAULT, 0, &id )))
        printf( "adapter \"%s\" driver \"%s\" vendor %04lx device %04lx\n",
                id.Description, id.Driver, id.VendorId, id.DeviceId );

    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    hr = IDirect3D9_CreateDevice( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                  D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device );
    printf( "CreateDevice hr %#lx\n", hr );
    if (FAILED(hr)) return 1;

    hr = IDirect3DDevice9_CreateVertexBuffer( device, 3 * sizeof(struct vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                              VERTEX_FVF, D3DPOOL_DEFAULT, &vb, NULL );
    if (FAILED(hr)) { printf( "CreateVertexBuffer failed %#lx\n", hr ); return 1; }
    IDirect3DDevice9_SetRenderState( device, D3DRS_LIGHTING, FALSE );
    IDirect3DDevice9_SetRenderState( device, D3DRS_CULLMODE, D3DCULL_NONE );
    IDirect3DDevice9_SetFVF( device, VERTEX_FVF );
    IDirect3DDevice9_SetStreamSource( device, 0, vb, 0, sizeof(struct vertex) );

    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    last = start;
    for (;;)
    {
        double elapsed, angle;
        struct vertex *v;

        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT) goto done;
            DispatchMessageA( &msg );
        }
        QueryPerformanceCounter( &t );
        elapsed = (double)(t.QuadPart - start.QuadPart) / freq.QuadPart;
        if (elapsed >= seconds) break;
        angle = elapsed * 2.0;

        if (SUCCEEDED(IDirect3DVertexBuffer9_Lock( vb, 0, 0, (void **)&v, D3DLOCK_DISCARD )))
        {
            for (i = 0; i < 3; i++)
            {
                float x = px[i] * cos( angle ) - py[i] * sin( angle );
                float y = px[i] * sin( angle ) + py[i] * cos( angle );
                v[i].x = (x * 0.75f + 1.0f) * 400.0f;
                v[i].y = (1.0f - y) * 300.0f;
                v[i].z = 0.5f;
                v[i].rhw = 1.0f;
                v[i].color = colors[i];
            }
            IDirect3DVertexBuffer9_Unlock( vb );
        }
        else errors++;

        IDirect3DDevice9_Clear( device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB( 25, 25, 38 ), 1.0f, 0 );
        IDirect3DDevice9_BeginScene( device );
        if (FAILED(IDirect3DDevice9_DrawPrimitive( device, D3DPT_TRIANGLELIST, 0, 1 ))) errors++;
        IDirect3DDevice9_EndScene( device );
        hr = IDirect3DDevice9_Present( device, NULL, NULL, NULL, NULL );
        if (FAILED(hr) && errors++ < 5) printf( "Present failed %#lx\n", hr );
        frames++;
        window_frames++;

        if ((double)(t.QuadPart - last.QuadPart) / freq.QuadPart >= 1.0)
        {
            printf( "%.1f fps\n", window_frames * (double)freq.QuadPart / (t.QuadPart - last.QuadPart) );
            window_frames = 0;
            last = t;
        }
    }
done:
    QueryPerformanceCounter( &t );
    printf( "%u frames in %.2f s: %.1f fps average, %u errors\n", frames,
            (double)(t.QuadPart - start.QuadPart) / freq.QuadPart,
            frames * (double)freq.QuadPart / (t.QuadPart - start.QuadPart), errors );
    IDirect3DVertexBuffer9_Release( vb );
    IDirect3DDevice9_Release( device );
    IDirect3D9_Release( d3d );
    DestroyWindow( hwnd );
    return 0;
}
