#!/usr/bin/env bash
# Put Hadron.app (scripts/build-app.sh) into a disk image: the app beside a link to Applications,
# compressed. What a release publishes and what the Homebrew cask downloads.
#
# Usage: scripts/make-dmg.sh [Hadron.app]   (default: build/package/Hadron.app)
source "$(dirname "$0")/env.sh"

APP="${1:-$BUILD/package/Hadron.app}"
[[ -d "$APP" ]] || die "no $APP: run scripts/build-app.sh"
version=$(cat "$APP/Contents/SharedSupport/runtime/VERSION")
out="$BUILD/package/Hadron-$version.dmg"

stage=$(mktemp -d)
# A clone, so staging costs no time or space; the image is made from the copy.
cp -Rc "$APP" "$stage/Hadron.app"
ln -s /Applications "$stage/Applications"
rm -f "$out"
log "compressing $(du -sh "$APP" | cut -f1) into $out"
hdiutil create -quiet -volname "Hadron $version" -srcfolder "$stage" -fs APFS -format ULMO "$out"
rm -rf "$stage"
log "built $out ($(du -h "$out" | cut -f1), sha256 $(shasum -a 256 "$out" | cut -d' ' -f1))"
