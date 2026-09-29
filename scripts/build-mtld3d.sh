#!/usr/bin/env bash
# Build mtld3d (https://github.com/athei/mtld3d, zlib), a Direct3D 9 implementation on
# Metal, as Hadron's D3D9 backend in place of wined3d-over-OpenGL.
#
# mtld3d is Rust. It is three pieces per architecture:
#   d3d9.dll     the D3D9 API; records each frame for the unix side. Shipped here as an
#                ordinary *native* PE, so Wine's own builtin d3d9 stays in place and a
#                prefix opts in with a DLL override (see scripts/mtld3d-prefix).
#   mtld3d.dll   PE shim owning the unix-call bridge; must be a Wine *builtin*, since only
#                builtins can reach a unixlib. Custom name, so Wine's `make install` never
#                touches it.
#   mtld3d.so    arm64 Mach-O unixlib: encoder/submit/present/shader threads on Metal.
#                Exports __wine_unix_call_funcs and __wine_unix_call_wow64_funcs, so 32-bit
#                games run through WoW64 with none of its memory in their address space.
#                Attaches to windows through winemac.drv's `macdrv_functions` table
#                (patches/wine/0007, the same one DXMT uses).
#
# Built:  i686 (32-bit games under WoW64) and ARM64X (arm64ec + aarch64 halves: x64 game
#         code in an ARM64EC process calls the EC half natively). No x86_64 PE build.
#
# Installed:
#   $DIST/lib/wine/i386-windows/mtld3d.dll       builtin (wineboot stamps new prefixes)
#   $DIST/lib/wine/aarch64-windows/mtld3d.dll    builtin, ARM64X
#   $DIST/lib/wine/aarch64-unix/mtld3d.so
#   $DIST/lib/hadron/d3d9/i386/d3d9.dll          native, for a prefix's syswow64
#   $DIST/lib/hadron/d3d9/aarch64/d3d9.dll       native ARM64X, for a prefix's system32
#   $DIST/lib/hadron/d3d9/markers/{syswow64,system32}/mtld3d.dll  (copies of the builtins)
#                                                builtin markers for prefixes created
#                                                before mtld3d was installed
#   $DIST/lib/hadron/d3d9/mtld3d.conf, LICENSE
#
# Toolchain: rustup stable (1.98+) with the i686/aarch64/arm64ec windows-msvc targets
# (added automatically), Homebrew lld-link >= 23 (ARM64X link), llvm-mingw (ARM64/ARM64EC
# CRT), and the MSVC CRT + Windows SDK headers/import libs for the i686 link, splatted by
# `xwin` into toolchains/xwin. Downloading those means accepting Microsoft's license for
# them, so the first run needs MTLD3D_ACCEPT_MSVC_LICENSE=1 (nothing from them is
# redistributed beyond what the linker puts into the DLLs).
#
# Usage: scripts/build-mtld3d.sh [--dev] [--no-install]
#   MTLD3D_PROFILE=release for the upstream debug-assertion profile (default production).

source "$(dirname "$0")/env.sh"

variant=release install=1
for arg in "$@"; do
    case $arg in
        --dev) variant=dev ;;
        --no-install) install=0 ;;
        *) die "unknown argument $arg" ;;
    esac
done

MTLD3D_SRC="$SRC/mtld3d"
MINGW="$ROOT/toolchains/llvm-mingw"
XWIN="$ROOT/toolchains/xwin"
XWIN_TOOL="$ROOT/toolchains/xwin-tool/bin/xwin"
LLD_LINK="$BREW/opt/lld/bin/lld-link"
RUST=stable
PROFILE="${MTLD3D_PROFILE:-production}"
if [[ $variant == dev ]]; then
    WINE_BUILD="$BUILD/wine-dev" PREFIX="$ROOT/dist-dev" OUT="$BUILD/mtld3d-dev"
