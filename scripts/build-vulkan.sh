#!/usr/bin/env bash
# Build the Vulkan-on-Metal stack:
#   dist/mesa/lib/libvulkan_kosmickrisp.dylib   KosmicKrisp, Mesa's Vulkan driver on Metal (src/mesa)
#   dist/vkd3d-proton/x64/{d3d12,d3d12core}.dll  vkd3d-proton, D3D12 on Vulkan (src/vkd3d-proton)
#   dist/dxvk/x64/dxgi.dll                       DXVK's DXGI, which vkd3d-proton presents through (src/dxvk)
#   dist/mesa-zink/lib/libEGL.1.dylib            Zink, Mesa's OpenGL on Vulkan (src/mesa)
#
# Usage: scripts/build-vulkan.sh [mesa|zink|vkd3d-proton|dxvk...]   (default: all)

source "$(dirname "$0")/env.sh"

build_mesa() {
    [[ -d "$SRC/mesa" ]] || die "missing $SRC/mesa, run scripts/fetch.sh mesa"
    # Mesa's generators need mako, packaging and pyyaml; keep them out of Homebrew's Python.
    local venv="$BUILD/venv-mesa"
    if [[ ! -x "$venv/bin/python" ]]; then
        log "creating $venv"
        python3 -m venv "$venv"
        "$venv/bin/pip" install -q mako packaging pyyaml
    fi
    for formula in libclc spirv-llvm-translator spirv-tools; do
        [[ -d "$BREW/opt/$formula" ]] || die "missing Homebrew $formula (brew install $formula)"
    done
    if [[ ! -f "$BUILD/mesa/build.ninja" ]]; then
        log "configuring Mesa (KosmicKrisp)"
        PATH="$venv/bin:$PATH" PKG_CONFIG_PATH="$BREW/opt/llvm/lib/pkgconfig:$BREW/lib/pkgconfig" \
            meson setup "$BUILD/mesa" "$SRC/mesa" --buildtype=debugoptimized --prefix="$DIST/mesa" \
            -Dplatforms=macos -Dvulkan-drivers=kosmickrisp -Dgallium-drivers= -Dopengl=false \
            -Dzstd=disabled --prefer-static >/dev/null
    fi
    log "building KosmicKrisp"
    PATH="$venv/bin:$PATH" ninja -C "$BUILD/mesa" install >/dev/null
}

# Zink, Mesa's OpenGL on Vulkan, with EGL, for KosmicKrisp: dist/mesa-zink/lib/libEGL.1.dylib,
# which Wine's Mac driver uses for OpenGL (scripts/play). Build it before
# Wine, whose configure looks for EGL there. Zink's macOS build wants MoltenVK's headers (it has
# code for MoltenVK, unused here) and loads the Vulkan loader.
build_zink() {
    local venv="$BUILD/venv-mesa"
    [[ -x "$venv/bin/python" ]] || die "missing $venv, run scripts/build-vulkan.sh mesa first"
    for formula in bison molten-vk vulkan-loader; do
        [[ -d "$BREW/opt/$formula" ]] || die "missing Homebrew $formula (brew install $formula)"
    done
    # macOS's own bison is too old for Mesa's GLSL compiler.
    local path="$BREW/opt/bison/bin:$venv/bin:$PATH"
    if [[ ! -f "$BUILD/mesa-zink/build.ninja" ]]; then
        log "configuring Mesa (Zink)"
        PATH="$path" PKG_CONFIG_PATH="$BREW/opt/llvm/lib/pkgconfig:$BREW/lib/pkgconfig" \
            meson setup "$BUILD/mesa-zink" "$SRC/mesa" --buildtype=debugoptimized \
            --prefix="$DIST/mesa-zink" -Dplatforms=macos -Dvulkan-drivers=kosmickrisp \
            -Dgallium-drivers=zink -Dopengl=true -Degl=enabled -Dgles2=enabled -Dglx=disabled \
            -Dzstd=disabled -Dmoltenvk-dir="$BREW/opt/molten-vk" -Dvulkan-loader-rpath="$BREW/lib" >/dev/null
    fi
    log "building Zink"
    PATH="$path" ninja -C "$BUILD/mesa-zink" install >/dev/null
}

# A meson cross file for x86_64 PE with llvm-mingw, rewritten only when its contents change.
MINGW="$ROOT/toolchains/llvm-mingw/bin"
X64_CROSS="$BUILD/x86_64-mingw.cross"
write_x64_cross() {
    mkdir -p "$BUILD"
    cat > "$X64_CROSS.new" <<EOF
[binaries]
c = '$MINGW/x86_64-w64-mingw32-clang'
cpp = '$MINGW/x86_64-w64-mingw32-clang++'
ar = '$MINGW/x86_64-w64-mingw32-ar'
strip = '$MINGW/x86_64-w64-mingw32-strip'
widl = '$MINGW/x86_64-w64-mingw32-widl'
windres = '$MINGW/x86_64-w64-mingw32-windres'

[built-in options]
# dxil-spirv and dxbc-spirv use std::launder and std::terminate without <new> and <exception>;
# libstdc++ pulls those in indirectly, llvm-mingw's libc++ doesn't.
cpp_args = ['-include', 'new', '-include', 'exception']

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
    if cmp -s "$X64_CROSS.new" "$X64_CROSS"; then rm "$X64_CROSS.new"; else mv "$X64_CROSS.new" "$X64_CROSS"; fi
}

# meson_pe <name> <build dir> <meson args...>: configure and install an x86_64 PE project. Starts
# the build dir over when the cross file changed: meson copies it at setup and doesn't re-read it.
meson_pe() {
    local name="$1" out="$2"; shift 2
    [[ -d "$SRC/$name" ]] || die "missing $SRC/$name, run scripts/fetch.sh $name"
    write_x64_cross
    [[ -f "$out/build.ninja" && "$out/build.ninja" -nt "$X64_CROSS" ]] || rm -rf "$out"
    if [[ ! -f "$out/build.ninja" ]]; then
        log "configuring $name (x86_64)"
        meson setup "$out" "$SRC/$name" --cross-file "$X64_CROSS" --buildtype=release "$@" >/dev/null
    fi
    log "building $name"
    ninja -C "$out" install >/dev/null
}

# x86_64 PE for now: games call these from FEX-emulated code. ARM64EC builds would run them natively.
build_vkd3d_proton() {
    meson_pe vkd3d-proton "$BUILD/vkd3d-proton-x64" --prefix="$DIST/vkd3d-proton" --bindir=x64 --libdir=x64 \
        -Denable_tests=false
}

# Only DXVK's dxgi.dll, which vkd3d-proton presents through; D3D11 stays on DXMT.
build_dxvk() {
    meson_pe dxvk "$BUILD/dxvk-x64" --prefix="$DIST/dxvk" --bindir=x64 --libdir=x64 \
        -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false -Denable_d3d11=false
}

want=("$@")
(( ${#want[@]} )) || want=(mesa zink vkd3d-proton dxvk)
for target in "${want[@]}"; do
    case "$target" in
        mesa) build_mesa ;;
        vkd3d-proton) build_vkd3d_proton ;;
        dxvk) build_dxvk ;;
        zink) build_zink ;;
        *) die "unknown target $target" ;;
    esac
done
log "done"
