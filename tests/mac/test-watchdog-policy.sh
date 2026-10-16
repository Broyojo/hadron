#!/bin/sh
# Replay runtime policies without starting a game or changing machine settings.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/scripts/watchdog-policy.sh"
unset HADRON_MEM_LIMIT_GB HADRON_SWAP_LIMIT_MB HADRON_DISK_MIN_GB
checks=0
disk=100000
check() {  # <footprint> <pressure> <swap growth> <stop: 0|1> <warning: 0|1>
    footprint=$1 pressure=$2 swap_growth=$3
    watchdog_decide
    stopped=0
    [ -z "$reason" ] || stopped=1
    [ "$stopped" = "$4" ] && [ "$warning" = "$5" ] || {
        echo "FAIL: mode=$mode footprint=$1 pressure=$2 swap=$3 disk=$disk reason=$reason warning=$warning" >&2
        exit 1
    }
    checks=$((checks + 1))
}
[ "$(watchdog_runtime_mode '/support/Hadron/runtimes/build-id' '/support/Hadron/runtimes')" = installed ]
[ "$(watchdog_runtime_mode '/support/Hadron/runtimes-copy/build-id' '/support/Hadron/runtimes')" = checkout ]
[ "$(watchdog_runtime_mode '/support/Hadron/runtimes' '/support/Hadron/runtimes')" = checkout ]
[ "$(watchdog_runtime_mode '/source/Hadron' '/support/Hadron/runtimes')" = checkout ]
[ "$(watchdog_runtime_mode '/Application Support/Hadron/runtimes/build' '/Application Support/Hadron/runtimes')" = installed ]

watchdog_limits 16384 installed
check 4400 1 0 0 0
# Recorded progressing loads warn and continue in installed runtimes.
check 8206 2 996 0 1
check 10878 2 2021 0 1
check 20000 2 20000 0 1
check 15000 1 0 0 1
check 4400 1 4097 0 1
check 4400 4 0 1 0
disk=5000
check 4400 1 0 0 0
disk=1024
check 4400 1 0 0 0
disk=1023
check 4400 1 0 1 0

watchdog_limits 16384 checkout
disk=100000
check 4400 1 0 0 0
check 8192 1 2048 0 0
check 8206 1 0 1 0
check 4400 1 2049 1 0
check 4400 2 0 0 1
check 4400 4 0 1 0
disk=5120
check 4400 1 0 0 0
disk=5119
check 4400 1 0 1 0

for runtime_mode in installed checkout; do
    disk=100000
    HADRON_MEM_LIMIT_GB=8
    watchdog_limits 16384 "$runtime_mode"
    check 8206 1 0 1 0
    HADRON_MEM_LIMIT_GB=32
    watchdog_limits 16384 "$runtime_mode"
    check 10878 1 0 0 1
    HADRON_MEM_LIMIT_GB=0
    watchdog_limits 16384 "$runtime_mode"
    check 1 1 0 1 0
    unset HADRON_MEM_LIMIT_GB
    HADRON_SWAP_LIMIT_MB=2048
    watchdog_limits 16384 "$runtime_mode"
    check 4400 1 2049 1 0
    HADRON_SWAP_LIMIT_MB=0
    watchdog_limits 16384 "$runtime_mode"
    check 4400 1 1 1 0
    unset HADRON_SWAP_LIMIT_MB
    HADRON_DISK_MIN_GB=5
    watchdog_limits 16384 "$runtime_mode"
    disk=4000
    check 4400 1 0 1 0
    unset HADRON_DISK_MIN_GB
    for variable in HADRON_MEM_LIMIT_GB HADRON_SWAP_LIMIT_MB HADRON_DISK_MIN_GB; do
        for bad in 08 -1 1.5 abc 9999999; do
            if (export "$variable=$bad"; watchdog_limits 16384 "$runtime_mode") >/dev/null 2>&1; then
                echo "FAIL: invalid $variable accepted: $bad" >&2
                exit 1
            fi
        done
    done
done
if watchdog_limits 16384 invalid >/dev/null 2>&1; then
    echo 'FAIL: invalid runtime mode accepted' >&2
    exit 1
fi
echo "watchdog policy: $checks decisions and runtime/invalid-input checks passed"