else
    WINE_BUILD="$BUILD/wine" PREFIX="$DIST" OUT="$BUILD/mtld3d"
fi
WINEBUILD="$WINE_BUILD/tools/winebuild/winebuild"
# mtld3d's build.rs pulls unix_lib.o out of lib/wine/<arch>/libwinecrt0.a, and the
# ARM64X link takes libntdll.a from there: the installed tree has the right layout.
WINE_SDK="$PREFIX"

# Versions upstream pins (src/mtld3d/Makefile, .github/workflows/ci.yml).
XWIN_VERSION=0.10.0
XWIN_CRT_VERSION=14.44.17.14
XWIN_SDK_VERSION=10.0.26100

[[ -d "$MTLD3D_SRC/windows" ]] || die "missing $MTLD3D_SRC, run scripts/fetch.sh mtld3d"
[[ -x "$WINEBUILD" ]] || die "missing $WINEBUILD, run scripts/build-wine.sh"
[[ -f "$WINE_SDK/lib/wine/i386-windows/libwinecrt0.a" ]] || die "missing $WINE_SDK/lib/wine, install Wine first"
[[ -f "$WINE_SDK/lib/wine/aarch64-windows/libntdll.a" ]] || die "missing $WINE_SDK/lib/wine/aarch64-windows/libntdll.a"
[[ -f "$MINGW/aarch64-w64-mingw32/lib/dllcrt2.o" ]] || die "missing llvm-mingw, run scripts/setup-toolchain.sh"
command -v rustup >/dev/null || die "rustup not found (brew install rustup)"
lld_major=$("$LLD_LINK" --version 2>/dev/null | sed -n 's/.*LLD \([0-9][0-9]*\)\..*/\1/p')
[[ -n "$lld_major" && "$lld_major" -ge 23 ]] || die "ARM64X link needs Homebrew lld-link >= 23 (brew install lld)"

want_targets=(i686-pc-windows-msvc aarch64-pc-windows-msvc arm64ec-pc-windows-msvc)
installed=$(rustup target list --installed --toolchain $RUST)
for t in "${want_targets[@]}"; do
    grep -qx "$t" <<<"$installed" || { log "adding Rust target $t"; rustup target add --toolchain $RUST "$t"; }
done

if [[ ! -f "$XWIN/crt/lib/x86/msvcrt.lib" ]]; then
    [[ "${MTLD3D_ACCEPT_MSVC_LICENSE:-}" == 1 ]] || die "the i686 build needs the MSVC CRT and Windows SDK in $XWIN;
       rerun with MTLD3D_ACCEPT_MSVC_LICENSE=1 to accept Microsoft's license and download them with xwin"
    if [[ ! -x "$XWIN_TOOL" ]]; then
        log "installing xwin $XWIN_VERSION"
        cargo +$RUST install --locked "xwin@$XWIN_VERSION" --root "$ROOT/toolchains/xwin-tool"
    fi
    log "downloading the MSVC CRT $XWIN_CRT_VERSION and Windows SDK $XWIN_SDK_VERSION (x86)"
    "$XWIN_TOOL" --accept-license --arch x86 --crt-version "$XWIN_CRT_VERSION" \
        --sdk-version "$XWIN_SDK_VERSION" --cache-dir "$BUILD/xwin-cache" splat --output "$XWIN"
    rm -rf "$BUILD/xwin-cache"
fi

# Upstream's windows/.cargo/config.toml hard-codes /opt/xwin. RUSTFLAGS replaces the
# per-target rustflags there wholesale, and variables already in the environment win over
# its [env] table, so the same settings are restated here against toolchains/xwin.
# Build scripts (host code) are unaffected: with --target, RUSTFLAGS only reaches the target.
export PATH="$MINGW/bin:$PATH" LLVM_AR="$BREW/opt/llvm/bin/llvm-ar"
export CARGO_TARGET_DIR="$OUT/windows" WINE_SDK
xwin_inc="-idirafter $XWIN/crt/include -idirafter $XWIN/sdk/include/ucrt -idirafter $XWIN/sdk/include/um -idirafter $XWIN/sdk/include/shared"
if [[ $PROFILE == production ]]; then
    # Upstream's PROD=1: no C/C++ assertions in snmalloc & co.
    export CFLAGS="-DNDEBUG" CXXFLAGS="-DNDEBUG"
