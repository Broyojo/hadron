#!/usr/bin/env bash
# Make the draft release that scripts/release.sh created public: give it its notes and the update
# feed (appcast.xml, with the same notes), then publish it as the latest release. Older copies of
# Hadron.app read the feed from the latest release, so they learn of this version the moment it
# is published and not before.
#
# Usage: scripts/publish-release.sh <notes.md>
#
# The update key has to be in the login keychain (scripts/make-appcast.sh). The disk image on
# GitHub has to be the one in build/package: nothing is published if they differ.
source "$(dirname "$0")/env.sh"

notes="${1:-}"
[[ -f "$notes" ]] || die "usage: $0 <notes.md>"
version=$(cat "$ROOT/VERSION")
APP="$BUILD/package/Hadron.app"
DMG="$BUILD/package/Hadron-$version.dmg"
[[ -d "$APP" && -f "$DMG" ]] || die "no build of $version in $BUILD/package: run scripts/release.sh"
[[ "$(cat "$APP/Contents/SharedSupport/runtime/VERSION")" == "$version" ]] || die "$APP is not version $version"

state=$(gh release view "v$version" --json isDraft,assets \
    --jq '(.isDraft | tostring) + " " + ([.assets[] | select(.name == "'"$(basename "$DMG")"'") | .digest] | first // "")')
[[ "$state" == "true "* ]] || die "v$version is not a draft release: it is published already, or scripts/release.sh has not run"
[[ "${state#true }" == "sha256:$(shasum -a 256 "$DMG" | cut -d' ' -f1)" ]] ||
    die "the disk image on GitHub is not $DMG"

feed="$BUILD/package/appcast.xml"
"$ROOT/scripts/make-appcast.sh" "$APP" "$DMG" "$notes" "$feed"
gh release upload "v$version" "$feed" --clobber
gh release edit "v$version" --title "Hadron $version" --notes-file "$notes" --draft=false --latest
log "published: $(gh release view "v$version" --json url --jq .url)"
log "the feed: https://github.com/Broyojo/hadron/releases/latest/download/appcast.xml"
log "now put $BUILD/package/hadron.rb into the tap as Casks/hadron.rb"
