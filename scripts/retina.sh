#!/bin/sh
# Retina mode for a prefix (findings #45). Sourced by every script that starts Wine in a prefix,
# scripts/play and scripts/wine-run, which call retina_decide right before Wine starts.
#
# Retina mode has to be the same in every process of a prefix. It is always passed on, 0 or 1, so
# that a RetinaMode left in a prefix's registry (CrossOver's "High Resolution") does not turn it on
# unasked. A launch that finds a Wine session running in the prefix takes the session's setting.
# A launch that asks for the other setting (HADRON_RETINA set) first waits a little for that session
# to end: Steam runs a helper in the prefix just before the game, without the game's launch options,
# and its session can still be closing when the game starts. One launch decides at a time: the first records its setting
# and starts the prefix's wineserver before letting the next look, so two launches into an idle
# prefix do not both decide.
#
# Uses PREFIX and WINESERVER; sets HADRON_RETINA.

retina_session_running() {
    _servers=$(pgrep -x wineserver) || return 1
    _session="server-$(stat -f '%Xd-%Xi' "$PREFIX" 2>/dev/null | tr 'A-F' 'a-f')"
    # A wineserver works in its server folder, and for its first moments in the prefix itself.
    _prefix_real=$(/bin/realpath "$PREFIX" 2>/dev/null)
    for _server in $_servers; do
        case "$(lsof -a -p "$_server" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p')" in
            */"$_session"|"$_prefix_real") return 0 ;;
        esac
    done
    return 1
}

retina_decide() {
    case "${HADRON_RETINA:-0}" in [1yYtT]*) _requested=1 ;; *) _requested=0 ;; esac
    mkdir -p "$PREFIX"
    exec 6>"$PREFIX/.hadron-retina.lock"
    if ! lockf -s -t 60 6; then
        echo "another launch has held $PREFIX/.hadron-retina.lock for a minute; not starting" >&2
        exit 1
    fi
    if [ -n "${HADRON_RETINA:-}" ] && retina_session_running &&
        [ "$(cat "$PREFIX/.hadron-retina" 2>/dev/null)" != "$_requested" ]; then
        _wait=15
        while [ "$_wait" -gt 0 ] && retina_session_running; do sleep 1; _wait=$((_wait - 1)); done
    fi
    if retina_session_running; then
        _joined=$(cat "$PREFIX/.hadron-retina" 2>/dev/null)
        HADRON_RETINA="${_joined:-0}"
        [ "$HADRON_RETINA" = "$_requested" ] ||
            echo "HADRON_RETINA=$HADRON_RETINA: the Wine session already running in this prefix has it" >&2
    else
        HADRON_RETINA=$_requested
        printf '%s\n' "$HADRON_RETINA" > "$PREFIX/.hadron-retina"
        # No launcher lock goes to the wineserver, which outlives the launcher: this one's (6), nor
        # one its caller holds (steam-run's prefix lock is 7, the runtime lock 9).
        WINEPREFIX="$PREFIX" "$WINESERVER" 6>&- 7>&- 8>&- 9>&-
    fi
    exec 6>&-
}