fi
cd "$MTLD3D_SRC/windows"

log "building mtld3d i686 ($PROFILE)"
env RUSTFLAGS="-C target-cpu=pentium4 -C target-feature=-crt-static -Lnative=$XWIN/crt/lib/x86 -Lnative=$XWIN/sdk/lib/um/x86 -Lnative=$XWIN/sdk/lib/ucrt/x86" \
    "CFLAGS_i686-pc-windows-msvc=-march=pentium4 -fno-omit-frame-pointer $xwin_inc" \
    "CXXFLAGS_i686-pc-windows-msvc=-march=pentium4 -fno-omit-frame-pointer $xwin_inc" \
    cargo +$RUST build --profile "$PROFILE" --target i686-pc-windows-msvc -p mtld3d -p d3d9
OUT_i386="$OUT/windows/i686-pc-windows-msvc/$PROFILE"

# ARM64X: Rust has no ARM64X target, so each crate is built once per half as a static
# library and both are linked into one image (upstream's `EC=1 make windows-arm64x`).
# The C++ of snmalloc-sys is built without exceptions/RTTI/thread-safe statics because
# the halves link llvm-mingw's CRT, not MSVC's; see upstream's windows/.cargo/config.toml.
arm64_cxx='-fno-exceptions -fno-rtti -fno-threadsafe-statics -DSNMALLOC_USE_THREAD_CLEANUP -DARM64_SYSREG(op0,op1,crn,crm,op2)=((((op0)&1)<<14)|(((op1)&7)<<11)|(((crn)&15)<<7)|(((crm)&15)<<3)|((op2)&7))'
for target in aarch64-pc-windows-msvc arm64ec-pc-windows-msvc; do
    for crate in mtld3d d3d9; do
        log "building mtld3d $crate for $target ($PROFILE)"
        env RUSTFLAGS="-C target-feature=-crt-static" \
            "CFLAGS_$target=$xwin_inc" "CXXFLAGS_$target=$arm64_cxx $xwin_inc" \
            "AR_$target=llvm-lib" "ARFLAGS_arm64ec-pc-windows-msvc=-machine:arm64ec" \
            cargo +$RUST rustc --profile "$PROFILE" --target $target -p $crate --crate-type staticlib
    done
done
OUT_arm64="$OUT/windows/aarch64-pc-windows-msvc/$PROFILE"
OUT_arm64ec="$OUT/windows/arm64ec-pc-windows-msvc/$PROFILE"
OUT_arm64x="$OUT/windows/arm64x/$PROFILE"

# Upstream's archive checks: snmalloc's per-thread teardown must go through the TLS
# callback, never through llvm-mingw's __tlregdtor (which never runs destructors).
for lib in "$OUT_arm64/d3d9.lib" "$OUT_arm64ec/d3d9.lib"; do
    syms=$("$MINGW/bin/llvm-nm" "$lib" 2>/dev/null)
    grep -qE ' T #?_malloc_thread_cleanup$' <<<"$syms" || die "$lib defines no _malloc_thread_cleanup"
    ! grep -qE ' #?__tlregdtor$' <<<"$syms" || die "$lib registers a thread_local destructor through __tlregdtor"
done

