# Shared memory policy; sourced by the watchdog and replayed by its regression tests.
watchdog_limits() {  # <RAM in MB> <footprint helper available: 0|1>
    for value in "${HADRON_MEM_LIMIT_GB:-1}" "${HADRON_SWAP_LIMIT_MB:-1}" "${HADRON_DISK_MIN_GB:-5}"; do
        case "$value" in ''|*[!0-9]*|0[0-9]*) echo "watchdog: limits must be whole nonnegative numbers without leading zeros" >&2; return 1 ;; esac
        [ "${#value}" -le 6 ] || { echo "watchdog: limit is too large" >&2; return 1; }
    done
    ram_mb=$1
    has_footprint=$2
    ram_gb=$((ram_mb / 1024))
    mem_warn_mb=$((ram_mb / 2))
    mem_limit_mb=$((ram_mb * 9 / 10))
    [ -n "${HADRON_MEM_LIMIT_GB:-}" ] && mem_limit_mb=$((HADRON_MEM_LIMIT_GB * 1024))
    swap_warn_mb=$((ram_gb * 256))
    [ "$swap_warn_mb" -lt 2048 ] && swap_warn_mb=2048
    [ "$swap_warn_mb" -gt 4096 ] && swap_warn_mb=4096
    default_swap_limit_mb=$((ram_mb / 2))
    [ "$default_swap_limit_mb" -gt 8192 ] && default_swap_limit_mb=8192
    # Keep the original conservative limit when an older checkout lacks the footprint helper.
    if [ "$has_footprint" != 1 ]; then
        default_swap_limit_mb=2048
        [ -n "${HADRON_MEM_LIMIT_GB:-}" ] || mem_limit_mb=$mem_warn_mb
    fi
    swap_limit_mb=${HADRON_SWAP_LIMIT_MB:-$default_swap_limit_mb}
    disk_min_mb=$(( ${HADRON_DISK_MIN_GB:-5} * 1024 ))
    mem_hard=0 swap_hard=0
    [ -z "${HADRON_MEM_LIMIT_GB:-}" ] || mem_hard=1
    [ -z "${HADRON_SWAP_LIMIT_MB:-}" ] || swap_hard=1
    [ "$has_footprint" = 1 ] || { mem_hard=1; swap_hard=1; }
}

watchdog_decide() {  # uses footprint, pressure, swap_growth, disk; sets reason and warning
    reason= warning=0
    if [ "$pressure" -ge 2 ] || [ "$swap_hard" -eq 1 ]; then
        [ "$swap_growth" -le "$swap_limit_mb" ] || reason="swap grew by ${swap_growth} MB"
    fi
    if [ "$pressure" -ge 2 ] || [ "$mem_hard" -eq 1 ]; then
        [ "$footprint" -le "$mem_limit_mb" ] || reason="Wine memory footprint at ${footprint} MB"
    fi
    [ "$disk" -ge "$disk_min_mb" ] || reason="only ${disk} MB of disk left"
    [ "$pressure" -lt 4 ] || reason="critical memory pressure"
    if [ -z "$reason" ] && { [ "$footprint" -gt "$mem_warn_mb" ] || [ "$swap_growth" -gt "$swap_warn_mb" ] || [ "$pressure" -ge 2 ]; }; then
        warning=1
    fi
}
