#!/bin/sh
# Checks that a launch brings a prefix's mtld3d copies up to the runtime's: a prefix gets them
# when it is made (scripts/mtld3d-prefix enable), and without the refresh in scripts/play it kept
# that build's mtld3d for good. Makes a throwaway prefix, makes its copies stale, starts a small
# program through scripts/play and compares. Needs a built dist/ with mtld3d.
#
# Usage: tests/mac/test-mtld3d-refresh.sh
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T=$(mktemp -d) && T=$(cd "$T" && pwd -P)
export WINEPREFIX="$T/pfx" HADRON_SUPPORT="$T/support" HADRON_WATCHDOG=0
trap '"$REPO/dist/bin/wineserver" -k 2>/dev/null; rm -rf "$T"' EXIT
mkdir -p "$HADRON_SUPPORT"
H="$REPO/dist/lib/hadron/d3d9"
[ -f "$H/i386/d3d9.dll" ] || { echo "no mtld3d in dist; run scripts/build-mtld3d.sh"; exit 1; }
WINEDEBUG=-all WINEDLLOVERRIDES="mshtml=" "$REPO/dist/bin/wine" wineboot --init >/dev/null 2>&1
"$REPO/dist/bin/wineserver" -w
"$REPO/scripts/mtld3d-prefix" enable "$WINEPREFIX" >/dev/null
W="$WINEPREFIX/drive_c/windows"
# Stale copies, as a prefix made by an older runtime has: Wine's own d3d9 in their place.
cp "$REPO/dist/lib/wine/i386-windows/d3d9.dll" "$W/syswow64/d3d9.dll"
cp "$REPO/dist/lib/wine/aarch64-windows/d3d9.dll" "$W/system32/d3d9.dll"
rm -f "$W/syswow64/mtld3d.dll" "$W/system32/mtld3d.dll"

"$REPO/scripts/play" 0 "$REPO/build/hello-x86_64.exe" >/dev/null 2>&1

failures=0
for f in "i386/d3d9.dll syswow64/d3d9.dll" "aarch64/d3d9.dll system32/d3d9.dll" \
         "markers/syswow64/mtld3d.dll syswow64/mtld3d.dll" "markers/system32/mtld3d.dll system32/mtld3d.dll"; do
    src=${f% *} dst=${f#* }
    if cmp -s "$H/$src" "$W/$dst"; then echo "ok    $dst is the runtime's"
    else echo "FAIL  $dst is not the runtime's"; failures=$((failures + 1)); fi
done
if HADRON_D3D9=wined3d "$REPO/scripts/play" 0 "$REPO/build/hello-x86_64.exe" >/dev/null 2>&1; then
    echo "ok    HADRON_D3D9=wined3d still launches"
else
    echo "FAIL  HADRON_D3D9=wined3d does not launch"; failures=$((failures + 1))
fi
[ "$failures" -eq 0 ] && echo "all passed" || { echo "$failures failed"; exit 1; }
