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
# Wine opens these libraries by name, when its configure found them: what this build of Wine
# found is in its config.h (SONAME_*). The others it looks for there are optional (D-Bus, ODBC)
# or macOS's own (CUPS, and EGL is Zink's, already in dist/).
BY_NAME=()
for lib in FREETYPE GNUTLS SDL2 VULKAN; do
    name=$(sed -n "s/^#define SONAME_LIB$lib \"\(.*\)\"/\1/p" "$BUILD/wine/include/config.h" 2>/dev/null)
    [[ -n "$name" ]] && BY_NAME+=("$name")
done

[[ -x "$DIST/bin/wine" ]] || die "no runtime in $DIST: build it first"
[[ -f "$BUILD/notproton/notproton.dylib" && -f "$BUILD/notproton/overlay-shim.dylib" ]] ||
    die "missing the Steam client library or the overlay helper, run scripts/build-steam-play.sh"

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
    # Which Homebrew package it came from, for the notices: <prefix>/Cellar/<formula>/<version>/...
    if [[ "$dir" == "$BREW"/Cellar/*/*/* ]]; then
        keg="${dir#"$BREW"/Cellar/}"
        echo "${keg%%/*} $(cut -d/ -f2 <<<"$keg")" >> "$OUT/licenses/.bundled"
    fi
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

mkdir -p "$OUT/scripts" "$OUT/steam" "$OUT/licenses" "$EXT"
for script in hadron-procs.sh mtld3d-prefix paths.sh play report shortcut-icon steam-install steam-run steam-status steam-uninstall stop watchdog; do
    cp -p "$ROOT/scripts/$script" "$OUT/scripts/"
