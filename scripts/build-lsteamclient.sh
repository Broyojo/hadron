#!/usr/bin/env bash
# Build lsteamclient, the Steam bridge: a Windows-side steamclient replacement whose
# unix half forwards every Steamworks call to the native Mac Steam client's
# steamclient.dylib ($STEAM_COMPAT_CLIENT_INSTALL_PATH/steamclient.dylib).
#
#   aarch64-unix/lsteamclient.so      arm64 Mach-O unixlib, linker-signed; exports
#                                     __wine_unix_call_funcs (64-bit PE callers) and
#                                     __wine_unix_call_wow64_funcs (i386 callers)
#   i386-windows/lsteamclient.dll     i386 builtin, for 32-bit games under WoW64
#   aarch64-windows/lsteamclient.dll  ARM64X builtin (arm64ec + aarch64 halves), what
#                                     x64 game code in an ARM64EC process calls into
#
# Sources: Valve Proton's lsteamclient at the commit NotProton pins (fetched and
# digest-checked by NotProton's lsteamclient/fetch.sh, never committed here) with
# NotProton's macOS changes laid over it. Needs `scripts/fetch.sh notproton` and a
# built build/wine (headers, winebuild, winegcc, import libs, ntdll.so), which is
# only read, never rebuilt.
#
# Everything is compiled out of tree with the same compilers and flags build/wine
# uses, and the PE halves are linked by build/wine's winegcc with --wine-builtin,
# so they carry Wine's builtin marker exactly like the DLLs Wine ships.
#
# Usage: scripts/build-lsteamclient.sh [--no-install]

source "$(dirname "$0")/env.sh"

NOTPROTON="$SRC/notproton"
WINE_SRC="$SRC/wine"
WINE_BUILD="$BUILD/wine"
OUT="$BUILD/lsteamclient"
TREE="$OUT/src"
PE_CC="$BREW/opt/llvm/bin/clang"
UNIX_CC=/usr/bin/clang
UNIX_CXX=/usr/bin/clang++

install=1
for arg in "$@"; do
    case $arg in
        --no-install) install=0 ;;
        *) die "unknown argument $arg" ;;
    esac
done

[[ -x "$NOTPROTON/lsteamclient/fetch.sh" ]] || die "missing $NOTPROTON, run scripts/fetch.sh notproton"
[[ -x "$WINE_BUILD/tools/winegcc/winegcc" ]] || die "missing $WINE_BUILD, run scripts/build-wine.sh"
[[ -f "$WINE_BUILD/dlls/ntdll/ntdll.so" ]] || die "missing $WINE_BUILD/dlls/ntdll/ntdll.so"
[[ -x "$PE_CC" ]] || die "missing Homebrew LLVM clang at $PE_CC"

