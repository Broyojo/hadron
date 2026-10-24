#!/bin/sh
# Checks that a game's first launch keeps the files Steam put in its prefix folder before the
# prefix existed: Steam syncs Cloud saves down before it starts the game, under the XP-era names
# its Cloud rules use ("Local Settings/Application Data" and so on). Runs scripts/steam-run on a
# throwaway Steam folder, with a helper (cmd) in place of a game; it needs a built dist/.
# Hadron's real folder in the Library is not touched (HADRON_SUPPORT).
#
# Usage: tests/mac/test-cloud-first-launch.sh
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T=$(mktemp -d) && T=$(cd "$T" && pwd -P)
trap 'WINEPREFIX="$T/compat/pfx" "$REPO/dist/bin/wineserver" -k 2>/dev/null; rm -rf "$T"' EXIT
export HADRON_SUPPORT="$T/support" HADRON_WATCHDOG=0
mkdir -p "$HADRON_SUPPORT" "$T/install"

# What Steam's Cloud leaves in compatdata/<id>/pfx before the first launch.
cloud="$T/compat/pfx/drive_c/users/steamuser"
put() {  # <path under the profile> <contents>
    mkdir -p "$(dirname "$cloud/$1")" && echo "$2" > "$cloud/$1"
}
put "Local Settings/Application Data/Rusted_Moss/save.ini" local
put "Application Data/Glaiel Games/Mewgenics/save.sav" roaming
put "My Documents/My Games/Game/doc.sav" documents
put "Saved Games/Game/saved.sav" saved
put "AppData/LocalLow/Studio/Game/low.sav" locallow

STEAM_COMPAT_DATA_PATH="$T/compat" STEAM_COMPAT_INSTALL_PATH="$T/install" STEAM_COMPAT_APP_ID=999999 \
    "$REPO/scripts/steam-run" run cmd /c exit >/dev/null 2>&1
WINEPREFIX="$T/compat/pfx" "$REPO/dist/bin/wineserver" -w 2>/dev/null

profile="$T/compat/pfx/drive_c/users/steamuser"
failures=0
check() {  # <path under the profile> <contents>
    if [ "$(cat "$profile/$1" 2>/dev/null)" = "$2" ]; then echo "ok    $1"
    else echo "FAIL  $1"; failures=$((failures + 1)); fi
}
check "AppData/Local/Rusted_Moss/save.ini" local
check "Local Settings/Application Data/Rusted_Moss/save.ini" local
check "AppData/Roaming/Glaiel Games/Mewgenics/save.sav" roaming
check "Application Data/Glaiel Games/Mewgenics/save.sav" roaming
check "Documents/My Games/Game/doc.sav" documents
check "My Documents/My Games/Game/doc.sav" documents
check "Saved Games/Game/saved.sav" saved
check "AppData/LocalLow/Studio/Game/low.sav" locallow
if [ -e "$T/compat/.hadron-steam-files" ]; then echo "FAIL  the files' holding folder is gone"; failures=$((failures + 1))
else echo "ok    the files' holding folder is gone"; fi
[ "$failures" -eq 0 ] && echo "all passed" || { echo "$failures failed"; exit 1; }