done
cp -R "$ROOT/config" "$OUT/config"
cp "$BUILD/notproton/notproton.dylib" "$BUILD/notproton/overlay-shim.dylib" "$OUT/steam/"
cp -R "$SRC/notproton/signatures" "$OUT/steam/signatures"
# What fetches Valve's Windows client files on the user's Mac: they are not ours to ship.
cp "$SRC/notproton/bridge/fetch-valve.sh" "$SRC/notproton/app/Sources/NotProtonApp/Resources/valve-packages.manifest" "$OUT/steam/"
# The packaged scripts run on Macs without Apple's developer tools, where otool, python3 and the
# like are stubs that only offer to install them: none of those may be used.
# By name or by path (/usr/bin/otool), and as an interpreter (#!/usr/bin/python3); comments apart.
used=$(grep -nE '(^|[;|&(`[:space:]/])(otool|xcrun|python3?|strings|nm|lipo|install_name_tool|dwarfdump|swiftc?|clang|make|brew)([[:space:]]|$)' \
           "$OUT"/scripts/* "$OUT"/steam/*.sh | grep -vE '^[^:]*:[0-9]+:[[:space:]]*#([^!]|$)' || true)
[[ -z "$used" ]] || die "the packaged scripts use developer tools:
$used"
# Every script a packaged script runs has to be in the package too.
for ref in $(grep -oh '\$ROOT/scripts/[A-Za-z0-9_.-]*' "$OUT"/scripts/* | sort -u); do
    [[ -e "$OUT/${ref#\$ROOT/}" ]] || die "the packaged scripts use ${ref#\$ROOT/}, which is not in the package"
done
# The version it will report: VERSION, marked with the commit unless this commit is that release's tag.
version=$(cat "$ROOT/VERSION")
[[ "$(git -C "$ROOT" describe --tags --exact-match 2>/dev/null)" == "v$version" ]] || version+="-dev.$(git -C "$ROOT" rev-parse --short HEAD)"
echo "$version" > "$OUT/VERSION"

# The runtime's own Mach-O files, except the loader's bundle, which keeps its signature.
machos=()
while IFS= read -r f; do
    [[ "$f" == */wine.app/* ]] && continue
    file -b "$f" | grep -q 'Mach-O' && machos+=("$f")
done < <(find "$OUT/dist/bin" "$OUT/dist/lib" "$OUT/dist/mesa" "$OUT/dist/mesa-zink" "$OUT/dist/ffmpeg" -type f \
              \( -name '*.so' -o -name '*.dylib' -o -perm +111 \) ! -path '*-windows/*' ! -path '*/ext/*')

log "bundling the libraries from Homebrew"
for name in "${BY_NAME[@]}"; do bundle "$BREW/lib/$name"; done
# Homebrew's SDL2 is sdl2-compat: SDL2's interface on SDL3, which it opens by name when it starts,
# beside itself first, and aborts without. Nothing links SDL3, so it has to be named here. Without
# it Wine's controller service (winebus) dies at startup and games see no game controllers.
for sdl2 in "$EXT"/libSDL2*.dylib; do
    if [[ -e "$sdl2" ]] && strings -a "$sdl2" | grep -q '@loader_path/libSDL3.dylib'; then
        bundle "$BREW/lib/libSDL3.dylib"
    fi
done
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

log "collecting licences"
# Each project's own licence files, and THIRD-PARTY-NOTICES.md: what is in here, under which
# licence, and which exact sources it was built from.
notices="$OUT/licenses/THIRD-PARTY-NOTICES.md"
commit=$(git -C "$ROOT" rev-parse HEAD)
mkdir -p "$OUT/licenses/hadron"
cp "$ROOT/LICENSE" "$ROOT/LICENSE.hadron" "$OUT/licenses/hadron/"
{
    echo "# Third-party notices"
    echo
    echo "Hadron $version is built from https://github.com/Broyojo/hadron at commit $commit."
    echo "Hadron's own code is under the BSD 3-Clause licence (hadron/LICENSE.hadron). Everything else"
    echo "in this application comes from the projects below, each under its own licence, whose text"
    echo "is in the directory named after it."
    echo
    echo "The complete corresponding source of every component is that repository at that commit:"
    echo "sources.conf names the upstream repository and revision of each, patches/<name>/ holds"
    echo "Hadron's changes to it as a patch series, and the scripts in scripts/ build and install"
    echo "them. You may replace any of these libraries in the application with your own build."
    echo
    echo "| Component | Licence | Upstream revision | Hadron's patches |"
    echo "|---|---|---|---|"
} > "$notices"
while IFS='|' read -r name what licence files dir source; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    dir="$ROOT/${dir:-src/$name}"
    mkdir -p "$OUT/licenses/$name"
    for f in $files; do
        [[ -e "$dir/$f" ]] || die "missing licence file $dir/$f"
        cp -R "$dir/$f" "$OUT/licenses/$name/$(tr / - <<<"$f")"
    done
    # The projects a component carries inside its own tree (FEX's External, the subprojects of
    # vkd3d-proton and DXVK, and so on) have their licence files there.
    while IFS= read -r f; do
        mkdir -p "$OUT/licenses/$name/vendored"
        cp "$f" "$OUT/licenses/$name/vendored/$(tr / - <<<"${f#"$dir"/}")"
    done < <(find "$dir" -maxdepth 5 -type f \( -iname 'LICEN[CS]E*' -o -iname 'COPYING*' -o -iname 'NOTICE*' \) \
                  \( -path '*/External/*' -o -path '*/external/*' -o -path '*/subprojects/*' -o -path '*/third_party/*' \
                     -o -path '*/vendor/*' -o -path '*/3rdparty/*' \) ! -path '*/test/*' 2>/dev/null)
    queue=("$ROOT/patches/$name"/*.patch)
    if [[ -e "${queue[0]}" ]]; then patches=${#queue[@]}; else patches=none; fi
    if [[ -z "$source" ]]; then
        read -r _ url ref < <(grep -E "^$name[[:space:]]" "$ROOT/sources.conf")
        source="$url at \`$ref\`"
    fi
    echo "| $what | $licence | $source | $patches |" >> "$notices"
done < "$ROOT/packaging/third-party.conf"
# mtld3d is Rust: the crates its Cargo.lock files name, each with the licence its manifest
# declares and its licence files, from Cargo's registry on this Mac.
python3 - "$SRC/mtld3d" "$OUT/licenses/mtld3d" >> "$notices" <<'CRATES'
import glob, os, re, shutil, sys
src, out = sys.argv[1], sys.argv[2]
crates = set()
for lock in glob.glob(src + "/*/Cargo.lock"):
    text = open(lock).read()
    crates |= {(n, v) for n, v in re.findall(r'\[\[package\]\]\nname = "([^"]+)"\nversion = "([^"]+)"\nsource = ', text)}
registries = glob.glob(os.path.expanduser("~/.cargo/registry/src/*"))
rows = []
for name, version in sorted(crates):
    dirs = [d for r in registries for d in glob.glob(f"{r}/{name}-{version}")]
    if not dirs:
        # Cargo fetches only what a build for its targets needs: this one was never compiled in.
        continue
    manifest = open(dirs[0] + "/Cargo.toml").read()
    declared = re.search(r'^license = "([^"]+)"', manifest, re.M)
    files = [f for f in glob.glob(dirs[0] + "/*") if re.match(r"(?i)(licen[cs]e|copying|notice)", os.path.basename(f)) and os.path.isfile(f)]
    for f in files:
        os.makedirs(f"{out}/crates/{name}-{version}", exist_ok=True)
        shutil.copy(f, f"{out}/crates/{name}-{version}/")
    rows.append((name, version, declared.group(1) if declared else "see its repository"))
if not rows:
    sys.exit("none of the crates in mtld3d's Cargo.lock files is in Cargo's registry")
print()
print("## Rust crates in mtld3d")
print()
print("The crates of its Cargo.lock files that this build fetched; the licence files they ship are")
print("in mtld3d/crates.")
print()
print("| Crate | Version | Licence |")
print("|---|---|---|")
for row in rows:
    print("| %s | %s | %s |" % row)
CRATES
{
    echo
    echo "## Libraries taken as built by Homebrew"
    echo
    echo "These are unmodified, in dist/ext/lib. Their sources are the Homebrew formulae of the"
    echo "same name at these versions (https://formulae.brew.sh)."
    echo
    echo "| Library | Version | Licence files |"
    echo "|---|---|---|"
} >> "$notices"
sort -u "$OUT/licenses/.bundled" | while read -r formula ver; do
    mkdir -p "$OUT/licenses/$formula"
    found=
    for f in "$BREW/Cellar/$formula/$ver"/{LICENSE,LICENCE,COPYING,NOTICE}*; do
        [[ -f "$f" ]] && cp "$f" "$OUT/licenses/$formula/" && found+="$(basename "$f") "
    done
    [[ -n "$found" ]] || die "Homebrew's $formula $ver has no licence file to carry"
    echo "| $formula | $ver | $found|" >> "$notices"
done
rm -f "$OUT/licenses/.bundled"
cat >> "$notices" <<'NOTES'

## Code that compilers and linkers add

- The Windows libraries built with llvm-mingw contain parts of LLVM's compiler-rt and libc++
  (Apache-2.0 with LLVM exceptions) and of mingw-w64's runtime (public domain and permissive
  licences, https://www.mingw-w64.org).
- mtld3d contains parts of Rust's standard library (MIT or Apache-2.0). Its 32-bit library is linked with Microsoft's Visual C++ runtime import libraries and contains
  the startup code that linking puts in; nothing else of Microsoft's is included. The files named
  like Microsoft's runtime (vcruntime140.dll, ucrtbase.dll and so on) are Wine's own.

## What is not in here

Valve's Windows client libraries are not part of Hadron. Setting up Steam downloads them from
Valve's servers to your Mac.
NOTES

log "removing debug information"
find "$OUT/dist/lib/wine" "$OUT/dist/lib/hadron" "$OUT/dist/vkd3d-proton" "$OUT/dist/dxvk" -type f \
     \( -path '*-windows/*' -o -name '*.dll' -o -name '*.exe' \) -print0 2>/dev/null |
    xargs -0 -P "$JOBS" -n 32 "$STRIP_PE" --strip-debug 2>/dev/null || true
for f in "${machos[@]}" "$EXT"/*; do
    strip -S -x "$f" 2>/dev/null || true
done

log "signing what changed"
for f in "${machos[@]}" "$EXT"/* "$OUT/steam/notproton.dylib" "$OUT/steam/overlay-shim.dylib"; do
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
