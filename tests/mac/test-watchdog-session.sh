#!/bin/sh
# Signal only disposable processes created by this test, never real Wine sessions.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/scripts/watchdog-session.sh"
probe=$(mktemp -d)
state="$probe/session.paused"
WINEPREFIX="$probe/prefix with spaces"
# SIP-protected /bin/sleep does not expose its environment through ps on macOS.
printf '#include <unistd.h>\nint main(void) { sleep(120); return 0; }\n' > "$probe/sleeper.c"
cc "$probe/sleeper.c" -o "$probe/sleeper"
env WINEPREFIX="$WINEPREFIX" "$probe/sleeper" &
first=$!
env WINEPREFIX="$WINEPREFIX-other" "$probe/sleeper" &
other=$!
cleanup() { kill -CONT "$first" "$other" 2>/dev/null || :; kill "$first" "$other" 2>/dev/null || :; rm -rf "$probe"; }
trap cleanup EXIT HUP INT TERM
sleep 1
watchdog_same_prefix "$first"
if watchdog_same_prefix "$other"; then echo 'FAIL: another prefix matched'; exit 1; fi
ours="$first:$(watchdog_start "$first") $other:$(watchdog_start "$other")"
: > "$state"
watchdog_suspend
case $(ps -p "$first" -o state=) in *T*) ;; *) echo 'FAIL: process not suspended'; exit 1 ;; esac
case $(ps -p "$other" -o state=) in *T*) echo 'FAIL: other prefix suspended'; exit 1 ;; esac
watchdog_has_paused
watchdog_suspend
[ "$(wc -l < "$state" | tr -d ' ')" = 1 ]
watchdog_continue
[ ! -f "$state" ]
case $(ps -p "$first" -o state=) in *T*) echo 'FAIL: process not resumed'; exit 1 ;; esac
# A stale PID/start pair must not suspend a replacement process.
ours="$first:not-the-start-time"
: > "$state"
watchdog_suspend
[ ! -s "$state" ]
if watchdog_has_paused; then echo 'FAIL: empty record considered live'; exit 1; fi
printf '%s %s\n' "$first" wrong-start > "$state"
if watchdog_has_paused; then echo 'FAIL: reused PID considered live'; exit 1; fi
echo 'watchdog session: suspend/resume, prefix isolation, repeat calls and stale identities passed'
