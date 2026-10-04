#!/usr/bin/env bash
# Make a release: Hadron.app signed with a Developer ID and the hardened runtime, notarized, in
# a disk image that is signed and notarized too, published as a draft release on GitHub with the
# Homebrew cask filled in for it.
#
# Usage: scripts/release.sh <Developer ID profile for the loader> <notarytool keychain profile>
#
#   The profile is the Developer ID provisioning profile for com.broyojo.hadron.loader
#   (docs/apple-developer-setup.md). The keychain profile is what
#   `xcrun notarytool store-credentials` saved the notarization credentials under.
#   The version is VERSION, and HEAD has to carry its tag (v<version>).
#
# Everything a release needs from Apple comes from the team's Developer ID Application
# certificate; without it in the keychain this stops before building anything.
source "$(dirname "$0")/env.sh"

profile="${1:-}" notary="${2:-}"
[[ -f "$profile" && -n "$notary" ]] || die "usage: $0 <loader.provisionprofile> <notarytool keychain profile>"
identity=$(security find-identity -v -p codesigning | awk '/Developer ID Application/ { print $2; exit }')
[[ -n "$identity" ]] || die "no Developer ID Application certificate in the keychain"
version=$(cat "$ROOT/VERSION")
[[ "$(git -C "$ROOT" describe --tags --exact-match 2>/dev/null)" == "v$version" ]] ||
    die "HEAD is not tagged v$version: tag the release commit first"
[[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] || die "the checkout has uncommitted changes"

APP="$BUILD/package/Hadron.app"
DMG="$BUILD/package/Hadron-$version.dmg"

# The loader first: it carries the provisioning profile and the entitlements, and the runtime is
# copied from dist/ with it as it is.
"$ROOT/scripts/package-loader.sh" "$profile" "$identity" --runtime
"$ROOT/scripts/build-app.sh" "$identity"

log "signing the runtime's programs and libraries"
runtime="$APP/Contents/SharedSupport/runtime"
while IFS= read -r f; do
    [[ "$f" == */wine.app/* ]] && continue
    file -b "$f" | grep -q 'Mach-O' || continue
    codesign -f -s "$identity" --options runtime --timestamp "$f"
done < <(find "$runtime/dist" "$runtime/steam" -type f \( -name '*.so' -o -name '*.dylib' -o -perm +111 \) ! -path '*-windows/*')
codesign -f -s "$identity" --options runtime --timestamp "$APP"
codesign --verify --strict "$APP"

notarize() {
    xcrun notarytool submit "$1" --keychain-profile "$notary" --wait
}
log "notarizing the app"
zip="$BUILD/package/Hadron-notarize.zip"
ditto -c -k --keepParent "$APP" "$zip"
notarize "$zip"
rm -f "$zip"
xcrun stapler staple "$APP"

"$ROOT/scripts/make-dmg.sh" "$APP"
codesign -f -s "$identity" --timestamp "$DMG"
log "notarizing the disk image"
notarize "$DMG"
xcrun stapler staple "$DMG"
spctl --assess --type open --context context:primary-signature "$DMG"

sha=$(shasum -a 256 "$DMG" | cut -d' ' -f1)
sed -e "s/@VERSION@/$version/" -e "s/@SHA256@/$sha/" "$ROOT/packaging/homebrew/hadron.rb" > "$BUILD/package/hadron.rb"
gh release create "v$version" "$DMG" --draft --title "Hadron $version" --generate-notes
log "draft release v$version created; publish it on GitHub, then put $BUILD/package/hadron.rb into the tap as Casks/hadron.rb"
