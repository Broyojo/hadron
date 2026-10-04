#!/usr/bin/env bash
# Make a release: Hadron.app signed with a Developer ID and the hardened runtime, notarized, in
# a disk image that is signed and notarized too, published as a draft release on GitHub with the
# Homebrew cask filled in for it.
#
# Usage: scripts/release.sh [--local] <Developer ID profile for the loader> <API key ID> <issuer ID>
#
#   The profile is the Developer ID provisioning profile for com.broyojo.hadron.loader
#   (docs/apple-developer-setup.md). The key is a team API key for App Store Connect, read by
#   notarytool from ~/.appstoreconnect/private_keys/AuthKey_<key ID>.p8; the issuer ID is the
#   team's. The version is VERSION, and HEAD has to carry its tag (v<version>).
#
#   --local   a rehearsal: no tag is required and nothing is published. The app and the disk
#             image are still signed and notarized, which sends them to Apple.
#
# Everything a release needs from Apple comes from the team's Developer ID Application
# certificate; without it in the keychain this stops before building anything.
source "$(dirname "$0")/env.sh"

local_only=
[[ "${1:-}" == --local ]] && { local_only=1; shift; }
profile="${1:-}" key_id="${2:-}" issuer="${3:-}"
key="$HOME/.appstoreconnect/private_keys/AuthKey_$key_id.p8"
[[ -f "$profile" && -n "$key_id" && -n "$issuer" ]] || die "usage: $0 [--local] <loader.provisionprofile> <API key ID> <issuer ID>"
[[ -f "$key" ]] || die "no API key at $key"
identity=$(security find-identity -v -p codesigning | awk '/Developer ID Application/ { print $2; exit }')
[[ -n "$identity" ]] || die "no Developer ID Application certificate in the keychain"
version=$(cat "$ROOT/VERSION")
if [[ -z $local_only ]]; then
    [[ "$(git -C "$ROOT" describe --tags --exact-match 2>/dev/null)" == "v$version" ]] ||
        die "HEAD is not tagged v$version: tag the release commit first"
    [[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] || die "the checkout has uncommitted changes"
fi

APP="$BUILD/package/Hadron.app"

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

# notarytool reads the key file itself. (A profile saved with `notarytool store-credentials`
# was reported as saved and then not found, so the key is named directly.)
notarize() {
    xcrun notarytool submit "$1" --key "$key" --key-id "$key_id" --issuer "$issuer" --wait |
        tee "$BUILD/package/notary.log"
    grep -q "status: Accepted" "$BUILD/package/notary.log" || die "the notary service did not accept $1"
}
log "notarizing the app"
zip="$BUILD/package/Hadron-notarize.zip"
ditto -c -k --keepParent "$APP" "$zip"
notarize "$zip"
rm -f "$zip"
xcrun stapler staple "$APP"

"$ROOT/scripts/make-dmg.sh" "$APP"
# The image is named after the version the app reports, which for a rehearsal carries the commit.
DMG="$BUILD/package/Hadron-$(cat "$APP/Contents/SharedSupport/runtime/VERSION").dmg"
codesign -f -s "$identity" --timestamp "$DMG"
log "notarizing the disk image"
notarize "$DMG"
xcrun stapler staple "$DMG"
spctl --assess --type open --context context:primary-signature "$DMG"

if [[ -n $local_only ]]; then
    log "rehearsal done: $DMG is signed and notarized; nothing was published"
    exit 0
fi
sha=$(shasum -a 256 "$DMG" | cut -d' ' -f1)
sed -e "s/@VERSION@/$version/" -e "s/@SHA256@/$sha/" "$ROOT/packaging/homebrew/hadron.rb" > "$BUILD/package/hadron.rb"
gh release create "v$version" "$DMG" --draft --title "Hadron $version" --generate-notes
log "draft release v$version created; publish it on GitHub, then put $BUILD/package/hadron.rb into the tap as Casks/hadron.rb"
