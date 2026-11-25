/* Vertex fetch through every common DXGI vertex format: one point per format into a 64x1
 * R32G32B32A32_FLOAT target, read back and printed, for comparing two DXMT builds (findings #68).
 * Also per-instance data with a step rate, a start instance and a base vertex.
 * Build: x86_64-w64-mingw32-clang -O2 d3d11vfmt.c -o d3d11vfmt.exe -ld3d11 -ld3dcompiler -ldxguid */
#define COBJMACROS
#define INITGUID
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

static const char src[] =
    "struct o { float4 p : SV_Position; float4 v : V; };\n"
    "cbuffer c : register(b0) { float x; float3 pad; };\n"
    "o vsf(float4 a : ATTR) { o r; r.p = float4(x, 0, 0, 1); r.v = a; return r; }\n"
    "o vsu(uint4 a : ATTR) { o r; r.p = float4(x, 0, 0, 1); r.v = (float4)a; return r; }\n"
    "o vsi(int4 a : ATTR) { o r; r.p = float4(x, 0, 0, 1); r.v = (float4)a; return r; }\n"
    "o vsinst(float4 a : ATTR, float4 b : INST, uint vid : SV_VertexID, uint iid : SV_InstanceID) {\n"
    "  o r; r.p = float4(x + (iid * 4 + vid) * (2.0 / 64), 0, 0, 1); r.v = a * 1000 + b * 10 + float4(vid, iid, 0, 0); return r; }\n"
    "float4 ps(o i) : SV_Target { return i.v; }\n";

static ID3DBlob *compile( const char *entry, const char *target )
{
    ID3DBlob *blob = NULL, *err = NULL;
    if (FAILED(D3DCompile( src, sizeof(src) - 1, "v", NULL, NULL, entry, target, 0, 0, &blob, &err )))
    {
        printf( "compile %s failed: %s\n", entry, err ? (char *)ID3D10Blob_GetBufferPointer( err ) : "" );
        exit( 1 );
    }
    return blob;
}

struct fmt { DXGI_FORMAT f; const char *name; int kind; /* 0 float, 1 uint, 2 sint */ };

