#!/bin/sh
# Checks scripts/runtime-versions.sh on throwaway folders: which copy of the runtime a launch
# gets, how the link moves, and above all what is deleted and what never is. No Wine and no
# Steam are involved; Hadron's real folder in the Library is not touched (HADRON_SUPPORT).
#
# Usage: tests/mac/test-runtime-versions.sh
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T=$(mktemp -d) && T=$(cd "$T" && pwd -P)
busy=
trap '[ -z "$busy" ] || kill "$busy" 2>/dev/null; rm -rf "$T"' EXIT
failures=0
check() {  # <what> <command...>
    what="$1"; shift
    if "$@" >/dev/null 2>&1; then echo "ok    $what"; else echo "FAIL  $what"; failures=$((failures + 1)); fi
}
not() { ! "$@"; }

# An "installed app" is a folder with a BUILD-ID and something in it.
app() {  # <folder> <build id>
    mkdir -p "$1/dist/bin" "$1/scripts"
    echo "$2" > "$1/BUILD-ID"
    echo "runtime $2" > "$1/dist/bin/payload"
}
export HADRON_SUPPORT="$T/support"
mkdir -p "$HADRON_SUPPORT"
ROOT="$REPO"
. "$REPO/scripts/paths.sh"
. "$REPO/scripts/runtime-versions.sh"
link="$HADRON_SUPPORT/runtime"

app "$T/app" 1.0-aaaa
app "$T/checkout" ""; rm "$T/checkout/BUILD-ID"

echo "--- copies"
check "a checkout has no build id" test -z "$(runtime_id "$T/checkout")"
check "an app has one" test "$(runtime_id "$T/app")" = 1.0-aaaa
runtime_lock
one=$(runtime_copy "$T/app")
check "the copy is named after the build" test "$one" = "$HADRON_RUNTIMES/1.0-aaaa"
check "the copy is complete" cmp "$one/dist/bin/payload" "$T/app/dist/bin/payload"
check "copying again changes nothing" test "$(runtime_copy "$T/app")" = "$one"
check "a checkout is not copied" not runtime_copy "$T/checkout"
mkdir "$HADRON_RUNTIMES/2.0-taken"
app "$T/other" 2.0-taken
check "a folder of that name that is not a copy is left alone" not runtime_copy "$T/other"
check "and is still there, empty" test -z "$(ls -A "$HADRON_RUNTIMES/2.0-taken")"
rmdir "$HADRON_RUNTIMES/2.0-taken"
runtime_unlock

echo "--- the link"
check "the link is made" runtime_point "$one"
check "it names the copy" test "$(readlink "$link")" = "$one"
rm "$link"; mkdir "$link"; echo keep > "$link/file"
check "a real folder in the link's place is refused" not runtime_point "$one"
check "and keeps its contents" test "$(cat "$link/file")" = keep
rm -rf "$link"; runtime_point "$one"

echo "--- a launch after an update"
echo "$T/app" > "$HADRON_SUPPORT/source"
check "nothing to do when the app's build is the one running" test -z "$(runtime_latest "$one")"
# The app is replaced by a newer one at the same path, as an update does.
rm -rf "$T/app"; app "$T/app" 1.1-bbbb
two=$(runtime_latest "$one")
check "the next launch gets the copy of the new build" test "$two" = "$HADRON_RUNTIMES/1.1-bbbb"
check "the link moved to it" test "$(readlink "$link")" = "$two"
check "the old copy is still there, for a game running from it" test -f "$one/dist/bin/payload"
check "a launch from the new copy has nothing to do" test -z "$(runtime_latest "$two")"
mv "$T/app" "$T/app-moved"
check "an app that is gone changes nothing" test -z "$(runtime_latest "$two")"
check "the link stays" test "$(readlink "$link")" = "$two"
mv "$T/app-moved" "$T/app"

echo "--- deleting copies"
# A process that runs from the first copy, named as Wine's are.
cat > "$T/busy.c" <<'EOF'
#include <unistd.h>
int main(void) { sleep(120); return 0; }
EOF
cc -o "$one/dist/bin/wine-busy" "$T/busy.c" || { echo "cannot compile the test program"; exit 1; }
"$one/dist/bin/wine-busy" >/dev/null 2>&1 &
busy=$!
sleep 1
app "$HADRON_RUNTIMES/0.9-idle" 0.9-idle
app "$HADRON_RUNTIMES/0.8-chosen" 0.8-chosen; touch "$HADRON_RUNTIMES/0.8-chosen/.used"
mkdir "$HADRON_RUNTIMES/no-build-id"; echo x > "$HADRON_RUNTIMES/no-build-id/file"
app "$T/elsewhere" 0.7-linked; ln -s "$T/elsewhere" "$HADRON_RUNTIMES/0.7-linked"
echo x > "$HADRON_RUNTIMES/a-file"
mkdir "$HADRON_RUNTIMES/.new.999999"; echo x > "$HADRON_RUNTIMES/.new.999999/half"
touch -t 202001010000 "$one/.used" 2>/dev/null
check "a process runs from the first copy" runtime_in_use "$one"
check "none runs from the idle one" not runtime_in_use "$HADRON_RUNTIMES/0.9-idle"
runtime_lock; runtime_prune "$two"; runtime_unlock
check "the copy in use is kept" test -f "$one/dist/bin/payload"
check "the current copy is kept" test -f "$two/dist/bin/payload"
check "the copy a launch chose just now is kept" test -d "$HADRON_RUNTIMES/0.8-chosen"
check "the idle copy is deleted" test ! -e "$HADRON_RUNTIMES/0.9-idle"
check "a folder without a build id is kept" test -f "$HADRON_RUNTIMES/no-build-id/file"
check "a link is kept" test -L "$HADRON_RUNTIMES/0.7-linked"
check "what a link names is untouched" test -f "$T/elsewhere/dist/bin/payload"
check "a file is kept" test -f "$HADRON_RUNTIMES/a-file"
check "what a dead launch left half copied is deleted" test ! -e "$HADRON_RUNTIMES/.new.999999"
kill $busy 2>/dev/null; wait $busy 2>/dev/null
runtime_lock; runtime_prune "$two"; runtime_unlock
check "once its process has ended, the first copy is deleted" test ! -e "$one"

echo "--- an app Steam's link names directly, as Hadron 0.2.0 set it up"
rm -rf "$HADRON_SUPPORT"; mkdir -p "$HADRON_SUPPORT"
app "$T/old-style" 1.2-cccc
ln -s "$T/old-style" "$link"
got=$(runtime_latest "$T/old-style")
check "the launch gets a copy" test "$got" = "$HADRON_RUNTIMES/1.2-cccc"
check "the link moved to the copy" test "$(readlink "$link")" = "$got"
check "the app is written down as the installed one" test "$(cat "$HADRON_SUPPORT/source")" = "$T/old-style"
check "the app itself is untouched" test -f "$T/old-style/dist/bin/payload"
runtime_lock; runtime_prune "$got"; runtime_unlock
check "and still is after a clean-up" test -f "$T/old-style/BUILD-ID"

echo "--- the lock"
mkdir -p "$HADRON_RUNTIMES/.lock"; echo 999999 > "$HADRON_RUNTIMES/.lock/pid"
check "a lock whose holder died is taken over" runtime_lock
check "by this process" test "$(cat "$HADRON_RUNTIMES/.lock/pid")" = $$
runtime_unlock
check "and released" test ! -e "$HADRON_RUNTIMES/.lock"

echo
if [ "$failures" -eq 0 ]; then echo "all passed"; else echo "$failures failed"; exit 1; fi
