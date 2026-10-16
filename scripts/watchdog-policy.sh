# Shared policy, sourced by the watchdog and replayed by regression tests.
watchdog_runtime_mode() {  # <real runtime root> <real runtime store>
    case "$1" in "$2"/*) echo installed ;; *) echo checkout ;; esac
}

watchdog_limits() {  # <RAM in MB> <checkout|installed>
    mode=$2
    case "$mode" in checkout|installed) ;; *) echo "watchdog: invalid runtime mode" >&2; return 1 ;; esac
    for value in "${HADRON_MEM_LIMIT_GB:-1}" "${HADRON_SWAP_LIMIT_MB:-1}" "${HADRON_DISK_MIN_GB:-1}"; do
        case "$value" in ''|*[!0-9]*|0[0-9]*) echo "watchdog: limits must be whole nonnegative numbers without leading zeros" >&2; return 1 ;; esac
        [ "${#value}" -le 6 ] || { echo "watchdog: limit is too large" >&2; return 1; }
    done
    ram_mb=$1
    mem_warn_mb=$((ram_mb / 2))
    swap_warn_mb=$((ram_mb / 1024 * 256))
    [ "$swap_warn_mb" -ge 2048 ] || swap_warn_mb=2048
    [ "$swap_warn_mb" -le 4096 ] || swap_warn_mb=4096
    mem_hard=0 swap_hard=0 mem_limit_mb=0 swap_limit_mb=0
    default_disk_gb=1
    if [ "$mode" = checkout ]; then
        mem_hard=1 swap_hard=1
        mem_limit_mb=$mem_warn_mb
        swap_limit_mb=2048
        default_disk_gb=5
    fi
    if [ -n "${HADRON_MEM_LIMIT_GB:-}" ]; then
        mem_hard=1
        mem_limit_mb=$((HADRON_MEM_LIMIT_GB * 1024))
    fi
    if [ -n "${HADRON_SWAP_LIMIT_MB:-}" ]; then
        swap_hard=1
        swap_limit_mb=$HADRON_SWAP_LIMIT_MB
    fi
    disk_min_mb=$(( ${HADRON_DISK_MIN_GB:-$default_disk_gb} * 1024 ))
}

watchdog_decide() {  # uses footprint, pressure, swap_growth, disk; sets reason and warning
    reason= warning=0
    if [ "$swap_hard" -eq 1 ] && [ "$swap_growth" -gt "$swap_limit_mb" ]; then
        reason="swap grew by ${swap_growth} MB"
    fi
    if [ "$mem_hard" -eq 1 ] && [ "$footprint" -gt "$mem_limit_mb" ]; then
        reason="Wine memory footprint at ${footprint} MB"
    fi
    [ "$disk" -ge "$disk_min_mb" ] || reason="only ${disk} MB of disk left"
    [ "$pressure" -lt 4 ] || reason="critical memory pressure"
    if [ -z "$reason" ] && { [ "$footprint" -gt "$mem_warn_mb" ] || [ "$swap_growth" -gt "$swap_warn_mb" ] || [ "$pressure" -ge 2 ]; }; then
        warning=1
    fi
}
