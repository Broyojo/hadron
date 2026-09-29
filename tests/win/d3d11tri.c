/* D3D11 smoke test: spinning triangle with runtime-compiled HLSL, reports FPS.
 * Usage: d3d11tri.exe [seconds]   (default 5) */
#define COBJMACROS
#define _WIN32_WINNT 0x0A00
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

static const char shader_src[] =
    "cbuffer cb : register(b0) { float angle; float3 pad; };\n"
    "struct vs_out { float4 pos : SV_Position; float3 col : COLOR; };\n"
    "vs_out vs(uint id : SV_VertexID) {\n"
    "    float2 p[3] = { float2(0, 0.7), float2(0.6, -0.5), float2(-0.6, -0.5) };\n"
    "    float3 c[3] = { float3(1, 0.2, 0.2), float3(0.2, 1, 0.2), float3(0.2, 0.4, 1) };\n"
    "    float s = sin(angle), co = cos(angle);\n"
    "    vs_out o;\n"
    "    o.pos = float4(p[id].x * co - p[id].y * s, p[id].x * s + p[id].y * co, 0, 1);\n"
    "    o.col = c[id];\n"
    "    return o;\n"
    "}\n"
    "float4 ps(vs_out i) : SV_Target { return float4(i.col, 1); }\n";

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_DESTROY) PostQuitMessage( 0 );
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static ID3DBlob *compile( const char *entry, const char *target )
{
    ID3DBlob *blob = NULL, *errors = NULL;
    HRESULT hr = D3DCompile( shader_src, sizeof(shader_src) - 1, "tri", NULL, NULL, entry, target, 0, 0, &blob, &errors );
    if (FAILED(hr))
    {
        printf( "D3DCompile(%s) failed %#lx: %s\n", entry, hr, errors ? (char *)ID3D10Blob_GetBufferPointer( errors ) : "" );
        exit( 1 );
    }
    return blob;
}

int main( int argc, char **argv )
{
    double seconds = argc > 1 ? atof( argv[1] ) : 5.0;
    WNDCLASSA wc = { 0, wndproc, 0, 0, GetModuleHandleA( NULL ), NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "d3d11tri" };
    DXGI_SWAP_CHAIN_DESC scd = { 0 };
    ID3D11Device *device; ID3D11DeviceContext *ctx; IDXGISwapChain *swapchain;
    ID3D11Texture2D *backbuffer; ID3D11RenderTargetView *rtv;
    ID3D11VertexShader *vs; ID3D11PixelShader *ps; ID3D11Buffer *cb;
    D3D11_BUFFER_DESC cbd = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    D3D_FEATURE_LEVEL level;
    LARGE_INTEGER freq, start, t;
    ID3DBlob *vsb, *psb;
    unsigned frames = 0;
    HRESULT hr;
    HWND hwnd;
    MSG msg;

    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "d3d11tri", "proton-apple D3D11 test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL );
    printf( "window %p\n", hwnd );

    scd.BufferCount = 2;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                        &scd, &swapchain, &device, &level, &ctx );
    if (FAILED(hr)) { printf( "D3D11CreateDeviceAndSwapChain failed %#lx\n", hr ); return 1; }
    printf( "device created, feature level %#x\n", level );

    IDXGISwapChain_GetBuffer( swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuffer );
    ID3D11Device_CreateRenderTargetView( device, (ID3D11Resource *)backbuffer, NULL, &rtv );
    vsb = compile( "vs", "vs_5_0" );
    psb = compile( "ps", "ps_5_0" );
    ID3D11Device_CreateVertexShader( device, ID3D10Blob_GetBufferPointer( vsb ), ID3D10Blob_GetBufferSize( vsb ), NULL, &vs );
    ID3D11Device_CreatePixelShader( device, ID3D10Blob_GetBufferPointer( psb ), ID3D10Blob_GetBufferSize( psb ), NULL, &ps );
    ID3D11Device_CreateBuffer( device, &cbd, NULL, &cb );
    printf( "shaders compiled and created\n" );

    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    for (;;)
    {
        static const float clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
        D3D11_VIEWPORT vp = { 0, 0, 800, 600, 0, 1 };
        float consts[4];
        double elapsed;

        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) { TranslateMessage( &msg ); DispatchMessageA( &msg ); }
        QueryPerformanceCounter( &t );
        elapsed = (double)(t.QuadPart - start.QuadPart) / freq.QuadPart;
        if (elapsed > seconds || msg.message == WM_QUIT) break;

        consts[0] = (float)elapsed;
        ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)cb, 0, NULL, consts, 0, 0 );
        ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
        ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, clear );
        ID3D11DeviceContext_RSSetViewports( ctx, 1, &vp );
        ID3D11DeviceContext_IASetPrimitiveTopology( ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        ID3D11DeviceContext_VSSetShader( ctx, vs, NULL, 0 );
        ID3D11DeviceContext_VSSetConstantBuffers( ctx, 0, 1, &cb );
        ID3D11DeviceContext_PSSetShader( ctx, ps, NULL, 0 );
        ID3D11DeviceContext_Draw( ctx, 3, 0 );
        IDXGISwapChain_Present( swapchain, 0, 0 );
        frames++;
    }
    printf( "rendered %u frames in %.1fs: %.0f fps\n", frames, seconds, frames / seconds );
    return 0;
}
