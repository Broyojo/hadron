# Session suspension helpers. ROOT, WINEPREFIX and state are supplied by watchdog.
watchdog_start() {
    ps -p "$1" -o lstart= | tr -d ' :\n'
}
watchdog_same_prefix() {
    case " $(ps eww -p "$1" -o command=) " in
        *" WINEPREFIX=$WINEPREFIX "*) return 0 ;;
    esac
    return 1
}
watchdog_has_paused() {
    [ -f "$state" ] || return 1
    while read -r saved_pid saved_start; do
        [ -n "$saved_start" ] || continue
        [ "$(watchdog_start "$saved_pid")" != "$saved_start" ] || return 0
    done < "$state"
    return 1
}
watchdog_suspend() {
    for key in $ours; do
        target=${key%%:*}
        # Revalidate immediately before signalling: a cached PID may have been reused.
        [ "$(watchdog_start "$target")" = "${key#*:}" ] || continue
        watchdog_same_prefix "$target" || continue
        grep -qxF "$target ${key#*:}" "$state" && continue
        # Record first so interruption cannot lose our only recovery information.
        printf '%s %s\n' "$target" "${key#*:}" >> "$state"
        kill -STOP "$target" || echo "watchdog: could not suspend $target"
    done
}
watchdog_continue() {
    while read -r target started; do
        [ -n "$started" ] || continue
        [ "$(watchdog_start "$target")" = "$started" ] || continue
        watchdog_same_prefix "$target" || continue
        kill -CONT "$target" || echo "watchdog: could not resume $target"
    done < "$state"
    rm -f "$state" "$state.guard" "$state.resume"
    pause_shown=0
}
