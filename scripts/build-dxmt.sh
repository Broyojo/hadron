#!/usr/bin/env bash
# Build DXMT (Direct3D 10/11 -> Metal) for our runtime: ARM64X PE DLLs (usable from
# both native arm64 and x86_64-under-FEX code), the arm64 winemetal unixlib, and i386 PE DLLs
# for 32-bit games, which call the same unixlib through its WoW64 entry points (#145).
#
# Usage: scripts/build-dxmt.sh [--dev]    (--dev builds against and installs into the dev variant)

source "$(dirname "$0")/env.sh"

variant=release
[[ "${1:-}" == --dev ]] && variant=dev

DXMT_SRC="$SRC/dxmt"
MINGW="$ROOT/toolchains/llvm-mingw/bin"
LLVM15="$ROOT/toolchains/llvm15"
if [[ $variant == dev ]]; then
    WINE_BUILD="$BUILD/wine-dev" PREFIX="$ROOT/dist-dev" DXMT_BUILD="$BUILD/dxmt-dev"
else
    WINE_BUILD="$BUILD/wine" PREFIX="$DIST" DXMT_BUILD="$BUILD/dxmt"
fi

[[ -d "$DXMT_SRC" ]] || die "missing $DXMT_SRC, run scripts/fetch.sh dxmt"
[[ -f "$LLVM15/lib/libLLVMCore.a" ]] || die "missing LLVM 15, run scripts/build-llvm15.sh"
[[ -f "$WINE_BUILD/Makefile" ]] || die "missing Wine build tree $WINE_BUILD"
xcrun -f metal >/dev/null 2>&1 || die "Metal compiler not found; install Xcode and its Metal toolchain"

export PATH="$MINGW:$PATH"

if [[ ! -f "$DXMT_BUILD/build.ninja" ]]; then
    log "configuring DXMT ($variant)"
    meson setup "$DXMT_BUILD" "$DXMT_SRC" \
        --cross-file "$DXMT_SRC/build-arm64ec.txt" \
        --buildtype release \
        --prefix "$PREFIX/lib/wine" \
        -Dnative_llvm_path="$LLVM15" \
        -Dwine_build_path="$WINE_BUILD" >/dev/null
fi

log "building DXMT ($variant)"
meson compile -C "$DXMT_BUILD"
meson install -C "$DXMT_BUILD" --no-rebuild >/dev/null

# 32-bit games got Wine's wined3d on Zink before, at a fraction of the speed. The dev variant
# runs 64-bit programs only.
if [[ $variant == release ]]; then
    DXMT32_BUILD="$DXMT_BUILD-i386"
    if [[ ! -f "$DXMT32_BUILD/build.ninja" ]]; then
        log "configuring DXMT (i386)"
        meson setup "$DXMT32_BUILD" "$DXMT_SRC" \
            --cross-file "$DXMT_SRC/build-win32.txt" \
            --buildtype release \
            --prefix "$PREFIX/lib/wine" \
            -Dnative_llvm_path="$LLVM15" \
            -Dwine_build_path="$WINE_BUILD" >/dev/null
    fi
    log "building DXMT (i386)"
    meson compile -C "$DXMT32_BUILD"
    meson install -C "$DXMT32_BUILD" --no-rebuild >/dev/null
fi
log "done"
