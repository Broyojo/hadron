#!/usr/bin/env bash
# Build Hadron.app: the window and the `hadron` command (app/), around a self-contained copy of
# the runtime (scripts/package-runtime.sh) in Contents/SharedSupport/runtime.
#
# Usage: scripts/build-app.sh [codesign identity]   (default: ad hoc, for this Mac only)
#
# The Wine loader inside the runtime keeps the signature scripts/package-loader.sh gave it. A
# release signs everything with a Developer ID identity and the hardened runtime, and notarizes.
source "$(dirname "$0")/env.sh"

identity="${1:--}"
APP="$BUILD/package/Hadron.app"
version=$(cat "$ROOT/VERSION")

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/SharedSupport"
"$ROOT/scripts/package-runtime.sh" "$APP/Contents/SharedSupport/runtime"
"$ROOT/scripts/make-icon.sh" "$APP/Contents/Resources/Hadron.icns"

log "compiling the app"
xcrun swiftc -O -swift-version 5 -target arm64-apple-macos26.0 \
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
</dict>
</plist>
PLIST

codesign -f -s "$identity" "$APP"
codesign -v "$APP/Contents/SharedSupport/runtime/dist/lib/wine/aarch64-unix/wine.app" || die "the loader's signature did not survive"
log "built $APP ($(du -sh "$APP" | cut -f1))"
