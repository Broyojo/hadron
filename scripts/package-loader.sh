#!/usr/bin/env bash
# Wrap the Wine loader in an app bundle signed with a provisioning profile, so it can carry
# the cross-architecture entitlement (memory below 4GB). Every Wine process runs this loader.
#
# Usage: scripts/package-loader.sh <profile.provisionprofile> [codesign identity] [--runtime]
#
#   identity   defaults to the first "Apple Development" identity in the keychain
#   --runtime  enable the hardened runtime (required for notarized distribution)
#
# Layout produced in dist/:
#   lib/wine/aarch64-unix/wine.app/Contents/{Info.plist,MacOS/wine,embedded.provisionprofile}
#   lib/wine/aarch64-unix/wine -> wine.app/Contents/MacOS/wine   (ntdll execs this for children)
#   bin/wine -> ../lib/wine/aarch64-unix/wine.app/Contents/MacOS/wine

source "$(dirname "$0")/env.sh"

profile="${1:-}" identity="" runtime=()
shift || true
for arg; do
    case $arg in
        --runtime) runtime=(--options runtime) ;;
        *) identity="$arg" ;;
    esac
done
[[ -f "$profile" ]] || die "usage: $0 <profile.provisionprofile> [identity] [--runtime]"

if [[ -z "$identity" ]]; then
    # by SHA-1 hash: several certificates can share the same name
    identity=$(security find-identity -v -p codesigning | awk '/Apple Development/ { print $2; exit }')
    [[ -n "$identity" ]] || die "no Apple Development identity found; create one in Xcode > Settings > Accounts"
fi

UNIX_DIR="$DIST/lib/wine/aarch64-unix"
APP="$UNIX_DIR/wine.app"
WORK="$BUILD/package-loader"
mkdir -p "$WORK"

log "reading $profile"
security cms -D -i "$profile" > "$WORK/profile.plist"

# Entitlements: exactly what the profile grants, plus unrestricted ones Hadron needs.
python3 - "$WORK/profile.plist" "$WORK/entitlements.plist" "$WORK/bundle-id" "${#runtime[@]}" <<'EOF'
import plistlib, sys
profile = plistlib.load(open(sys.argv[1], "rb"))
ents = dict(profile["Entitlements"])
if not any(k.startswith("com.apple.developer.cross-architecture-support") for k in ents):
    sys.exit("profile does not grant com.apple.developer.cross-architecture-support; "
             "enable Cross-architecture Compatibility Framework on the App ID and regenerate it")
appid = ents["com.apple.application-identifier"]
open(sys.argv[3], "w").write(appid.split(".", 1)[1])
ents["com.apple.security.custom-x18-abi-toggle"] = True
if sys.argv[4] != "0":  # hardened runtime
    ents["com.apple.security.cs.allow-jit"] = True
    ents["com.apple.security.cs.allow-unsigned-executable-memory"] = True
    ents["com.apple.security.cs.allow-dyld-environment-variables"] = True
    ents["com.apple.security.cs.disable-library-validation"] = True  # ad-hoc .so unixlibs, bundled dylibs
plistlib.dump(ents, open(sys.argv[2], "wb"))
print("entitlements:", ", ".join(sorted(ents)))
EOF
bundle_id=$(cat "$WORK/bundle-id")

# Find the real loader binary (first run: lib/wine/aarch64-unix/wine; later: already in the bundle).
if [[ -f "$UNIX_DIR/wine" && ! -L "$UNIX_DIR/wine" ]]; then
    loader="$UNIX_DIR/wine"
elif [[ -f "$APP/Contents/MacOS/wine" ]]; then
    loader="$APP/Contents/MacOS/wine"
else
    die "no Wine loader found in $UNIX_DIR"
fi

log "building $APP ($bundle_id)"
mkdir -p "$APP/Contents/MacOS"
if [[ "$loader" != "$APP/Contents/MacOS/wine" ]]; then
    rm -f "$APP/Contents/MacOS/wine"
    mv "$loader" "$APP/Contents/MacOS/wine"
fi
cp "$profile" "$APP/Contents/embedded.provisionprofile"
version=$("$APP/Contents/MacOS/wine" --version 2>/dev/null | sed 's/^wine-//' || echo 0)
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>wine</string>
    <key>CFBundleIdentifier</key>
    <string>$bundle_id</string>
    <key>CFBundleName</key>
    <string>Hadron Loader</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$version</string>
    <key>LSMinimumSystemVersion</key>
    <string>26.5</string>
    <key>NSPrincipalClass</key>
    <string>WineApplication</string>
    <key>LSUIElement</key>
    <true/>
</dict>
</plist>
EOF

ln -sfn wine.app/Contents/MacOS/wine "$UNIX_DIR/wine"
rm -f "$DIST/bin/wine"
ln -s ../lib/wine/aarch64-unix/wine.app/Contents/MacOS/wine "$DIST/bin/wine"

log "signing with \"$identity\""
codesign --force --sign "$identity" --entitlements "$WORK/entitlements.plist" ${runtime[@]+"${runtime[@]}"} "$APP"
codesign --verify --strict "$APP"
log "done; entitlements in the signature:"
codesign -d --entitlements - --xml "$APP" 2>/dev/null | plutil -p - | sed 's/^/    /'
