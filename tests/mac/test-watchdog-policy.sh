#!/bin/sh
# Replay the observed Raft loads and the independent emergency guards without starting a game.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/scripts/watchdog-policy.sh"
unset HADRON_MEM_LIMIT_GB HADRON_SWAP_LIMIT_MB HADRON_DISK_MIN_GB
watchdog_limits 16384 1
disk=100000
check() {  # <footprint> <pressure> <swap growth> <stop: 0|1> <warning: 0|1>
    footprint=$1 pressure=$2 swap_growth=$3
    watchdog_decide
    stopped=0
    [ -z "$reason" ] || stopped=1
    [ "$stopped" = "$4" ] && [ "$warning" = "$5" ] || {
        echo "FAIL: footprint=$1 pressure=$2 swap=$3 reason=$reason warning=$warning" >&2
        exit 1
    }
}
check 4400 1 0 0 0
# Actual prior stop and completed world-load measurements must warn and continue.
check 8206 2 996 0 1
check 10878 2 2021 0 1
check 15000 2 0 1 0
check 15000 1 0 0 1
check 4400 2 8193 1 0
check 4400 4 0 1 0
disk=100
check 4400 1 0 1 0
disk=100000
# Explicit limits work even at normal system pressure.
HADRON_MEM_LIMIT_GB=8
watchdog_limits 16384 1
check 8206 1 0 1 0
unset HADRON_MEM_LIMIT_GB
HADRON_SWAP_LIMIT_MB=2048
watchdog_limits 16384 1
check 4400 1 2049 1 0
unset HADRON_SWAP_LIMIT_MB
# An old runtime without physical-footprint support keeps its old hard guards.
watchdog_limits 16384 0
check 8206 1 0 1 0
check 4400 1 2049 1 0
for bad in 08 -1 1.5 abc 9999999; do
    if (HADRON_MEM_LIMIT_GB=$bad; watchdog_limits 16384 1) >/dev/null 2>&1; then
        echo "FAIL: invalid limit accepted: $bad" >&2; exit 1
    fi
done
echo 'watchdog policy: all checks passed'
