/* d3d12probe: create a D3D12 device and print what it supports (feature level, shader model,
 * binding tiers). Place vkd3d-proton's d3d12.dll and d3d12core.dll next to the exe.
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

    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = { D3D_SHADER_MODEL_6_7 };
    while (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
            && shader_model.HighestShaderModel > D3D_SHADER_MODEL_5_1)
        shader_model.HighestShaderModel--;
    printf("shader model: %d.%d\n", shader_model.HighestShaderModel >> 4, shader_model.HighestShaderModel & 0xf);

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = { 0 };
    ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("resource binding tier: %d, tiled resources tier: %d, ROVs: %d\n", options.ResourceBindingTier,
           options.TiledResourcesTier, options.ROVsSupported);

    ID3D12Device_Release(device);
    return 0;
}
