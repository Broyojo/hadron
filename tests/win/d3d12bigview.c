/* D3D12 structured buffer view larger than a Vulkan texel buffer may be: a 2 GB buffer with a
 * structured SRV over all of it (532,676,608 elements of 4 bytes), read by a compute shader at
 * two places past element 2^28. Jurassic World Evolution 2 makes such a view while starting
 * (issue #56). Prints the two values read and the ones written there; they match when it works.
 * Usage: d3d12bigview.exe */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#define _WIN32_WINNT 0x0A00
#include <stdio.h>
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#define ELEMENTS 532676608u
#define FAR_INDEX ((1u << 28) + 5u)

static const char shader_src[] =
    "StructuredBuffer<uint> src : register(t0);\n"
    "RWStructuredBuffer<uint> dst : register(u0);\n"
    "[numthreads(1, 1, 1)] void main() {\n"
    "    dst[0] = src[" "532676607" "];\n"
    "    dst[1] = src[" "268435461" "];\n"
    "}\n";

static ID3D12Resource *make_buffer( ID3D12Device *dev, D3D12_HEAP_TYPE type, UINT64 size,
                                    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state )
{
    D3D12_HEAP_PROPERTIES heap = { type };
    D3D12_RESOURCE_DESC desc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1,
                                 DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags };
    ID3D12Resource *res = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource( dev, &heap, D3D12_HEAP_FLAG_NONE, &desc, state,
                                                       NULL, &IID_ID3D12Resource, (void **)&res );
    if (FAILED(hr)) printf( "CreateCommittedResource(%llu bytes) failed: %#lx\n", (unsigned long long)size, hr );
    return res;
}

int main( void )
{
    D3D12_DESCRIPTOR_RANGE ranges[2] = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1 },
    };
    D3D12_ROOT_PARAMETER param = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE };
    D3D12_ROOT_SIGNATURE_DESC rs_desc = { 1, &param };
    D3D12_COMMAND_QUEUE_DESC queue_desc = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2,
                                             D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = { DXGI_FORMAT_UNKNOWN, D3D12_SRV_DIMENSION_BUFFER,
                                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { DXGI_FORMAT_UNKNOWN, D3D12_UAV_DIMENSION_BUFFER };
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    D3D12_RESOURCE_BARRIER barrier = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    ID3D12Resource *big, *out, *readback;
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    ID3D12DescriptorHeap *heap;
    ID3D12RootSignature *rs;
    ID3D12PipelineState *pso;
    ID3D12CommandQueue *queue;
    ID3D12Device *dev;
    ID3D12Fence *fence;
    ID3DBlob *blob, *err;
    UINT *data, inc, last, deep;
    HRESULT hr;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (FAILED(hr = D3D12CreateDevice( NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev )))
    { printf( "D3D12CreateDevice failed: %#lx\n", hr ); return 1; }

    if (!(big = make_buffer( dev, D3D12_HEAP_TYPE_UPLOAD, (UINT64)ELEMENTS * 4, 0, D3D12_RESOURCE_STATE_GENERIC_READ )))
        return 1;
    if (FAILED(hr = ID3D12Resource_Map( big, 0, NULL, (void **)&data ))) { printf( "Map failed: %#lx\n", hr ); return 1; }
    data[ELEMENTS - 1] = 0x600df00d;
    data[FAR_INDEX] = 0x0badcafe;
    ID3D12Resource_Unmap( big, 0, NULL );
    printf( "buffer: %u elements of 4 bytes\n", ELEMENTS );

    out = make_buffer( dev, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    readback = make_buffer( dev, D3D12_HEAP_TYPE_READBACK, 256, 0, D3D12_RESOURCE_STATE_COPY_DEST );
    if (!out || !readback) return 1;

    ID3D12Device_CreateDescriptorHeap( dev, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap );
    inc = ID3D12Device_GetDescriptorHandleIncrementSize( dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    cpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( heap );
    gpu = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart( heap );
    srv.Buffer.NumElements = ELEMENTS;
    srv.Buffer.StructureByteStride = 4;
    ID3D12Device_CreateShaderResourceView( dev, big, &srv, cpu );
    printf( "structured SRV over all of it created\n" );
    uav.Buffer.NumElements = 64;
    uav.Buffer.StructureByteStride = 4;
    cpu.ptr += inc;
    ID3D12Device_CreateUnorderedAccessView( dev, out, NULL, &uav, cpu );

    param.DescriptorTable.NumDescriptorRanges = 2;
    param.DescriptorTable.pDescriptorRanges = ranges;
    if (FAILED(hr = D3D12SerializeRootSignature( &rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err )))
    { printf( "D3D12SerializeRootSignature failed: %#lx\n", hr ); return 1; }
    ID3D12Device_CreateRootSignature( dev, 0, ID3D10Blob_GetBufferPointer( blob ), ID3D10Blob_GetBufferSize( blob ),
                                      &IID_ID3D12RootSignature, (void **)&rs );
    if (FAILED(hr = D3DCompile( shader_src, sizeof(shader_src) - 1, NULL, NULL, NULL, "main", "cs_5_0", 0, 0, &blob, &err )))
    { printf( "D3DCompile failed: %s\n", err ? (char *)ID3D10Blob_GetBufferPointer( err ) : "" ); return 1; }
    pso_desc.pRootSignature = rs;
    pso_desc.CS.pShaderBytecode = ID3D10Blob_GetBufferPointer( blob );
    pso_desc.CS.BytecodeLength = ID3D10Blob_GetBufferSize( blob );
    if (FAILED(hr = ID3D12Device_CreateComputePipelineState( dev, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso )))
    { printf( "CreateComputePipelineState failed: %#lx\n", hr ); return 1; }

    ID3D12Device_CreateCommandQueue( dev, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue );
    ID3D12Device_CreateCommandAllocator( dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc );
    ID3D12Device_CreateCommandList( dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, pso,
                                    &IID_ID3D12GraphicsCommandList, (void **)&list );
    ID3D12GraphicsCommandList_SetDescriptorHeaps( list, 1, &heap );
    ID3D12GraphicsCommandList_SetComputeRootSignature( list, rs );
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable( list, 0, gpu );
    ID3D12GraphicsCommandList_Dispatch( list, 1, 1, 1 );
    barrier.Transition.pResource = out;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier( list, 1, &barrier );
    ID3D12GraphicsCommandList_CopyResource( list, readback, out );
    ID3D12GraphicsCommandList_Close( list );
    ID3D12CommandQueue_ExecuteCommandLists( queue, 1, (ID3D12CommandList **)&list );
    ID3D12Device_CreateFence( dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence );
    ID3D12CommandQueue_Signal( queue, fence, 1 );
    ID3D12Fence_SetEventOnCompletion( fence, 1, NULL );

    ID3D12Resource_Map( readback, 0, NULL, (void **)&data );
    last = data[0];
    deep = data[1];
    ID3D12Resource_Unmap( readback, 0, NULL );
    printf( "read: last %#x (wrote 0x600df00d), element 2^28+5 %#x (wrote 0xbadcafe)\n", last, deep );
    return last == 0x600df00d && deep == 0x0badcafe ? 0 : 1;
}
