#!/usr/bin/env bash
# Build upstream Wine natively for arm64 macOS.
#
# PE archs:
#   aarch64  native ARM64 Windows code
#   arm64ec  ARM64EC/ARM64X layer; x86_64 code runs through an emulator DLL (FEX)
#   i386     32-bit DLLs for new-style WoW64; i386 code runs through FEX's WoW64 DLL
#
# Usage: scripts/build-wine.sh [--reconfigure]

source "$(dirname "$0")/env.sh"

WINE_SRC="$SRC/wine"
WINE_BUILD="$BUILD/wine"
[[ -d "$WINE_SRC" ]] || die "missing $WINE_SRC, run scripts/fetch.sh wine"

mkdir -p "$WINE_BUILD"
cd "$WINE_BUILD"

if [[ ! -f Makefile || "${1:-}" == --reconfigure ]]; then
    log "configuring wine"
    # Unix side: Apple clang (Objective-C/AppKit for winemac.drv, SDK defaults).
    # PE side: Homebrew clang targeting *-windows, linked with lld-link.
    PKG_CONFIG_PATH="$BREW/opt/freetype/lib/pkgconfig:$BREW/opt/gnutls/lib/pkgconfig" \
    CPPFLAGS="-I$BREW/include" \
    LDFLAGS="-L$BREW/lib" \
    "$WINE_SRC/configure" \
        CC=/usr/bin/clang \
        CXX=/usr/bin/clang++ \
        --prefix="$DIST" \
        --enable-archs=arm64ec,aarch64,i386 \
        --with-mingw="$BREW/opt/llvm/bin/clang" \
        --without-x \
        --disable-tests \
        BISON="$BREW/opt/bison/bin/bison"
fi

log "building wine with $JOBS jobs"
make -j"$JOBS"

log "installing to $DIST"
make install >/dev/null
log "done: $DIST/bin/wine"
