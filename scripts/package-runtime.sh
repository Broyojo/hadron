#!/usr/bin/env bash
# Make a self-contained copy of the runtime: what Hadron.app carries, and what has to run on a Mac
# that has neither this checkout nor Homebrew.
#
#   <out>/dist       Wine, FEX, the graphics layers and the launcher tools, without debug info
#   <out>/dist/ext   the libraries the build took from Homebrew, and the ones Wine opens by name
#   <out>/scripts    the launch scripts
#   <out>/config     per-game defaults
#   <out>/steam      the Steam client library and its signatures (scripts/steam-install)
#
# Nothing in it names this checkout or Homebrew: libraries find each other relative to
# themselves, and the script fails if a reference to either is left. Files that change are signed
# again ad hoc; the Wine loader, signed with its provisioning profile (scripts/package-loader.sh),
# is copied as it is.
#
# Usage: scripts/package-runtime.sh [output directory]   (default: build/package/runtime)
source "$(dirname "$0")/env.sh"

OUT="${1:-$BUILD/package/runtime}"
EXT="$OUT/dist/ext/lib"
STRIP_PE="$BREW/opt/llvm/bin/llvm-strip"
# Wine opens these by name (include/config.h's SONAME_*); the others it looks for are optional
# (D-Bus, ODBC) or macOS's own (CUPS).
BY_NAME=(libfreetype.6.dylib libgnutls.30.dylib libSDL2-2.0.0.dylib libvulkan.1.dylib)

[[ -x "$DIST/bin/wine" ]] || die "no runtime in $DIST: build it first"
[[ -f "$BUILD/notproton/notproton.dylib" ]] || die "missing the Steam client library, run scripts/build-steam-play.sh"