# Valve's tree (pinned commit, content digest verified) + NotProton's three authored
# files. Rebuilt from the verified baseline every run; unchanged files keep mtimes.
log "assembling lsteamclient sources"
mkdir -p "$OUT"
WORK="$OUT/fetch" TREE="$OUT/src.new" "$NOTPROTON/lsteamclient/fetch.sh" | sed 's/^/    /'
# Hadron's addition: overlay requests go to the Mac Steam client (launcher/lsteamclient-overlay.cpp).
cp "$ROOT/launcher/lsteamclient-overlay.cpp" "$OUT/src.new/hadron_overlay.cpp"
for f in "$OUT/src.new"/cppISteamFriends_SteamFriends*.cpp; do
    sed -i '' \
        -e '1i\
extern void hadron_overlay_open_url( const char *url ); extern void hadron_overlay_open_store( unsigned int app );
' \
        -e 's/^\( *\)iface->ActivateGameOverlayToWebPage( u_pchURL/\1hadron_overlay_open_url( u_pchURL ); &/' \
        -e 's/^\( *\)iface->ActivateGameOverlayToStore( params->nAppID/\1hadron_overlay_open_store( params->nAppID ); &/' "$f"
done
hooks=$(cat "$OUT/src.new"/cppISteamFriends_SteamFriends*.cpp | grep -c 'hadron_overlay_open_[a-z]*( [a-z]')
(( hooks >= 40 )) || die "only $hooks overlay calls were redirected, lsteamclient's generated code changed"
# No -t: files whose content is unchanged keep their old mtime, so objects stay current.
rsync -rlp --checksum --delete "$OUT/src.new/" "$TREE/"
rm -rf "$OUT/src.new"

# SOURCES in Makefile.in: .c is the PE side, .cpp the unix side. Keep the order, it
# is the link order.
sources() {
    sed -n "s/^[[:space:]]*\([A-Za-z0-9_]*\.$1\)[[:space:]]*\\\\*[[:space:]]*\$/\1/p" "$TREE/Makefile.in"
}
PE_SOURCES=($(sources c))
UNIX_SOURCES=($(sources cpp) hadron_overlay.cpp)
(( ${#PE_SOURCES[@]} > 10 && ${#UNIX_SOURCES[@]} > 10 )) || die "could not parse SOURCES from Makefile.in"
log "${#PE_SOURCES[@]} PE sources, ${#UNIX_SOURCES[@]} unix sources"

EXTRADEFS=(-DSTEAM_API_EXPORTS -Dprivate=public -Dprotected=public)
INCLUDES=(-I"$TREE" -I"$WINE_BUILD/include" -I"$WINE_SRC/include")

# build/wine's {arch}_EXTRACFLAGS, minus warnings (Valve's generated code is noisy).
pe_flags() {
    local common=(-D__WINE_PE_BUILD -target "$1-windows" -fuse-ld=lld --no-default-config
                  -fno-strict-aliasing -ffunction-sections -ffp-exception-behavior=maytrap
                  -gdwarf-4 -fasync-exceptions -g -O2 -w)
    case $1 in
        i686) common+=(-fms-hotpatch -fno-omit-frame-pointer -mlong-double-64 -msse2) ;;
    esac
    write_flags "$OUT/flags.$1" "${INCLUDES[@]}" -I"$WINE_SRC/include/msvcrt" -D_UCRT -D__WINESRC__ \
        "${EXTRADEFS[@]}" "${common[@]}"
}

UNIX_FLAGS=("${INCLUDES[@]}" -D__WINESRC__ -DWINE_UNIX_LIB "${EXTRADEFS[@]}" -arch arm64
            -fPIC -fasynchronous-unwind-tables -fvisibility=hidden -fno-stack-protector
            -fno-strict-aliasing -gdwarf-4 -g -O2 -w)

# Compile "src obj" pairs from stdin in parallel with the compiler and flags file given
# (one flag per line). An object is rebuilt when missing, older than its source, or
# older than any header or the flags file (flags files are only rewritten on change).
compile_all() {
    local cc="$1" flags_file="$2" newest todo=() src obj
    newest=$(ls -t "$TREE"/*.h "$flags_file" | head -1)
    while read -r src obj; do
        if [[ ! -f "$obj" || "$src" -nt "$obj" || "$newest" -nt "$obj" ]]; then
            todo+=("$src" "$obj")
        fi
    done
    if (( ${#todo[@]} == 0 )); then log "  all objects current"; return; fi
    log "  compiling $(( ${#todo[@]} / 2 )) file(s) with $JOBS jobs"
    printf '%s\0' "${todo[@]}" | xargs -0 -n 2 -P "$JOBS" sh -c '
        mkdir -p "$(dirname "$4")"
        tr "\n" "\0" < "$1" | xargs -0 "$2" -c -o "$4.tmp" "$3" && mv "$4.tmp" "$4" \
            || { echo "failed: $3" >&2; exit 255; }
    ' sh "$flags_file" "$cc" || die "compile failed"
}

# Write flags one per line, touching the file only when they changed.
write_flags() {
    local f="$1"; shift
    printf '%s\n' "$@" > "$f.new"
    if cmp -s "$f.new" "$f"; then rm -f "$f.new"; else mv -f "$f.new" "$f"; fi
}

# --- PE halves ---------------------------------------------------------------------
for arch in i686 arm64ec aarch64; do
    log "compiling PE side for $arch"
    pe_flags $arch
    for f in "${PE_SOURCES[@]}"; do
        echo "$TREE/$f $OUT/$arch-windows/${f%.c}.o"
    done | compile_all "$PE_CC" "$OUT/flags.$arch"
done

pe_objs() { for f in "${PE_SOURCES[@]}"; do echo "$OUT/$1-windows/${f%.c}.o"; done; }
pe_libs() {  # IMPORTS = user32 ws2_32, then the default CRT/runtime imports
    local d="$1-windows"
    printf '%s\n' "$WINE_BUILD/dlls/user32/$d/libuser32.a" "$WINE_BUILD/dlls/ws2_32/$d/libws2_32.a" \
        "$WINE_BUILD/libs/winecrt0/$d/libwinecrt0.a" "$WINE_BUILD/libs/compiler-rt/$d/libcompiler-rt.a" \
        "$WINE_BUILD/dlls/ucrtbase/$d/libucrtbase.a" "$WINE_BUILD/dlls/kernel32/$d/libkernel32.a" \
        "$WINE_BUILD/dlls/ntdll/$d/libntdll.a"
}
winegcc() {
    "$WINE_BUILD/tools/winegcc/winegcc" --wine-objdir "$WINE_BUILD" --cc-cmd="$PE_CC" "$@"
}

log "linking i386-windows/lsteamclient.dll"
mkdir -p "$OUT/i386-windows"
winegcc -o "$OUT/i386-windows/lsteamclient.dll" -b i686-windows -Wl,--wine-builtin -shared \
    "$TREE/lsteamclient.spec" $(pe_objs i686) $(pe_libs i386) \
    --no-default-config -fms-hotpatch -Wl,-debug:dwarf -Wl,--build-id

# ARM64X, like every 64-bit Wine DLL in this runtime: the arm64ec half is what x64
# game code (emulated by FEX) sees, the aarch64 half serves native arm64 callers.
log "linking aarch64-windows/lsteamclient.dll (ARM64X)"
winegcc -o "$OUT/aarch64-windows/lsteamclient.dll" -b arm64ec-windows -marm64x -Wl,--wine-builtin -shared \
    "$TREE/lsteamclient.spec" $(pe_objs aarch64) $(pe_objs arm64ec) $(pe_libs aarch64) \
    --no-default-config -Wl,-debug:dwarf -Wl,--build-id

# --- unix half ---------------------------------------------------------------------
log "compiling unix side (arm64)"
write_flags "$OUT/flags.unix" "${UNIX_FLAGS[@]}"
for f in "${UNIX_SOURCES[@]}"; do
    echo "$TREE/$f $OUT/unix/${f%.cpp}.o"
done | compile_all "$UNIX_CXX" "$OUT/flags.unix"

log "linking lsteamclient.so"
# Linked like build/wine links its own unixlibs (UNIXLDFLAGS), plus libc++. The arm64
# linker ad-hoc signs the output ("linker-signed"); do not strip or re-sign it.
"$UNIX_CC" -arch arm64 -o "$OUT/lsteamclient.so" -dynamiclib \
    -install_name @rpath/lsteamclient.so -Wl,-rpath,@loader_path/ \
    $(for f in "${UNIX_SOURCES[@]}"; do echo "$OUT/unix/${f%.cpp}.o"; done) \
    "$WINE_BUILD/dlls/ntdll/ntdll.so" -lc++ -L"$BREW/lib"

# --- checks ------------------------------------------------------------------------
so="$OUT/lsteamclient.so"
[[ "$(lipo -archs "$so")" == arm64 ]] || die "$so is not arm64"
linker_signed() { [[ "$(codesign -dv "$1" 2>&1)" == *linker-signed* ]]; }
linker_signed "$so" || die "$so is not linker-signed"
exports="$(nm -gU "$so")"$'\n'
for sym in __wine_unix_call_funcs __wine_unix_call_wow64_funcs; do
    [[ "$exports" == *" _$sym"$'\n'* ]] || die "$so does not export $sym"
done
for dll in "$OUT/i386-windows/lsteamclient.dll" "$OUT/aarch64-windows/lsteamclient.dll"; do
    size=$(stat -f %z "$dll")
    (( size > 1000000 )) || die "$dll is only $size bytes, the link dropped objects"
    [[ "$(dd if="$dll" bs=1 skip=64 count=16 2>/dev/null)" == "Wine builtin DLL" ]] \
        || die "$dll lacks the Wine builtin marker"
done

(( install )) || { log "built in $OUT (not installed)"; exit 0; }

log "installing into $DIST"
L="$DIST/lib/wine"
mkdir -p "$L/aarch64-unix" "$L/i386-windows" "$L/aarch64-windows"
# cp onto an existing, mapped file would invalidate its signature in running
# processes; replace atomically instead.
for pair in "$so:$L/aarch64-unix/lsteamclient.so" \
            "$OUT/i386-windows/lsteamclient.dll:$L/i386-windows/lsteamclient.dll" \
            "$OUT/aarch64-windows/lsteamclient.dll:$L/aarch64-windows/lsteamclient.dll"; do
    from=${pair%%:*} to=${pair#*:}
    cp -f "$from" "$to.new" && mv -f "$to.new" "$to"
done
linker_signed "$L/aarch64-unix/lsteamclient.so" || die "installed .so lost its signature"

log "done:"
ls -la "$L/aarch64-unix/lsteamclient.so" "$L/i386-windows/lsteamclient.dll" "$L/aarch64-windows/lsteamclient.dll"
