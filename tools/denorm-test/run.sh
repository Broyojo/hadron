#!/bin/sh
# Build and run denorm-test against KosmicKrisp (dist/mesa, or VK_DRIVER_FILES).
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/tools/denorm-test"
mkdir -p "$OUT"
cd "$(dirname "$0")"
glslangValidator -V --target-env vulkan1.2 -o "$OUT/plain.spv" denorm-test.comp >/dev/null
# GLSL can't ask for a denormal mode: add it to the SPIR-V. MODE=flush tests flush-to-zero instead.
mode=DenormPreserve; [ "${MODE:-preserve}" = flush ] && mode=DenormFlushToZero
spirv-dis "$OUT/plain.spv" | awk -v mode="$mode" '
    /OpCapability Shader/ { print; print "OpCapability " mode; print "OpCapability SignedZeroInfNanPreserve"; next }
    /OpExecutionMode %main LocalSize/ { print; print "OpExecutionMode %main " mode " 32";
                                        print "OpExecutionMode %main SignedZeroInfNanPreserve 32"; next }
    { print }' > "$OUT/moded.spvasm"
spirv-as --target-env vulkan1.2 -o "$OUT/moded.spv" "$OUT/moded.spvasm"
spirv-val --target-env vulkan1.2 "$OUT/moded.spv"
{ echo "static const uint32_t denorm_test_cs[] = {"; od -An -v -t x4 "$OUT/moded.spv" | sed 's/ *\([0-9a-f]\{8\}\)/0x\1,/g'; echo "};"; } > "$OUT/denorm-test-cs.h"
clang -O1 -g -ffp-contract=off -I/opt/homebrew/include -I"$OUT" -o "$OUT/denorm-test" denorm-test.c -L/opt/homebrew/lib -lvulkan
VK_DRIVER_FILES="${VK_DRIVER_FILES:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" "$OUT/denorm-test"
