#!/usr/bin/env bash
# Fetch Sparkle (https://sparkle-project.org, MIT), the framework a released Hadron.app updates
# itself with, and the tools that sign an update: the project's own release, checked against the
# checksum pinned here. Only scripts/release.sh needs it; a development build has no updater.
#
#   toolchains/sparkle/Sparkle.framework   embedded in the app by scripts/build-app.sh --updates
#   toolchains/sparkle/bin/sign_update     signs the disk image for the update feed
source "$(dirname "$0")/env.sh"

SPARKLE_VERSION=2.10.0
SPARKLE_SHA256=c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c

dest="$ROOT/toolchains/sparkle"
if [[ "$(cat "$dest/.hadron-version" 2>/dev/null)" == "$SPARKLE_VERSION" ]]; then
    log "Sparkle $SPARKLE_VERSION is in $dest"
    exit 0
fi
archive="$BUILD/Sparkle-$SPARKLE_VERSION.tar.xz"
mkdir -p "$BUILD"
log "downloading Sparkle $SPARKLE_VERSION"
curl -fsSL -o "$archive" \
    "https://github.com/sparkle-project/Sparkle/releases/download/$SPARKLE_VERSION/Sparkle-$SPARKLE_VERSION.tar.xz"
[[ "$(shasum -a 256 "$archive" | cut -d' ' -f1)" == "$SPARKLE_SHA256" ]] ||
    die "Sparkle-$SPARKLE_VERSION.tar.xz does not have the pinned checksum"
rm -rf "$dest.new"
mkdir -p "$dest.new"
tar -xJf "$archive" -C "$dest.new"
# Downloaded binaries carry the quarantine attribute; clear it so they run.
xattr -dr com.apple.quarantine "$dest.new" 2>/dev/null || true
echo "$SPARKLE_VERSION" > "$dest.new/.hadron-version"
rm -rf "$dest"
mv "$dest.new" "$dest"
rm -f "$archive"
log "done: $dest"