int main( void )
{
    static const struct fmt fmts[] = {
        {DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT", 0}, {DXGI_FORMAT_R32G32B32_FLOAT, "R32G32B32_FLOAT", 0},
        {DXGI_FORMAT_R32G32_FLOAT, "R32G32_FLOAT", 0}, {DXGI_FORMAT_R32_FLOAT, "R32_FLOAT", 0},
        {DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", 0}, {DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT", 0},
        {DXGI_FORMAT_R16_FLOAT, "R16_FLOAT", 0},
        {DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM", 0}, {DXGI_FORMAT_R16G16B16A16_SNORM, "R16G16B16A16_SNORM", 0},
        {DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", 0}, {DXGI_FORMAT_R16G16_SNORM, "R16G16_SNORM", 0},
        {DXGI_FORMAT_R16_UNORM, "R16_UNORM", 0}, {DXGI_FORMAT_R16_SNORM, "R16_SNORM", 0},
        {DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", 0}, {DXGI_FORMAT_R8G8B8A8_SNORM, "R8G8B8A8_SNORM", 0},
        {DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM", 0}, {DXGI_FORMAT_R8G8_UNORM, "R8G8_UNORM", 0},
        {DXGI_FORMAT_R8G8_SNORM, "R8G8_SNORM", 0}, {DXGI_FORMAT_R8_UNORM, "R8_UNORM", 0}, {DXGI_FORMAT_R8_SNORM, "R8_SNORM", 0},
        {DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2_UNORM", 0}, {DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT", 0},
        {DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT", 1}, {DXGI_FORMAT_R32G32B32_UINT, "R32G32B32_UINT", 1},
        {DXGI_FORMAT_R32G32_UINT, "R32G32_UINT", 1}, {DXGI_FORMAT_R32_UINT, "R32_UINT", 1},
        {DXGI_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT", 1}, {DXGI_FORMAT_R16G16_UINT, "R16G16_UINT", 1},
        {DXGI_FORMAT_R16_UINT, "R16_UINT", 1}, {DXGI_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT", 1},
        {DXGI_FORMAT_R8G8_UINT, "R8G8_UINT", 1}, {DXGI_FORMAT_R8_UINT, "R8_UINT", 1},
        {DXGI_FORMAT_R10G10B10A2_UINT, "R10G10B10A2_UINT", 1},
        {DXGI_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT", 2}, {DXGI_FORMAT_R32G32B32_SINT, "R32G32B32_SINT", 2},
        {DXGI_FORMAT_R32G32_SINT, "R32G32_SINT", 2}, {DXGI_FORMAT_R32_SINT, "R32_SINT", 2},
        {DXGI_FORMAT_R16G16B16A16_SINT, "R16G16B16A16_SINT", 2}, {DXGI_FORMAT_R16G16_SINT, "R16G16_SINT", 2},
        {DXGI_FORMAT_R16_SINT, "R16_SINT", 2}, {DXGI_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT", 2},
        {DXGI_FORMAT_R8G8_SINT, "R8G8_SINT", 2}, {DXGI_FORMAT_R8_SINT, "R8_SINT", 2},
    };
    const unsigned count = sizeof(fmts) / sizeof(fmts[0]);
    /* The same bytes for every format, at an offset of 4 in a stride of 24: what each format makes of them. */
    unsigned char vdata[48 + 24];
    ID3D11Device *dev; ID3D11DeviceContext *ctx; D3D_FEATURE_LEVEL fl;
    ID3D11Texture2D *rt, *staging; ID3D11RenderTargetView *rtv; ID3D11Buffer *vb, *cb, *ib, *instb;
    ID3D11VertexShader *vs[4]; ID3D11PixelShader *ps; ID3DBlob *blobs[4], *psb;
    static const char *entries[4] = {"vsf", "vsu", "vsi", "vsinst"};
    D3D11_TEXTURE2D_DESC td = {64, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    D3D11_BUFFER_DESC bd = {0};
    D3D11_SUBRESOURCE_DATA sd = {0};
    D3D11_VIEWPORT vp = {0, 0, 64, 1, 0, 1};
    D3D11_MAPPED_SUBRESOURCE map;
    static const float zero[4] = {-1, -1, -1, -1};
    unsigned i, j, stride = 24, offset = 0;
    HRESULT hr;

    setvbuf( stdout, NULL, _IONBF, 0 );
    for (i = 0; i < sizeof(vdata); i++) vdata[i] = (unsigned char)(i * 37 + 11);
    hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx );
    if (FAILED(hr)) { printf( "D3D11CreateDevice failed %#lx\n", hr ); return 1; }
    ID3D11Device_CreateTexture2D( dev, &td, NULL, &rt );
    ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)rt, NULL, &rtv );
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D( dev, &td, NULL, &staging );
    bd.ByteWidth = sizeof(vdata); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.Usage = D3D11_USAGE_DEFAULT;
    sd.pSysMem = vdata;
    ID3D11Device_CreateBuffer( dev, &bd, &sd, &vb );
    bd.ByteWidth = 16; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Device_CreateBuffer( dev, &bd, NULL, &cb );
    for (i = 0; i < 4; i++)
    {
        blobs[i] = compile( entries[i], "vs_5_0" );
        ID3D11Device_CreateVertexShader( dev, ID3D10Blob_GetBufferPointer( blobs[i] ), ID3D10Blob_GetBufferSize( blobs[i] ), NULL, &vs[i] );
    }
    psb = compile( "ps", "ps_5_0" );
    ID3D11Device_CreatePixelShader( dev, ID3D10Blob_GetBufferPointer( psb ), ID3D10Blob_GetBufferSize( psb ), NULL, &ps );
    ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, zero );
    ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
    ID3D11DeviceContext_RSSetViewports( ctx, 1, &vp );
    ID3D11DeviceContext_IASetPrimitiveTopology( ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );
    ID3D11DeviceContext_PSSetShader( ctx, ps, NULL, 0 );
    ID3D11DeviceContext_VSSetConstantBuffers( ctx, 0, 1, &cb );
    ID3D11DeviceContext_IASetVertexBuffers( ctx, 0, 1, &vb, &stride, &offset );
    for (i = 0; i < count; i++)
    {
        D3D11_INPUT_ELEMENT_DESC e = {"ATTR", 0, fmts[i].f, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0};
        ID3D11InputLayout *il;
        float c[4] = {(i + 0.5f) * 2.0f / 64 - 1, 0, 0, 0};
        ID3DBlob *b = blobs[fmts[i].kind];
        hr = ID3D11Device_CreateInputLayout( dev, &e, 1, ID3D10Blob_GetBufferPointer( b ), ID3D10Blob_GetBufferSize( b ), &il );
        if (FAILED(hr)) { printf( "%s: CreateInputLayout %#lx\n", fmts[i].name, hr ); continue; }
        ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)cb, 0, NULL, c, 0, 0 );
        ID3D11DeviceContext_IASetInputLayout( ctx, il );
        ID3D11DeviceContext_VSSetShader( ctx, vs[fmts[i].kind], NULL, 0 );
        /* vertex 1 of the buffer: offset 4 + stride 24 */
        ID3D11DeviceContext_Draw( ctx, 1, 1 );
        ID3D11InputLayout_Release( il );
    }
    /* Instancing: 2 vertices x 3 instances, per-instance data stepping every 2 instances, from start
     * instance 1, indexed with base vertex 1. Lands at pixels 48.. */
    {
        static const float inst[4][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}};
        static const unsigned short idx[2] = {0, 1};
        D3D11_INPUT_ELEMENT_DESC e[2] = {{"ATTR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                         {"INST", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 2}};
        ID3D11InputLayout *il; ID3D11Buffer *bufs[2]; unsigned strides[2] = {stride, 16}, offs[2] = {0, 0};
        float c[4] = {(48 + 0.5f) * 2.0f / 64 - 1, 0, 0, 0};
        bd.ByteWidth = sizeof(inst); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; sd.pSysMem = inst;
        ID3D11Device_CreateBuffer( dev, &bd, &sd, &instb );
        bd.ByteWidth = 4; bd.BindFlags = D3D11_BIND_INDEX_BUFFER; sd.pSysMem = idx;
        ID3D11Device_CreateBuffer( dev, &bd, &sd, &ib );
        hr = ID3D11Device_CreateInputLayout( dev, e, 2, ID3D10Blob_GetBufferPointer( blobs[3] ), ID3D10Blob_GetBufferSize( blobs[3] ), &il );
        if (FAILED(hr)) printf( "instanced: CreateInputLayout %#lx\n", hr );
        bufs[0] = vb; bufs[1] = instb;
        ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)cb, 0, NULL, c, 0, 0 );
        ID3D11DeviceContext_IASetVertexBuffers( ctx, 0, 2, bufs, strides, offs );
        ID3D11DeviceContext_IASetIndexBuffer( ctx, ib, DXGI_FORMAT_R16_UINT, 0 );
        ID3D11DeviceContext_IASetInputLayout( ctx, il );
        ID3D11DeviceContext_VSSetShader( ctx, vs[3], NULL, 0 );
        ID3D11DeviceContext_DrawIndexedInstanced( ctx, 2, 3, 0, 1, 1 );
    }
    ID3D11DeviceContext_CopyResource( ctx, (ID3D11Resource *)staging, (ID3D11Resource *)rt );
    ID3D11DeviceContext_Map( ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map );
    for (i = 0; i < 64; i++)
    {
        const float *p = (const float *)map.pData + i * 4;
        if (p[0] == -1 && p[1] == -1 && p[2] == -1 && p[3] == -1) continue;
        printf( "%2u %-20s", i, i < count ? fmts[i].name : "instanced" );
        for (j = 0; j < 4; j++) printf( " %.6g", p[j] );
        printf( "\n" );
    }
    ID3D11DeviceContext_Unmap( ctx, (ID3D11Resource *)staging, 0 );
    printf( "done\n" );
    return 0;
}
