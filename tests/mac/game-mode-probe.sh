#!/bin/sh
# Does macOS turn Game Mode on for a Wine window? Runs tests/win/fsmode.c full screen for 20 seconds
# from a bundle built like scripts/play's per-game bundles and declared a game, started two ways:
#   exec  as scripts/play starts games today: RunningBoard sees an anonymous process ("anon<wine>")
#   open  through LaunchServices: RunningBoard sees the bundle as an app
# and prints, for each, how RunningBoard identifies the process and whether Game Mode is on while
# the window is in front. Run it with the screen awake and unlocked: macOS lets the probe bring its
# window to the front only then (findings #44). Nothing outside a temporary folder is changed.
# Usage: tests/mac/game-mode-probe.sh [exec|open]...   (default: both)
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
pgrep -x wineserver >/dev/null && { echo "a Wine program is running; quit it first" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/hadron-game-mode.XXXXXX") || exit 1
trap 'WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -k 2>/dev/null; rm -rf "$work"' EXIT
"$ROOT/toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang" -O2 -o "$work/fsmode.exe" \
    "$ROOT/tests/win/fsmode.c" -luser32 -lgdi32 || exit 1
echo "making a prefix"
WINEPREFIX="$work/prefix" "$ROOT/scripts/wine-run" wineboot -i >/dev/null 2>&1
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
exe="Z:$(printf '%s' "$work/fsmode.exe" | tr '/' '\\')"
size=$(system_profiler SPDisplaysDataType 2>/dev/null | awk '/Main Display: Yes/ { print prev } { prev = $0 }' | grep -oE '[0-9]+ x [0-9]+' | head -1)

for how in ${@:-exec open}; do
    name="Hadron Game Mode Probe $how"
    id="com.broyojo.hadron.gamemodeprobe.$how"
    app="$work/$name.app"
    mkdir -p "$app/Contents/MacOS"
    ln -s "$(/bin/realpath "$ROOT/dist/bin/wine")" "$app/Contents/MacOS/$name"
    cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleExecutable</key><string>$name</string>
    <key>CFBundleIdentifier</key><string>$id</string>
    <key>CFBundleName</key><string>$name</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>NSPrincipalClass</key><string>WineApplication</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
    <key>LSSupportsGameMode</key><true/>
    <key>LSUIElement</key><true/>
</dict></plist>
EOF
    "$lsregister" -f "$app"
    since=$(date '+%Y-%m-%d %H:%M:%S')
    args="$exe \\\\.\\DISPLAY1 ${size%% x *} ${size##* x } 20"
    if [ "$how" = open ]; then
        open -n -a "$app" --env "WINEPREFIX=$work/prefix" --env WINEDEBUG=-all --env WINEDLLOVERRIDES=mshtml= \
            --env "WINELOADER=$app/Contents/MacOS/$name" --args $args
    else
        env -i HOME="$HOME" USER="$USER" PATH=/usr/bin:/bin WINEPREFIX="$work/prefix" WINEDEBUG=-all \
            WINEDLLOVERRIDES=mshtml= WINELOADER="$app/Contents/MacOS/$name" \
            "$app/Contents/MacOS/$name" $args >/dev/null 2>&1 &
    fi
    sleep 6
    osascript -e "tell application id \"$id\" to activate" >/dev/null 2>&1
    sleep 6
    front=$(lsappinfo info -only name "$(lsappinfo front)" 2>/dev/null | sed -n 's/.*"\(.*\)".*/\1/p' | head -1)
    mode=$(xcrun gamepolicyctl game-mode status 2>&1 | head -1 | sed 's/\x1b\[[0-9;]*m//g')
    sleep 12
    who=$(/usr/bin/log show --start "$since" --process gamepolicyd --info --debug --style compact 2>/dev/null |
          grep -oE "(anon<wine>|app<application\.$id)" | sort | uniq -c | sort -rn | head -1 | awk '{ print $2 }')
    echo "$how: RunningBoard sees ${who:-nothing}; in front: ${front:-?}; $mode"
    WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -k 2>/dev/null
    "$lsregister" -u "$app" 2>/dev/null
    sleep 3
done
