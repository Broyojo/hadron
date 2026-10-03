/* d3d12probe: create a D3D12 device and print what it supports: feature level, shader model and
 * the optional features games check for. This is the list of what vkd3d-proton can offer on
 * KosmicKrisp. Place vkd3d-proton's d3d12.dll and d3d12core.dll next to the exe.
 *
 *   toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang -O2 -o build/tools/d3d12probe.exe \
 *       tools/d3d12probe.c -ld3d12
 *   cp dist/vkd3d-proton/x64/d3d12*.dll build/tools/ && scripts/wine-run build/tools/d3d12probe.exe
 */
#define COBJMACROS
#define INITGUID
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d12.h>

static const char *level_name(D3D_FEATURE_LEVEL level)
{
    switch (level)
    {
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    default: return "?";
    }
}

int main(void)
{
    ID3D12Device *device;
    HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    if (FAILED(hr))
    {
        printf("D3D12CreateDevice: 0x%08lx\n", hr);
        fflush(stdout);
        if (getenv("PROBE_WAIT")) Sleep(60000);  /* time to inspect the process (vmmap, lldb) */
        return 1;
    }

    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2 };
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels = { ARRAYSIZE(levels), levels };
    ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS, &feature_levels, sizeof(feature_levels));
    printf("max feature level: %s\n", level_name(feature_levels.MaxSupportedFeatureLevel));

    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = { (D3D_SHADER_MODEL)0x68 };
    while (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
            && shader_model.HighestShaderModel > D3D_SHADER_MODEL_5_1)
        shader_model.HighestShaderModel--;
    printf("shader model: %d.%d\n", shader_model.HighestShaderModel >> 4, shader_model.HighestShaderModel & 0xf);

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = { 0 };
    ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("resource binding tier: %d, tiled resources tier: %d, ROVs: %d\n", options.ResourceBindingTier,
           options.TiledResourcesTier, options.ROVsSupported);

    printf("conservative rasterization tier: %d, typed UAV load of more formats: %d, 64-bit floats: %d, logic ops: %d\n",
           options.ConservativeRasterizationTier, options.TypedUAVLoadAdditionalFormats,
           options.DoublePrecisionFloatShaderOps, options.OutputMergerLogicOp);

#define QUERY(type, id, name) type name = { 0 }; \
    if (FAILED(ID3D12Device_CheckFeatureSupport(device, id, &name, sizeof(name)))) printf(#id ": not known to this d3d12\n")
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS1, D3D12_FEATURE_D3D12_OPTIONS1, o1);
    printf("wave ops: %d (lanes %u..%u), 64-bit integers: %d\n", o1.WaveOps, o1.WaveLaneCountMin, o1.WaveLaneCountMax,
           o1.Int64ShaderOps);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS2, D3D12_FEATURE_D3D12_OPTIONS2, o2);
    printf("depth bounds test: %d, programmable sample positions tier: %d\n", o2.DepthBoundsTestSupported,
           o2.ProgrammableSamplePositionsTier);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS3, D3D12_FEATURE_D3D12_OPTIONS3, o3);
    printf("barycentrics: %d, view instancing tier: %d, casting fully typed formats: %d\n", o3.BarycentricsSupported,
           o3.ViewInstancingTier, o3.CastingFullyTypedFormatSupported);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS4, D3D12_FEATURE_D3D12_OPTIONS4, o4);
    printf("native 16-bit shader ops: %d\n", o4.Native16BitShaderOpsSupported);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS5, D3D12_FEATURE_D3D12_OPTIONS5, o5);
    printf("ray tracing tier: %d, render passes tier: %d\n", o5.RaytracingTier, o5.RenderPassesTier);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS6, D3D12_FEATURE_D3D12_OPTIONS6, o6);
    printf("variable rate shading tier: %d\n", o6.VariableShadingRateTier);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS7, D3D12_FEATURE_D3D12_OPTIONS7, o7);
    printf("mesh shader tier: %d, sampler feedback tier: %d\n", o7.MeshShaderTier, o7.SamplerFeedbackTier);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS9, D3D12_FEATURE_D3D12_OPTIONS9, o9);
    printf("64-bit atomics on typed resources: %d, on group shared memory: %d\n", o9.AtomicInt64OnTypedResourceSupported,
           o9.AtomicInt64OnGroupSharedSupported);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS11, D3D12_FEATURE_D3D12_OPTIONS11, o11);
    printf("64-bit atomics on descriptor heap resources: %d\n", o11.AtomicInt64OnDescriptorHeapResourceSupported);
    QUERY(D3D12_FEATURE_DATA_D3D12_OPTIONS12, D3D12_FEATURE_D3D12_OPTIONS12, o12);
    printf("enhanced barriers: %d, relaxed format casting: %d\n", o12.EnhancedBarriersSupported,
           o12.RelaxedFormatCastingSupported);

    ID3D12Device_Release(device);
    return 0;
}
