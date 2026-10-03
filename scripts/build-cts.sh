#!/usr/bin/env bash
# Build the Khronos conformance suites that scripts/cts.sh runs, and deqp-runner.
#
#   scripts/build-cts.sh [vk] [gl]    (default: both)
#
# vk: the Vulkan CTS (deqp-vk), run against KosmicKrisp.
# gl: the OpenGL and OpenGL ES CTS (deqp-gles2, deqp-gles3, deqp-gles31, glcts), built for EGL
#     without a window and run against Zink. Needs scripts/build-vulkan.sh zink first.
#
# The suite source is not in sources.conf: it is large and only needed for testing. It is checked
# out unpatched, the results are only worth something against the suite as Khronos ships it.
source "$(dirname "$0")/env.sh"

cts="$SRC/vk-gl-cts"
ref=vulkan-cts-1.4.6.2

fetch_cts() {
    if [[ ! -d "$cts/.git" ]]; then
        log "cloning VK-GL-CTS ($ref)"
        git clone --filter=blob:none --no-checkout https://github.com/KhronosGroup/VK-GL-CTS.git "$cts"
        git -C "$cts" checkout -q --detach "$ref"
    fi
    [[ -z "$(git -C "$cts" status --porcelain --untracked-files=no)" ]] ||
        die "$cts has local changes: conformance results need the unmodified suite"
    # glslang, SPIRV-Tools and the other sources the suite builds itself
    [[ -d "$cts/external/glslang/src" ]] || python3 "$cts/external/fetch_sources.py"
}

build_runner() {
    [[ -x "$BUILD/deqp-runner/bin/deqp-runner" ]] && return
    command -v cargo >/dev/null || die "missing cargo (brew install rust), needed for deqp-runner"
    log "installing deqp-runner"
    cargo install deqp-runner --root "$BUILD/deqp-runner"
}

build_vk() {
    if [[ ! -f "$BUILD/vk-gl-cts/build.ninja" ]]; then
        log "configuring the Vulkan CTS"
        cmake -S "$cts" -B "$BUILD/vk-gl-cts" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=/usr/bin/cc -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
            -DCMAKE_EXE_LINKER_FLAGS="-L$BREW/opt/llvm/lib" >"$BUILD/vk-cts-configure.log" 2>&1 ||
            die "configure failed, see $BUILD/vk-cts-configure.log"
    fi
    log "building the Vulkan CTS"
    ninja -C "$BUILD/vk-gl-cts" deqp-vk >/dev/null
}

build_gl() {
    local zink="$DIST/mesa-zink"
    [[ -f "$zink/lib/libEGL.1.dylib" ]] || die "missing Zink, run scripts/build-vulkan.sh zink"
    if [[ ! -f "$BUILD/gl-cts/build.ninja" ]]; then
        log "configuring the OpenGL CTS"
        # The suite's macOS platform is CGL, Apple's OpenGL. Its EGL platform without a window
        # is the one that reaches Mesa.
        cmake -S "$cts" -B "$BUILD/gl-cts" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=/usr/bin/cc -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
            -DDEQP_TARGET=surfaceless \
            "-DTCUTIL_PLATFORM_SRCS=surfaceless/tcuSurfacelessPlatform.hpp;surfaceless/tcuSurfacelessPlatform.cpp" \
            -DCMAKE_CXX_FLAGS=-DDEQP_SURFACELESS=1 \
            -DCMAKE_PREFIX_PATH="$zink" -DCMAKE_INCLUDE_PATH="$zink/include" -DCMAKE_LIBRARY_PATH="$zink/lib" \
            -DCMAKE_EXE_LINKER_FLAGS="-L$zink/lib -Wl,-rpath,$zink/lib" >"$BUILD/gl-cts-configure.log" 2>&1 ||
            die "configure failed, see $BUILD/gl-cts-configure.log"
    fi
    log "building the OpenGL CTS"
    ninja -C "$BUILD/gl-cts" deqp-gles2 deqp-gles3 deqp-gles31 glcts >/dev/null
    # The EGL platform loads its libraries under their Linux names.
    mkdir -p "$BUILD/gl-cts/lib"
    ln -sf "$zink/lib/libEGL.1.dylib" "$BUILD/gl-cts/lib/libEGL.so"
    ln -sf "$zink/lib/libGLESv2.2.dylib" "$BUILD/gl-cts/lib/libGLESv2.so"
}

targets=("$@")
(( ${#targets[@]} )) || targets=(vk gl)
fetch_cts
build_runner
for t in "${targets[@]}"; do
    case "$t" in
        vk) build_vk ;;
        gl) build_gl ;;
        *) die "usage: $0 [vk] [gl]" ;;
    esac
done
log "done"
