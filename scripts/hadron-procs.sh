# Which processes are Hadron's: sourced by scripts/stop and scripts/watchdog, with ROOT set.
#
# A process is Hadron's when it has files from this checkout mapped, which leaves the native
# Mac Steam client and other Wine installations (CrossOver etc.) alone.

# Wine-looking processes, one "pid rss_kb start" line each. start is the process's start time
# (without spaces or colons), which tells a reused PID from the process that had it before.
hadron_candidates() {
    ps -Ao pid=,rss=,lstart=,command= | grep -E 'C:\\windows|C:\\Program Files|Z:\\|wine' | grep -v grep \
        | awk '{ start = $3 $4 $5 $6 $7; gsub(":", "", start); print $1, $2, start }'
}

# This checkout's real path: lsof reports mapped files by it, and ROOT may be reached through a
# symlink (Steam launches go through ~/Library/Application Support/Hadron/runtime).
HADRON_REAL_ROOT="$(cd "$ROOT" && pwd -P)"

# Whether process $1 belongs to this checkout.
is_hadron_process() {
    lsof -a -p "$1" -d txt -Fn 2>/dev/null | grep -qF "$HADRON_REAL_ROOT/"
}
