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

# Configure again when Zink's EGL appears or goes away, so the EGL OpenGL driver follows it.
egl_state="no Zink"
[[ -f "$ROOT/dist/mesa-zink/lib/pkgconfig/egl.pc" ]] && egl_state="Zink EGL"
# The same for Hadron's own FFmpeg (scripts/build-ffmpeg.sh): Wine's media playback links it in
# place of whatever FFmpeg the system has.
[[ -f "$ROOT/dist/ffmpeg/lib/pkgconfig/libavcodec.pc" ]] && egl_state+=", own FFmpeg"
[[ "$(cat .hadron-egl 2>/dev/null)" == "$egl_state" ]] || reconfigure=1

if [[ ! -f Makefile || -n $reconfigure ]]; then
    log "configuring wine ($variant)"
    # Unix side: Apple clang (Objective-C/AppKit for winemac.drv, SDK defaults).
    # PE side: Homebrew clang targeting *-windows, linked with lld-link.
    PKG_CONFIG_PATH="$ROOT/dist/ffmpeg/lib/pkgconfig:$BREW/opt/freetype/lib/pkgconfig:$BREW/opt/gnutls/lib/pkgconfig:$ROOT/dist/mesa-zink/lib/pkgconfig" \
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
    echo "$egl_state" > .hadron-egl
    # make does not notice that a library is now found somewhere else: build the ones that follow
    # the state above again.
    rm -f dlls/winedmo/*.o dlls/winedmo/winedmo.so dlls/winemac.drv/*.o dlls/winemac.drv/winemac.so
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
# make install replaces the links into the loader bundle with a bare loader, which lacks the
# cross-architecture entitlement; wrap it again the way the bundle was signed: the same profile,
# certificate and hardened-runtime setting, so a Developer ID bundle stays one.
bundle="$PREFIX/lib/wine/aarch64-unix/wine.app"
if [[ $variant == release && -f "$bundle/Contents/embedded.provisionprofile" ]]; then
    cp "$bundle/Contents/embedded.provisionprofile" "$BUILD/loader.provisionprofile"
    rm -f "$BUILD"/loader-cert*
    codesign -d --extract-certificates="$BUILD/loader-cert" "$bundle" 2>/dev/null
    identity=$(shasum -a 1 "$BUILD/loader-cert0" 2>/dev/null | cut -d' ' -f1 | tr a-f A-F)
    runtime=(); codesign -dv "$bundle" 2>&1 | grep -q 'flags=.*runtime' && runtime=(--runtime)
    if [[ -n "$identity" ]] && security find-identity -v -p codesigning | grep -q "$identity"; then
        "$ROOT/scripts/package-loader.sh" "$BUILD/loader.provisionprofile" "$identity" ${runtime[@]+"${runtime[@]}"} >/dev/null
        log "wrapped the loader in wine.app again"
    else
        log "warning: the certificate wine.app was signed with is not in the keychain; run scripts/package-loader.sh"
    fi
fi
# make install replaces xtajit64.dll with Wine's stub; put FEX back as the default emulators.
W="$PREFIX/lib/wine/aarch64-windows"
if [[ -f "$W/libarm64ecfex.dll" ]]; then
    rm -f "$W/xtajit.dll" "$W/xtajit64.dll"
    cp "$W/libwow64fex.dll" "$W/xtajit.dll"
    cp "$W/libarm64ecfex.dll" "$W/xtajit64.dll"
    log "reinstalled FEX as xtajit.dll/xtajit64.dll"
fi
# make install replaces d3d11/dxgi with wined3d's; put DXMT's back if it has been built.
dxmt_build="$BUILD/dxmt"; [[ $variant == dev ]] && dxmt_build="$BUILD/dxmt-dev"
if [[ -f "$dxmt_build/build.ninja" ]]; then
    PATH="$ROOT/toolchains/llvm-mingw/bin:$PATH" meson install -C "$dxmt_build" --no-rebuild >/dev/null
    log "reinstalled DXMT"
fi
log "done: $PREFIX/bin/wine"
