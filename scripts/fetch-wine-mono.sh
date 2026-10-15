#!/usr/bin/env bash
# Fetch Wine Mono (https://gitlab.winehq.org/mono/wine-mono), Wine's .NET Framework, and put it
# where Wine looks for a shared install: dist/share/wine/mono/wine-mono-<version>. With it there,
# Wine's mscoree sets .NET up in a new prefix without asking, and programs that use .NET, or only
# import mscoree.dll, run. The version is the one Wine asks for (WINE_MONO_VERSION in
# dlls/mscoree/mscoree_private.h); the project's own release, checked against the checksums
# pinned here. Its arm64 build also holds the x86 and x86-64 runtimes.
#
#   toolchains/wine-mono/wine-mono-<version>   the release as it comes, and its COPYING
#   dist/share/wine/mono/wine-mono-<version>   a clone of it, for Wine and the runtime
#
# Usage: scripts/fetch-wine-mono.sh
source "$(dirname "$0")/env.sh"

MONO_VERSION=11.3.0
MONO_SHA256=227cbeef943c71d9bbcbd9d00ea026decbc4ecfb758346f13d2a34a7fd8ecfcf
COPYING_SHA256=0265443ffc97c9450dbcf402c2430bf9fe87d7a6c92d5d7684fcecfe749bf75d

wanted=$(sed -n 's/^#define WINE_MONO_VERSION "\(.*\)"/\1/p' "$SRC/wine/dlls/mscoree/mscoree_private.h" 2>/dev/null)
[[ -z "$wanted" || "$wanted" == "$MONO_VERSION" ]] ||
    die "Wine asks for Wine Mono $wanted, this script fetches $MONO_VERSION: update the version and checksums"

dest="$ROOT/toolchains/wine-mono"
if [[ "$(cat "$dest/.hadron-version" 2>/dev/null)" != "$MONO_VERSION" ]]; then
    archive="$BUILD/wine-mono-$MONO_VERSION-arm64.tar.xz"
    mkdir -p "$BUILD"
    log "downloading Wine Mono $MONO_VERSION"
    curl -fsSL -o "$archive" \
        "https://github.com/madewokherd/wine-mono/releases/download/wine-mono-$MONO_VERSION/wine-mono-$MONO_VERSION-arm64.tar.xz"
    [[ "$(shasum -a 256 "$archive" | cut -d' ' -f1)" == "$MONO_SHA256" ]] ||
        die "wine-mono-$MONO_VERSION-arm64.tar.xz does not have the pinned checksum"
    rm -rf "$dest.new"
    mkdir -p "$dest.new"
    tar -xJf "$archive" -C "$dest.new"
    # The release carries no licence file; its repository's COPYING at the release's tag.
    curl -fsSL -o "$dest.new/COPYING" \
        "https://raw.githubusercontent.com/madewokherd/wine-mono/wine-mono-$MONO_VERSION/COPYING"
    [[ "$(shasum -a 256 "$dest.new/COPYING" | cut -d' ' -f1)" == "$COPYING_SHA256" ]] ||
        die "Wine Mono's COPYING does not have the pinned checksum"
    xattr -dr com.apple.quarantine "$dest.new" 2>/dev/null || true
    echo "$MONO_VERSION" > "$dest.new/.hadron-version"
    rm -rf "$dest"
    mv "$dest.new" "$dest"
    rm -f "$archive"
fi

# Wine looks only for the version it asks for; an older one would just take space.
rm -rf "$DIST/share/wine/mono"
mkdir -p "$DIST/share/wine/mono"
cp -Rc "$dest/wine-mono-$MONO_VERSION" "$DIST/share/wine/mono/"
log "Wine Mono $MONO_VERSION is in $DIST/share/wine/mono"
