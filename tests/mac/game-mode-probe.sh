#!/bin/sh
# Does macOS turn Game Mode on for a Wine window? Runs tests/win/fsmode.c full screen for 20 seconds
# from a bundle built like scripts/play's per-game bundles and declared a game, through
# hadron-steam.exe as a game is (the full-screen window belongs to the child it starts), started
# two ways:
#   exec  as scripts/play starts games today: RunningBoard sees an anonymous process ("anon<wine>")
#   open  through LaunchServices: RunningBoard sees the first process as the bundle's app
# and prints, for each, how RunningBoard identifies the process that owns the window, which app is
# in front and whether Game Mode is on. Both start from the same clean environment. Run it with the
# screen awake and unlocked: macOS lets the probe bring its window to the front only then (findings
# #44). Nothing outside a temporary folder is changed.
# Usage: tests/mac/game-mode-probe.sh [exec|open]...   (default: both)
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
pgrep -x wineserver >/dev/null && { echo "a Wine program is running; quit it first" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/hadron-game-mode.XXXXXX") || exit 1
trap 'WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -k 2>/dev/null; rm -rf "$work"' EXIT
"$ROOT/toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang" -O2 -o "$work/fsmode.exe" \
    "$ROOT/tests/win/fsmode.c" -luser32 -lgdi32 || exit 1
echo "making a prefix"
if ! WINEPREFIX="$work/prefix" "$ROOT/scripts/wine-run" wineboot -i > "$work/wineboot.log" 2>&1; then
    echo "making the prefix failed; the probe would mean nothing:" >&2
    tail -20 "$work/wineboot.log" >&2
    exit 1
fi
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
launcher="Z:$(printf '%s' "$ROOT/dist/lib/wine/aarch64-windows/hadron-steam.exe" | tr '/' '\\')"
exe="Z:$(printf '%s' "$work/fsmode.exe" | tr '/' '\\')"
size=$(system_profiler SPDisplaysDataType 2>/dev/null | awk '/Main Display: Yes/ { print prev } { prev = $0 }' | grep -oE '[0-9]+ x [0-9]+' | head -1)
width=${size%% x *} height=${size##* x }

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
    # The same clean environment both ways: open hands its own environment to the app it opens.
    set -- env -i HOME="$HOME" USER="$USER" PATH=/usr/bin:/bin
    if [ "$how" = open ]; then
        "$@" open -n -a "$app" --env "WINEPREFIX=$work/prefix" --env WINEDEBUG=-all \
            --env WINEDLLOVERRIDES=mshtml= --env "WINELOADER=$app/Contents/MacOS/$name" \
            --args "$launcher" "$exe" '\\.\DISPLAY1' "$width" "$height" 20
    else
        "$@" WINEPREFIX="$work/prefix" WINEDEBUG=-all WINEDLLOVERRIDES=mshtml= \
            WINELOADER="$app/Contents/MacOS/$name" \
            "$app/Contents/MacOS/$name" "$launcher" "$exe" '\\.\DISPLAY1' "$width" "$height" 20 >/dev/null 2>&1 &
    fi
    sleep 6
    # Ask for the front only when the bundle is already running: activate would start it otherwise.
    before=$(pgrep -f "$app/Contents/MacOS/" | wc -l | tr -d " ")
    if [ -n "$(lsappinfo find bundleid="$id" 2>/dev/null)" ]; then
        osascript -e "tell application id \"$id\" to activate" >/dev/null 2>&1
    fi
    sleep 6
    after=$(pgrep -f "$app/Contents/MacOS/" | wc -l | tr -d " ")
    front=$(lsappinfo info -only name "$(lsappinfo front)" 2>/dev/null | sed -n 's/.*"\(.*\)".*/\1/p' | head -1)
    mode=$(xcrun gamepolicyctl game-mode status 2>&1 | head -1 | sed 's/\x1b\[[0-9;]*m//g')
    # The program's own process: hadron-steam.exe, which started it, names it among its arguments.
    window_pid=$(ps -axo pid=,args= | awk '$2 ~ /fsmode\.exe$/ { print $1; exit }')
    sleep 12
    who=$(/usr/bin/log show --start "$since" --process gamepolicyd --info --debug --style compact 2>/dev/null |
          grep -oE "(anon|app)<[^]]*:${window_pid:-none}\\]" | sed 's/:[0-9]*]$//' | sort | uniq -c | sort -rn | head -1 | awk '{ print $2 }')
    note=
    [ "$after" -gt "$before" ] && note=" (asking for the front started another process: ignore this line)"
    echo "$how: RunningBoard sees the window's process (pid ${window_pid:-?}) as ${who:-nothing: RunningBoard has no record of it}; in front: ${front:-?}; $mode$note"
    WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -k 2>/dev/null
    "$lsregister" -u "$app" 2>/dev/null
    sleep 3
done
