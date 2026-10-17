#!/bin/sh
# Does macOS turn Game Mode on for a Wine window? Runs tests/win/fsmode.c full screen until it is read
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
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
# Whatever happens, the probe's Wine ends and its bundles leave LaunchServices with its folder.
cleanup() {
    WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -k 2>/dev/null
    WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -w 2>/dev/null
    for app in "$work"/*.app; do [ -d "$app" ] && "$lsregister" -u "$app" 2>/dev/null; done
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
"$ROOT/toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang" -O2 -o "$work/fsmode.exe" \
    "$ROOT/tests/win/fsmode.c" -luser32 -lgdi32 || exit 1
echo "making a prefix"
if ! WINEPREFIX="$work/prefix" "$ROOT/scripts/wine-run" wineboot -i > "$work/wineboot.log" 2>&1; then
    echo "making the prefix failed; the probe would mean nothing:" >&2
    tail -20 "$work/wineboot.log" >&2
    exit 1
fi
launcher="Z:$(printf '%s' "$ROOT/dist/lib/wine/aarch64-windows/hadron-steam.exe" | tr '/' '\\')"
exe="Z:$(printf '%s' "$work/fsmode.exe" | tr '/' '\\')"
# The primary display's desktop size, in points: what Wine reports as \\.\DISPLAY1's mode.
size=$(osascript -l JavaScript -e 'ObjC.import("AppKit"); var f = $.NSScreen.screens.objectAtIndex(0).frame; Math.round(f.size.width) + " x " + Math.round(f.size.height)')
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
            --args "$launcher" "$exe" '\\.\DISPLAY1' "$width" "$height" 90
    else
        "$@" WINEPREFIX="$work/prefix" WINEDEBUG=-all WINEDLLOVERRIDES=mshtml= \
            WINELOADER="$app/Contents/MacOS/$name" \
            "$app/Contents/MacOS/$name" "$launcher" "$exe" '\\.\DISPLAY1' "$width" "$height" 90 >/dev/null 2>&1 &
    fi
    # Wait (at most a minute) for the program's own process: the one whose Windows command line starts
    # with the program, not hadron-steam.exe, which names it among its arguments. Paths may hold spaces.
    window_pid= waited=0
    while [ -z "$window_pid" ] && [ "$waited" -lt 60 ]; do
        sleep 2; waited=$((waited + 2))
        window_pid=$(ps -axo pid=,args= | awk '{ pid = $1; sub(/^ *[0-9]+ /, "") }
            /^[A-Z]:\\.*fsmode\.exe( |$)/ && !/hadron-steam\.exe/ { print pid; exit }')
    done
    if [ -z "$window_pid" ]; then
        echo "$how: the program did not start within a minute; no result" >&2
        WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -k 2>/dev/null
        WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -w 2>/dev/null
        "$lsregister" -u "$app" 2>/dev/null
        continue
    fi
    sleep 3
    # Ask for the front only when the bundle is already running: activate would start it otherwise.
    before=$(pgrep -f "$app/Contents/MacOS/" | wc -l | tr -d " ")
    if [ -n "$(lsappinfo find bundleid="$id" 2>/dev/null)" ]; then
        osascript -e "tell application id \"$id\" to activate" >/dev/null 2>&1
    fi
    # Then give the window up to half a minute to be in front before Game Mode is read.
    front= waited=0
    while [ "$front" != "$name" ] && [ "$waited" -lt 30 ]; do
        sleep 2; waited=$((waited + 2))
        front=$(lsappinfo info -only name "$(lsappinfo front)" 2>/dev/null | sed -n 's/.*"\(.*\)".*/\1/p' | head -1)
    done
    sleep 3
    after=$(pgrep -f "$app/Contents/MacOS/" | wc -l | tr -d " ")
    mode=$(xcrun gamepolicyctl game-mode status 2>&1 | head -1 | sed 's/\x1b\[[0-9;]*m//g')
    sleep 6
    who=$(/usr/bin/log show --start "$since" --process gamepolicyd --info --debug --style compact 2>/dev/null |
          grep -oE "(anon|app)<[^]]*:${window_pid}\\]" | sed 's/:[0-9]*]$//' | sort | uniq -c | sort -rn | head -1 | awk '{ print $2 }')
    note=
    [ "$after" -gt "$before" ] && note=" (asking for the front started another process: ignore this line)"
    echo "$how: RunningBoard sees the window's process (pid ${window_pid:-?}) as ${who:-nothing: RunningBoard has no record of it}; in front: ${front:-?}; $mode$note"
    # The next route starts only once this one's Wine has ended.
    WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -k 2>/dev/null
    WINEPREFIX="$work/prefix" "$ROOT/dist/bin/wineserver" -w 2>/dev/null
    "$lsregister" -u "$app" 2>/dev/null
done
