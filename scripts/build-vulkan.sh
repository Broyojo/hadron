#!/usr/bin/env bash
# Build the Vulkan-on-Metal stack:
#   dist/mesa/lib/libvulkan_kosmickrisp.dylib   KosmicKrisp, Mesa's Vulkan driver on Metal (src/mesa)
#   dist/vkd3d-proton/x64/{d3d12,d3d12core}.dll  vkd3d-proton, D3D12 on Vulkan (src/vkd3d-proton)
#
# Usage: scripts/build-vulkan.sh [mesa|vkd3d-proton...]   (default: all)

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

build_vkd3d_proton() {
    [[ -d "$SRC/vkd3d-proton" ]] || die "missing $SRC/vkd3d-proton, run scripts/fetch.sh vkd3d-proton"
    # x86_64 PE for now: games call it from FEX-emulated code. An ARM64EC build would run it natively.
    local mingw="$ROOT/toolchains/llvm-mingw/bin" out="$BUILD/vkd3d-proton-x64"
    local cross="$BUILD/vkd3d-proton-x64.cross"
    cat > "$cross.new" <<EOF
[binaries]
c = '$mingw/x86_64-w64-mingw32-clang'
cpp = '$mingw/x86_64-w64-mingw32-clang++'
ar = '$mingw/x86_64-w64-mingw32-ar'
strip = '$mingw/x86_64-w64-mingw32-strip'
widl = '$mingw/x86_64-w64-mingw32-widl'

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
    # meson copies the cross file at setup and doesn't re-read it; start over when it changes.
    if ! cmp -s "$cross.new" "$cross"; then
        mv "$cross.new" "$cross"
        rm -rf "$out"
    else
        rm "$cross.new"
    fi
    if [[ ! -f "$out/build.ninja" ]]; then
        log "configuring vkd3d-proton (x86_64)"
        meson setup "$out" "$SRC/vkd3d-proton" --cross-file "$cross" --buildtype=release \
            --prefix="$DIST/vkd3d-proton" --bindir=x64 --libdir=x64 -Denable_tests=false >/dev/null
    fi
    log "building vkd3d-proton"
    ninja -C "$out" install >/dev/null
}

want=("$@")
(( ${#want[@]} )) || want=(mesa vkd3d-proton)
for target in "${want[@]}"; do
    case "$target" in
        mesa) build_mesa ;;
        vkd3d-proton) build_vkd3d_proton ;;
        *) die "unknown target $target" ;;
    esac
done
log "done"
