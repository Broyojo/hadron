#!/bin/sh
# Checks that a game's first launch keeps the files Steam put in its prefix folder before the
# prefix existed: Steam syncs Cloud saves down before it starts the game, under the XP-era names
# its Cloud rules use ("Local Settings/Application Data" and so on). Runs scripts/steam-run on
# throwaway Steam folders, with a helper (cmd) in place of a game; it needs a built dist/.
# Hadron's real folder in the Library is not touched (HADRON_SUPPORT).
#
# Usage: tests/mac/test-cloud-first-launch.sh
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T=$(mktemp -d) && T=$(cd "$T" && pwd -P)
trap 'for p in "$T"/*/compat/pfx; do WINEPREFIX="$p" "$REPO/dist/bin/wineserver" -k 2>/dev/null; done; rm -rf "$T"' EXIT
export HADRON_WATCHDOG=0
failures=0

# One Steam folder per case, as Steam's Cloud leaves compatdata/<id> before the first launch.
start() {  # <case>
    C="$T/$1"
    export HADRON_SUPPORT="$C/support"
    mkdir -p "$HADRON_SUPPORT" "$C/install"
    cloud="$C/compat/pfx/drive_c/users/steamuser"
    held="$C/compat/.hadron-steam-files/steamuser"
    profile="$cloud"
    echo "$1:"
}
put() {  # <folder> <path under it> <contents>
    mkdir -p "$(dirname "$1/$2")" && echo "$3" > "$1/$2"
}
launch() {
    STEAM_COMPAT_DATA_PATH="$C/compat" STEAM_COMPAT_INSTALL_PATH="$C/install" STEAM_COMPAT_APP_ID=999999 \
        "$REPO/scripts/steam-run" run cmd /c exit >/dev/null 2>&1
    status=$?
    WINEPREFIX="$C/compat/pfx" "$REPO/dist/bin/wineserver" -w 2>/dev/null
}
ok() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; failures=$((failures + 1)); }
check() {  # <path under the profile> <contents>
    if [ "$(cat "$profile/$1" 2>/dev/null)" = "$2" ]; then ok "$1"; else fail "$1"; fi
}
check_held_gone() {
    if [ -e "$C/compat/.hadron-steam-files" ]; then fail "the files' holding folder is still there"
    else ok "the files' holding folder is gone"; fi
}

start "first launch"
put "$cloud" "Local Settings/Application Data/Rusted_Moss/save.ini" local
put "$cloud" "Application Data/Glaiel Games/Mewgenics/save.sav" roaming
put "$cloud" "My Documents/My Games/Game/doc.sav" documents
put "$cloud" "Saved Games/Game/saved.sav" saved
put "$cloud" "AppData/LocalLow/Studio/Game/low.sav" locallow
put "$C/compat/pfx/drive_c" "windows/stale.txt" stale  # a broken prefix's: not Steam's
mkdir -p "$C/elsewhere" && ln -s "$C/elsewhere" "$cloud/Desktop"  # and Wine's link from it
launch
check "AppData/Local/Rusted_Moss/save.ini" local
check "Local Settings/Application Data/Rusted_Moss/save.ini" local
check "AppData/Roaming/Glaiel Games/Mewgenics/save.sav" roaming
check "Application Data/Glaiel Games/Mewgenics/save.sav" roaming
check "Documents/My Games/Game/doc.sav" documents
check "My Documents/My Games/Game/doc.sav" documents
check "Saved Games/Game/saved.sav" saved
check "AppData/LocalLow/Studio/Game/low.sav" locallow
if [ -e "$C/compat/pfx/drive_c/windows/stale.txt" ]; then fail "only drive_c/users is kept"
else ok "only drive_c/users is kept"; fi
if [ -d "$profile/Desktop" ] && [ ! -L "$profile/Desktop" ]; then ok "Desktop is the profile's own folder"
else fail "Desktop is the profile's own folder"; fi
check_held_gone

# An earlier launch kept the files and then failed to make the prefix; Steam has synced again since.
start "a sync after a failed first launch"
put "$held" "Application Data/Game/save.sav" older
put "$held" "Application Data/Game/only-kept.sav" kept
put "$cloud" "Application Data/Game/save.sav" newer
launch
check "AppData/Roaming/Game/save.sav" newer
check "AppData/Roaming/Game/only-kept.sav" kept
check_held_gone

# The files cannot go back (here a file stands where the profile needs a folder): the launch stops
# and keeps them, instead of starting the game without them.
start "files that cannot go back"
put "$cloud" "AppData" "not a folder"
put "$cloud" "Saved Games/Game/saved.sav" saved
launch
if [ "$status" -ne 0 ]; then ok "the launch stops"; else fail "the launch stops"; fi
if [ "$(cat "$held/Saved Games/Game/saved.sav" 2>/dev/null)" = saved ]; then ok "the files are kept"
else fail "the files are kept"; fi
if [ -e "$C/compat/pfx/.hadron-profile-v1" ]; then fail "the profile is not marked done"
else ok "the profile is not marked done"; fi

[ "$failures" -eq 0 ] && echo "all passed" || { echo "$failures failed"; exit 1; }
