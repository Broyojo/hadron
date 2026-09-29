#!/usr/bin/env bash
# Build FEX's Wine emulator DLLs and their macOS unix helper.
#
#   libwow64fex.dll    aarch64 PE, emulates i386 code under WoW64      (xtajit.dll role)
#   libarm64ecfex.dll  arm64ec PE, emulates x86_64 code in ARM64EC     (xtajit64.dll role)
#   lib*fex.so         Darwin unix-side helpers loaded by the DLLs via __wine_unix_call

source "$(dirname "$0")/env.sh"

FEX_SRC="$SRC/fex"
MINGW="$ROOT/toolchains/llvm-mingw/bin"
[[ -d "$FEX_SRC" ]] || die "missing $FEX_SRC, run scripts/fetch.sh fex"
[[ -x "$MINGW/arm64ec-w64-mingw32-clang" ]] || die "missing llvm-mingw, run scripts/setup-toolchain.sh"

for target in wow64 arm64ec; do
    case $target in
        wow64)   triple=aarch64-w64-mingw32 ;;
        arm64ec) triple=arm64ec-w64-mingw32 ;;
    esac
    log "building FEX $target DLL"
    PATH="$MINGW:$PATH" cmake -S "$FEX_SRC" -B "$BUILD/fex-$target" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$FEX_SRC/Data/CMake/toolchain_mingw.cmake" \
        -DMINGW_TRIPLE=$triple \
        -DCMAKE_INSTALL_PREFIX="$DIST" \
        -DCMAKE_INSTALL_LIBDIR=lib/wine/aarch64-windows \
        -DENABLE_LTO=False -DENABLE_ASSERTIONS=False -DENABLE_JEMALLOC_GLIBC_ALLOC=False \
        -DBUILD_TESTING=False -DTUNE_ARCH=generic -DTUNE_CPU=none -DRANGES_NATIVE=OFF >/dev/null
    PATH="$MINGW:$PATH" cmake --build "$BUILD/fex-$target"
    PATH="$MINGW:$PATH" cmake --install "$BUILD/fex-$target" >/dev/null
done

log "building FEX unix helpers"
cmake -S "$FEX_SRC/Source/Windows/UnixLib" -B "$BUILD/fex-unixlib" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_INSTALL_PREFIX="$DIST" \
    -DCMAKE_INSTALL_LIBDIR=lib/wine/aarch64-unix >/dev/null
cmake --build "$BUILD/fex-unixlib"
cmake --install "$BUILD/fex-unixlib" >/dev/null

# Install FEX under the emulator names Wine loads by default (HKLM\Software\Microsoft\Wow64\x86
# and \amd64 fall back to xtajit.dll / xtajit64.dll), so it works from the first prefix boot.
W="$DIST/lib/wine/aarch64-windows"
rm -f "$W/xtajit.dll" "$W/xtajit64.dll"
cp "$W/libwow64fex.dll" "$W/xtajit.dll"
cp "$W/libarm64ecfex.dll" "$W/xtajit64.dll"

log "done:"
ls -la "$DIST"/lib/wine/aarch64-windows/lib*fex.dll "$DIST"/lib/wine/aarch64-unix/lib*fex.so
