#!/bin/sh
# Checks a game's log as scripts/play writes it: standard output appended to the log as a file,
# standard error through scripts/logcap. Every line is kept, each stream's in the order it was
# written, and past HADRON_LOG_MAX_MB the log keeps its start and its end.
#
# Usage: tests/mac/test-logcap.sh
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
failures=0
ok() { echo "ok    $1"; }
fail() { echo "FAIL  $1"; failures=$((failures + 1)); }

# What scripts/play does for one launch, with <lines> lines alternating between the two.
launch() {  # <log> <lines>
    : > "$1"
    mkfifo "$1.fifo"
    "$REPO/scripts/logcap" "$1" < "$1.fifo" &
    (
        exec >> "$1" 2> "$1.fifo"
        i=1
        while [ "$i" -le "$2" ]; do
            if [ $((i % 2)) = 1 ]; then echo "stdout $i"; else echo "stderr $i padding the line out to a hundred bytes or so, as a debug line would be" >&2; fi
            i=$((i + 1))
        done
        # Is standard output a file? (A pipe stopped Geometry Dash with Geode, findings #53.)
        [ -f /dev/stdout ] || [ ! -p /dev/stdout ] && echo "stdout is not a pipe"
    )
    wait
    rm -f "$1.fifo"
}

launch "$T/small.log" 200
if [ "$(grep '^stdout [0-9]' "$T/small.log" | awk '{print $2}' | tr '\n' ' ')" = "$(seq 1 2 199 | tr '\n' ' ')" ] &&
   [ "$(grep '^stderr [0-9]' "$T/small.log" | awk '{print $2}' | tr '\n' ' ')" = "$(seq 2 2 200 | tr '\n' ' ')" ]; then
    ok "below the limit, every line is there, each stream's in order"
else
    fail "below the limit, every line is there, each stream's in order"
fi
if grep -q "stdout is not a pipe" "$T/small.log"; then ok "standard output is not a pipe"; else fail "standard output is not a pipe"; fi

HADRON_LOG_MAX_MB=1 launch "$T/big.log" 40000
size=$(wc -c < "$T/big.log" | tr -d ' ')
if grep -q "MB of it are left out here" "$T/big.log" && grep -q "^stderr 40000 " "$T/big.log" &&
   grep -q "^stderr 2 " "$T/big.log"; then
    ok "past the limit, the start and the end are kept ($size bytes)"
else
    fail "past the limit, the start and the end are kept ($size bytes)"
fi
if [ ! -e "$T/big.log.tail" ] && [ ! -e "$T/big.log.tail.1" ]; then ok "the tail files are gone"
else fail "the tail files are gone"; fi

[ "$failures" -eq 0 ] && echo "all passed" || { echo "$failures failed"; exit 1; }
