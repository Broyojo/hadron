#!/usr/bin/env bash
# Build Hadron.app: the window and the `hadron` command (app/), around a self-contained copy of
# the runtime (scripts/package-runtime.sh) in Contents/SharedSupport/runtime.
#
# Usage: scripts/build-app.sh [--updates] [codesign identity]   (default: ad hoc, for this Mac only)
#
#   --updates   the app updates itself with Sparkle (scripts/fetch-sparkle.sh), from the feed
#               attached to the latest release and with the key in packaging/update-key.pub. A
#               release is built this way; without it the app only says that a newer one exists.
#               HADRON_UPDATE_FEED and HADRON_UPDATE_KEY name another feed and key for a rehearsal.
#
# HADRON_APP is where the app is built, for a second one beside build/package/Hadron.app.
#
# The Wine loader inside the runtime keeps the signature scripts/package-loader.sh gave it. A
# release signs everything with a Developer ID identity and the hardened runtime, and notarizes.
source "$(dirname "$0")/env.sh"

updates=
[[ "${1:-}" == --updates ]] && { updates=1; shift; }
identity="${1:--}"
APP="${HADRON_APP:-$BUILD/package/Hadron.app}"
version=$(cat "$ROOT/VERSION")

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/SharedSupport"
"$ROOT/scripts/package-runtime.sh" "$APP/Contents/SharedSupport/runtime"
"$ROOT/scripts/make-icon.sh" "$APP/Contents/Resources/Hadron.icns"

swift_flags=() update_keys=
if [[ -n $updates ]]; then
    SPARKLE="$ROOT/toolchains/sparkle"
    [[ -d "$SPARKLE/Sparkle.framework" ]] || die "missing Sparkle, run scripts/fetch-sparkle.sh"
    feed="${HADRON_UPDATE_FEED:-https://github.com/Broyojo/hadron/releases/latest/download/appcast.xml}"
    key="${HADRON_UPDATE_KEY:-$(grep -v '^#' "$ROOT/packaging/update-key.pub" 2>/dev/null | tr -d '[:space:]')}"
    [[ -n "$key" ]] || die "no update key: packaging/update-key.pub holds the public half of the key updates are signed with"
    log "embedding Sparkle $(cat "$SPARKLE/.hadron-version")"
    mkdir -p "$APP/Contents/Frameworks"
    ditto "$SPARKLE/Sparkle.framework" "$APP/Contents/Frameworks/Sparkle.framework"
    licenses="$APP/Contents/SharedSupport/runtime/licenses"
    mkdir -p "$licenses/sparkle"
    cp "$SPARKLE/LICENSE" "$licenses/sparkle/LICENSE"
    printf '\nThe app updates itself with Sparkle %s (sparkle/LICENSE, MIT), in Contents/Frameworks as its\nproject released it: https://github.com/sparkle-project/Sparkle\n' \
        "$(cat "$SPARKLE/.hadron-version")" >> "$licenses/THIRD-PARTY-NOTICES.md"
    swift_flags=(-D SPARKLE -F "$SPARKLE" -framework Sparkle -Xlinker -rpath -Xlinker @executable_path/../Frameworks)
    # Checks happen without a question, as the plain notice's did; installing always asks.
    update_keys="    <key>SUFeedURL</key>
    <string>$feed</string>
    <key>SUPublicEDKey</key>
    <string>$key</string>
    <key>SUEnableAutomaticChecks</key>
    <true/>
    <key>SUAllowsAutomaticUpdates</key>
    <false/>"
    # A rehearsal's feed is served from this Mac over plain HTTP.
    [[ "$feed" == http://127.0.0.1:* ]] && update_keys="$update_keys
    <key>NSAppTransportSecurity</key>
    <dict><key>NSAllowsLocalNetworking</key><true/></dict>"
fi

log "compiling the app"
xcrun swiftc -O -swift-version 5 -target arm64-apple-macos26.0 ${swift_flags[@]+"${swift_flags[@]}"} \
    -o "$APP/Contents/MacOS/Hadron" "$ROOT"/app/*.swift

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>Hadron</string>
    <key>CFBundleIconFile</key>
    <string>Hadron</string>
    <key>CFBundleIdentifier</key>
    <string>com.broyojo.hadron</string>
    <key>CFBundleName</key>
    <string>Hadron</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$(git -C "$ROOT" rev-list --count HEAD)</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.utilities</string>
    <key>LSMinimumSystemVersion</key>
    <string>26.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright 2026 David Andrews</string>
$update_keys
</dict>
</plist>
PLIST

codesign -f -s "$identity" "$APP"
codesign -v "$APP/Contents/SharedSupport/runtime/dist/lib/wine/aarch64-unix/wine.app" || die "the loader's signature did not survive"
log "built $APP ($(du -sh "$APP" | cut -f1))"
