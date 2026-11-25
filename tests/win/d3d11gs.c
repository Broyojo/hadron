/* Geometry shaders through indexed draws: points expanded to quads, and an indexed triangle strip
 * with a cut index through a pass-through GS, which writes 1000 + 10 * SV_PrimitiveID + the first
 * vertex's number. Prints the 16x16 result, for comparing two DXMT builds.
 * Direct3D draws the triangle after the cut as primitive 2; DXMT drops it (findings #68).
 * Usage: d3d11gs.exe [nogs]   (nogs draws the strip without the geometry shader)
 * Build: x86_64-w64-mingw32-clang -O2 d3d11gs.c -o d3d11gs.exe -ld3d11 -ld3dcompiler -ldxguid */
#define COBJMACROS
#define INITGUID
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

static const char src[] =
    "struct v { float4 p : SV_Position; float id : ID; };\n"
    "v vs(float2 p : POS, uint vid : SV_VertexID) { v o; o.p = float4(p, 0, 1); o.id = vid + 1; return o; }\n"
    "[maxvertexcount(4)] void gsq(point v i[1], uint prim : SV_PrimitiveID, inout TriangleStream<v> s) {\n"
    "  float2 d[4] = { float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, -1) };\n"
    "  for (int k = 0; k < 4; k++) { v o; o.p = i[0].p + float4(d[k] * 0.124, 0, 0); o.id = i[0].id + prim * 100; s.Append(o); } }\n"
    "[maxvertexcount(3)] void gst(triangle v i[3], uint prim : SV_PrimitiveID, inout TriangleStream<v> s) {\n"
    "  for (int k = 0; k < 3; k++) { v o = i[k]; o.id = 1000 + prim * 10 + i[0].id; s.Append(o); } }\n"
    "float4 ps(v i) : SV_Target { return float4(i.id, 0, 0, 1); }\n";

static void *code( const char *entry, const char *target, SIZE_T *size, ID3DBlob **keep )
{
    ID3DBlob *err = NULL;
    if (FAILED(D3DCompile( src, sizeof(src) - 1, "g", NULL, NULL, entry, target, 0, 0, keep, &err )))
    {
        printf( "compile %s: %s\n", entry, err ? (char *)ID3D10Blob_GetBufferPointer( err ) : "" );
        exit( 1 );
    }
    *size = ID3D10Blob_GetBufferSize( *keep );
    return ID3D10Blob_GetBufferPointer( *keep );
}

int main( int argc, char **argv )
{
    /* points in the left half, strip vertices in the right half (two triangles, a cut, one triangle) */
    static const float verts[][2] = {
        {-0.75f, 0.75f}, {-0.25f, 0.75f}, {-0.75f, -0.25f}, {-0.25f, -0.25f},
        {0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.2f}, {1.0f, 0.2f}, {0.0f, -0.1f}, {1.0f, -0.1f}, {0.0f, -1.0f},
    };
    static const unsigned short pidx[] = {3, 1, 2, 0};
    static const unsigned short sidx[] = {4, 5, 6, 7, 0xffff, 8, 9, 10};
    static const float clear[4] = {0, 0, 0, 0};
    ID3D11Device *dev; ID3D11DeviceContext *ctx;
    ID3D11Texture2D *rt, *st; ID3D11RenderTargetView *rtv; ID3D11Buffer *vb, *ib1, *ib2;
    ID3D11VertexShader *vs; ID3D11GeometryShader *gsq, *gst; ID3D11PixelShader *ps; ID3D11InputLayout *il;
    ID3DBlob *b[5]; SIZE_T n; void *p;
    D3D11_TEXTURE2D_DESC td = {16, 16, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    D3D11_BUFFER_DESC bd = {sizeof(verts), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA sd = {verts};
    D3D11_INPUT_ELEMENT_DESC e = {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    D3D11_VIEWPORT vp = {0, 0, 16, 16, 0, 1};
    D3D11_MAPPED_SUBRESOURCE map;
    unsigned stride = 8, off = 0, x, y;
    HRESULT hr;

    setvbuf( stdout, NULL, _IONBF, 0 );
    hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx );
    if (FAILED(hr)) { printf( "D3D11CreateDevice %#lx\n", hr ); return 1; }
    ID3D11Device_CreateTexture2D( dev, &td, NULL, &rt );
    ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)rt, NULL, &rtv );
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D( dev, &td, NULL, &st );
    ID3D11Device_CreateBuffer( dev, &bd, &sd, &vb );
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.ByteWidth = sizeof(pidx); sd.pSysMem = pidx;
    ID3D11Device_CreateBuffer( dev, &bd, &sd, &ib1 );
    bd.ByteWidth = sizeof(sidx); sd.pSysMem = sidx;
    ID3D11Device_CreateBuffer( dev, &bd, &sd, &ib2 );
    p = code( "vs", "vs_5_0", &n, &b[0] ); ID3D11Device_CreateVertexShader( dev, p, n, NULL, &vs );
    ID3D11Device_CreateInputLayout( dev, &e, 1, p, n, &il );
    p = code( "gsq", "gs_5_0", &n, &b[1] ); hr = ID3D11Device_CreateGeometryShader( dev, p, n, NULL, &gsq );
    if (FAILED(hr)) printf( "CreateGeometryShader(gsq) %#lx\n", hr );
    p = code( "gst", "gs_5_0", &n, &b[2] ); hr = ID3D11Device_CreateGeometryShader( dev, p, n, NULL, &gst );
    if (FAILED(hr)) printf( "CreateGeometryShader(gst) %#lx\n", hr );
    p = code( "ps", "ps_5_0", &n, &b[3] ); ID3D11Device_CreatePixelShader( dev, p, n, NULL, &ps );

    ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, clear );
    ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
    ID3D11DeviceContext_RSSetViewports( ctx, 1, &vp );
    ID3D11DeviceContext_IASetInputLayout( ctx, il );
    ID3D11DeviceContext_IASetVertexBuffers( ctx, 0, 1, &vb, &stride, &off );
    ID3D11DeviceContext_VSSetShader( ctx, vs, NULL, 0 );
    ID3D11DeviceContext_PSSetShader( ctx, ps, NULL, 0 );

    ID3D11DeviceContext_IASetPrimitiveTopology( ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );
    ID3D11DeviceContext_IASetIndexBuffer( ctx, ib1, DXGI_FORMAT_R16_UINT, 0 );
    ID3D11DeviceContext_GSSetShader( ctx, gsq, NULL, 0 );
    ID3D11DeviceContext_DrawIndexed( ctx, 4, 0, 0 );

    ID3D11DeviceContext_IASetPrimitiveTopology( ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
    ID3D11DeviceContext_IASetIndexBuffer( ctx, ib2, DXGI_FORMAT_R16_UINT, 0 );
    ID3D11DeviceContext_GSSetShader( ctx, argc > 1 ? NULL : gst, NULL, 0 );
    ID3D11DeviceContext_DrawIndexed( ctx, 8, 0, 0 );

    ID3D11DeviceContext_CopyResource( ctx, (ID3D11Resource *)st, (ID3D11Resource *)rt );
    ID3D11DeviceContext_Map( ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &map );
    for (y = 0; y < 16; y++)
    {
        const float *row = (const float *)((const char *)map.pData + y * map.RowPitch);
        for (x = 0; x < 16; x++) printf( "%5.0f", row[x * 4] );
        printf( "\n" );
    }
    printf( "done\n" );
    return 0;
}