relpath() { perl -MFile::Spec -e 'print File::Spec->abs2rel($ARGV[0], $ARGV[1])' "$1" "$2"; }
# The libraries a Mach-O file links, without its own name.
links() { otool -L "$1" | tail -n +2 | awk '{print $1}' | grep -vxF "$(otool -D "$1" | tail -n +2)" || true; }
# Where a reference from a library in directory $2 leads on this Mac.
resolve() {
    case "$1" in
        @loader_path/*) echo "$2/${1#@loader_path/}" ;;
        @rpath/*) if [[ -e "$2/${1#@rpath/}" ]]; then echo "$2/${1#@rpath/}"; else echo "$BREW/lib/${1#@rpath/}"; fi ;;
        *) echo "$1" ;;
    esac
}
is_outside() { [[ "$1" == "$BREW"/* || "$1" == @rpath/* || "$1" == @loader_path/* ]]; }

# Copy a library from outside the runtime into dist/ext/lib, and everything it links, each
# referring to the others beside it.
bundle() {
    local src="$1" name dir dep target
    name=$(basename "$src")
    [[ -e "$EXT/$name" ]] && return
    [[ -e "$src" ]] || die "cannot find $src"
    dir=$(dirname "$(/bin/realpath "$src")")
    cp -L "$src" "$EXT/$name"
    chmod u+w "$EXT/$name"
    install_name_tool -id "@rpath/$name" "$EXT/$name" 2>/dev/null
    for dep in $(links "$EXT/$name"); do
        is_outside "$dep" || continue
        target=$(resolve "$dep" "$dir")
        install_name_tool -change "$dep" "@loader_path/$(basename "$target")" "$EXT/$name" 2>/dev/null
        bundle "$target"
    done
}

log "copying the runtime to $OUT"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -Rc "$DIST" "$OUT/dist"
# What only a build needs: headers, import and static libraries, pkg-config files, Wine's tools.
rm -rf "$OUT/dist/include" "$OUT/dist/share/man" "$OUT/dist/share/aclocal" "$OUT/dist"/{.,mesa,mesa-zink,ffmpeg}/lib/pkgconfig \
       "$OUT/dist"/{mesa,mesa-zink,ffmpeg}/include "$OUT/dist/ffmpeg/share"
find "$OUT/dist" -name '*.a' -delete
for tool in function_grep.pl widl winebuild winecpp winedump winegcc wineg++ winemaker wmc wrc; do
    rm -f "$OUT/dist/bin/$tool"
done

mkdir -p "$OUT/scripts" "$OUT/steam" "$EXT"
for script in hadron-procs.sh paths.sh play shortcut-icon steam-install steam-run steam-uninstall stop watchdog; do
    cp -p "$ROOT/scripts/$script" "$OUT/scripts/"
done
cp -R "$ROOT/config" "$OUT/config"
cp "$BUILD/notproton/notproton.dylib" "$OUT/steam/"
cp -R "$SRC/notproton/signatures" "$OUT/steam/signatures"
# What fetches Valve's Windows client files on the user's Mac: they are not ours to ship.
cp "$SRC/notproton/bridge/fetch-valve.sh" "$SRC/notproton/app/Sources/NotProtonApp/Resources/valve-packages.manifest" "$OUT/steam/"

# The runtime's own Mach-O files, except the loader's bundle, which keeps its signature.
machos=()
while IFS= read -r f; do
    [[ "$f" == */wine.app/* ]] && continue
    file -b "$f" | grep -q 'Mach-O' && machos+=("$f")
done < <(find "$OUT/dist/bin" "$OUT/dist/lib" "$OUT/dist/mesa" "$OUT/dist/mesa-zink" "$OUT/dist/ffmpeg" -type f \
              \( -name '*.so' -o -name '*.dylib' -o -perm +111 \) ! -path '*-windows/*' ! -path '*/ext/*')

log "bundling the libraries from Homebrew"
for name in "${BY_NAME[@]}"; do bundle "$BREW/lib/$name"; done
for f in "${machos[@]}"; do
    dir=$(dirname "$f")
    for dep in $(links "$f"); do
        if [[ "$dep" == "$BREW"/* ]]; then
            bundle "$dep"
            install_name_tool -change "$dep" "@loader_path/$(relpath "$EXT" "$dir")/$(basename "$dep")" "$f" 2>/dev/null
        elif [[ "$dep" == "$DIST"/* ]]; then
            # Mesa's libraries name each other by where this checkout installed them.
            install_name_tool -change "$dep" "@loader_path/$(relpath "$OUT/dist/${dep#"$DIST"/}" "$dir")" "$f" 2>/dev/null
        fi
    done
    id=$(otool -D "$f" | tail -n +2)
    [[ "$id" == "$DIST"/* ]] && install_name_tool -id "@rpath/$(basename "$id")" "$f" 2>/dev/null
    # A search path into Homebrew (Zink's, for the Vulkan loader) becomes one to dist/ext/lib.
    while IFS= read -r rpath; do
        [[ "$rpath" == "$BREW"/* ]] && install_name_tool -rpath "$rpath" "@loader_path/$(relpath "$EXT" "$dir")" "$f" 2>/dev/null
    done < <(otool -l "$f" | awk '/LC_RPATH/ {getline; getline; print $2}')
done
# The Vulkan loader finds the driver through a manifest: relative to the manifest, not this checkout.
for json in "$OUT"/dist/mesa*/share/vulkan/icd.d/*.json; do
    sed -i '' -E 's#"library_path": *"[^"]*/lib/([^"/]*)"#"library_path": "../../../lib/\1"#' "$json"
done

log "removing debug information"
find "$OUT/dist/lib/wine" "$OUT/dist/lib/hadron" "$OUT/dist/vkd3d-proton" "$OUT/dist/dxvk" -type f \
     \( -path '*-windows/*' -o -name '*.dll' -o -name '*.exe' \) -print0 2>/dev/null |
    xargs -0 -P "$JOBS" -n 32 "$STRIP_PE" --strip-debug 2>/dev/null || true
for f in "${machos[@]}" "$EXT"/*; do
    strip -S -x "$f" 2>/dev/null || true
done

log "signing what changed"
for f in "${machos[@]}" "$EXT"/* "$OUT/steam/notproton.dylib"; do
    codesign -f -s - "$f" 2>/dev/null || die "cannot sign $f"
done
codesign -v "$OUT/dist/lib/wine/aarch64-unix/wine.app" || die "the loader's signature did not survive the copy"

# Nothing may still point at this checkout or at Homebrew.
left=$(for f in "${machos[@]}" "$EXT"/*; do
           otool -L "$f" | tail -n +2 | awk -v f="${f#"$OUT"/}" '{print f ": " $1}'
           otool -l "$f" | awk -v f="${f#"$OUT"/}" '/LC_RPATH/ {getline; getline; print f ": rpath " $2}'
       done | grep -F -e "$BREW" -e "$ROOT" || true)
[[ -z "$left" ]] || die "references outside the runtime remain:
$left"
grep -rlF -e "$ROOT" -e "$BREW" "$OUT"/dist/mesa*/share/vulkan 2>/dev/null && die "a Vulkan manifest still names this checkout"

log "done: $OUT ($(du -sh "$OUT" | cut -f1), $(ls "$EXT" | wc -l | tr -d ' ') bundled libraries)"