# This llvm-mingw keeps one aarch64 sysroot whose archives carry both arm64 and arm64ec
# members (what `clang -marm64x` links), where upstream expects two sysroots.
arm64x_link() {
    local name=$1 def=$2 libs=() l
    for l in mingw32 mingwex ucrt kernel32 mincore user32 advapi32 gdi32 ws2_32 userenv bcrypt dbghelp; do
        libs+=("$MINGW/aarch64-w64-mingw32/lib/lib$l.a")
    done
    "$LLD_LINK" -lldmingw /dll /machine:arm64x /nodefaultlib /opt:ref,icf \
        /entry:DllMainCRTStartup /def:"$def" /defarm64native:"$def" \
        /debug /pdb:"$OUT_arm64x/$name.pdb" /out:"$OUT_arm64x/$name.dll" \
        "$MINGW/aarch64-w64-mingw32/lib/dllcrt2.o" \
        "$OUT_arm64/$name.lib" "$OUT_arm64ec/$name.lib" "${libs[@]}" \
        "$WINE_SDK/lib/wine/aarch64-windows/libntdll.a"
}
log "linking ARM64X mtld3d.dll and d3d9.dll"
mkdir -p "$OUT_arm64x"
arm64x_link mtld3d "$MTLD3D_SRC/windows/shim/mtld3d.def"
arm64x_link d3d9 "$MTLD3D_SRC/windows/d3d9/d3d9.def"

log "building mtld3d.so for aarch64-apple-darwin ($PROFILE)"
cd "$MTLD3D_SRC/unix"
CARGO_TARGET_DIR="$OUT/unix" cargo +$RUST build --profile "$PROFILE" --target aarch64-apple-darwin
OUT_unix="$OUT/unix/aarch64-apple-darwin/$PROFILE"
cp -c "$OUT_unix/libmtld3d_unix.dylib" "$OUT_unix/mtld3d.so"

# Stage: mtld3d.dll gets Wine's builtin marker, d3d9.dll stays native.
STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"/{i386-windows,aarch64-windows,aarch64-unix,native/i386,native/aarch64,markers/syswow64,markers/system32}
cp "$OUT_i386/mtld3d.dll" "$STAGE/i386-windows/"
cp "$OUT_arm64x/mtld3d.dll" "$STAGE/aarch64-windows/"
"$WINEBUILD" --builtin "$STAGE/i386-windows/mtld3d.dll"
"$WINEBUILD" --builtin "$STAGE/aarch64-windows/mtld3d.dll"
cp "$OUT_unix/mtld3d.so" "$STAGE/aarch64-unix/"
cp "$OUT_i386/d3d9.dll" "$STAGE/native/i386/"
cp "$OUT_arm64x/d3d9.dll" "$STAGE/native/aarch64/"
# Prefix markers are the builtin PEs themselves, as wineboot copies them: a winebuild
# --fake-module stub for aarch64 is refused (c000007b) when an ARM64EC process loads it.
cp "$STAGE/i386-windows/mtld3d.dll" "$STAGE/markers/syswow64/"
cp "$STAGE/aarch64-windows/mtld3d.dll" "$STAGE/markers/system32/"

if (( install )); then
    log "installing into $PREFIX"
    cp "$STAGE/i386-windows/mtld3d.dll" "$PREFIX/lib/wine/i386-windows/"
    cp "$STAGE/aarch64-windows/mtld3d.dll" "$PREFIX/lib/wine/aarch64-windows/"
    cp "$STAGE/aarch64-unix/mtld3d.so" "$PREFIX/lib/wine/aarch64-unix/"
    H="$PREFIX/lib/hadron/d3d9"
    rm -rf "$H"
    mkdir -p "$H"
    cp -R "$STAGE/native/i386" "$STAGE/native/aarch64" "$STAGE/markers" "$H/"
    cp "$MTLD3D_SRC/mtld3d.conf" "$MTLD3D_SRC/LICENSE" "$H/"
    echo "mtld3d $(git -C "$MTLD3D_SRC" describe --tags --match "v*" --always) ($PROFILE)" > "$H/VERSION"
fi
log "done; enable per prefix with scripts/mtld3d-prefix enable, run with WINEDLLOVERRIDES=d3d9=n,b"
