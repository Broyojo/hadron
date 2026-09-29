#!/usr/bin/env bash
# Build upstream Wine natively for arm64 macOS.
#
# PE archs:
#   aarch64  native ARM64 Windows code
#   arm64ec  ARM64EC/ARM64X layer; x86_64 code runs through an emulator DLL (FEX)
#   i386     32-bit DLLs for new-style WoW64; i386 code runs through FEX's WoW64 DLL
#
# Usage: scripts/build-wine.sh [--dev] [--reconfigure]
#
#   --dev   Build without needing the cross-architecture entitlement: KUSER_SHARED_DATA
#           and the address space start move above 4GB. 64-bit programs only.
#           Uses build/wine-dev and installs to dist-dev.

source "$(dirname "$0")/env.sh"

variant=release reconfigure=
for arg; do
    case $arg in
        --dev) variant=dev ;;
        --reconfigure) reconfigure=1 ;;
        *) die "unknown option $arg" ;;
    esac
done

WINE_SRC="$SRC/wine"
WINE_BUILD="$BUILD/wine"
PREFIX="$DIST"
DEFS=
if [[ $variant == dev ]]; then
    WINE_BUILD="$BUILD/wine-dev"
    PREFIX="$ROOT/dist-dev"
    DEFS="-DWINE_HIGH_USER_SHARED_DATA"
fi
[[ -d "$WINE_SRC" ]] || die "missing $WINE_SRC, run scripts/fetch.sh wine"

mkdir -p "$WINE_BUILD"
cd "$WINE_BUILD"

if [[ ! -f Makefile || -n $reconfigure ]]; then
    log "configuring wine ($variant)"
    # Unix side: Apple clang (Objective-C/AppKit for winemac.drv, SDK defaults).
    # PE side: Homebrew clang targeting *-windows, linked with lld-link.
    PKG_CONFIG_PATH="$BREW/opt/freetype/lib/pkgconfig:$BREW/opt/gnutls/lib/pkgconfig" \
    CPPFLAGS="-I$BREW/include $DEFS" \
    LDFLAGS="-L$BREW/lib" \
    CROSSCFLAGS="-g -O2 $DEFS" \
    "$WINE_SRC/configure" \
        CC=/usr/bin/clang \
        CXX=/usr/bin/clang++ \
        --prefix="$PREFIX" \
        --enable-archs=arm64ec,aarch64,i386 \
        --with-mingw="$BREW/opt/llvm/bin/clang" \
        --without-x \
        --disable-tests \
        BISON="$BREW/opt/bison/bin/bison"
fi

log "building wine ($variant) with $JOBS jobs"
make -j"$JOBS"

log "installing to $PREFIX"
make install >/dev/null

# The loader needs the custom x18 ABI entitlement (ad-hoc signable). Release builds are
# re-signed with the Developer ID and release entitlements by scripts/sign.sh.
for loader in "$PREFIX/bin/wine" "$PREFIX/lib/wine/aarch64-unix/wine"; do
    codesign -f -s - --entitlements "$ROOT/packaging/dev.entitlements" "$loader" 2>/dev/null
done
log "done: $PREFIX/bin/wine"
