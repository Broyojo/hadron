# Which copy of the runtime a game runs from. Sourced by the launch and setup scripts, after
# scripts/paths.sh.
#
# An installed Hadron.app carries the runtime, and games do not run from it: replacing the app,
# which is what every update does, would change the files under a running game. Each build of
# the runtime (its BUILD-ID, from scripts/package-runtime.sh) is copied once to
#
#   ~/Library/Application Support/Hadron/runtimes/<build id>/
#
# and ~/Library/Application Support/Hadron/runtime, which Steam's tool starts, is a link to the
# copy new games use. A running game keeps the copy it started from, by its real path; a game
# started after an update gets the copy of the new build. A copy is deleted once nothing uses it.
# On one APFS volume a copy is a clone: it takes no time and no space of its own.
#
# A checkout has no BUILD-ID and none of this applies to it: the link names the checkout itself.

# Real paths throughout: a game is told where it runs from by its real path.
_real=$(cd "$HADRON_SUPPORT" 2>/dev/null && pwd -P) && HADRON_SUPPORT="$_real"
HADRON_RUNTIMES="$HADRON_SUPPORT/runtimes"

# The build a runtime folder holds; nothing for a checkout.
runtime_id() {
    sed -n 1p "$1/BUILD-ID" 2>/dev/null | tr -cd 'A-Za-z0-9._-'
}

# One launch at a time makes copies, moves the link and deletes: a folder as the lock, with the
# holder's process ID in it, so that a holder that died does not block the next launch.
runtime_lock() {
    mkdir -p "$HADRON_RUNTIMES" || return 1
    _tries=0
    until mkdir "$HADRON_RUNTIMES/.lock" 2>/dev/null; do
        _holder=$(cat "$HADRON_RUNTIMES/.lock/pid" 2>/dev/null)
        if [ -n "$_holder" ] && ! kill -0 "$_holder" 2>/dev/null; then
            rm -rf "$HADRON_RUNTIMES/.lock"
            continue
        fi
        _tries=$((_tries + 1))
        [ "$_tries" -lt 150 ] || return 1    # half a minute: something is wrong, go without
        sleep 0.2
    done
    echo $$ > "$HADRON_RUNTIMES/.lock/pid"
}

runtime_unlock() {
    rm -rf "$HADRON_RUNTIMES/.lock"
}

# Make the copy of the runtime in $1 if there is none yet, and print the copy's folder. The copy
# gets its name only when it is complete. Call with the lock held.
runtime_copy() {
    _id=$(runtime_id "$1")
    [ -n "$_id" ] || return 1
    _dest="$HADRON_RUNTIMES/$_id"
    if [ ! -f "$_dest/BUILD-ID" ]; then
        [ ! -e "$_dest" ] || return 1         # something else by that name: not ours to replace
        _new="$HADRON_RUNTIMES/.new.$$"
        rm -rf "$_new"
        cp -c -R "$1" "$_new" 2>/dev/null || { rm -rf "$_new"; cp -R "$1" "$_new" 2>/dev/null; } ||
            { rm -rf "$_new"; return 1; }
        [ "$(runtime_id "$_new")" = "$_id" ] && mv "$_new" "$_dest" || { rm -rf "$_new"; return 1; }
    fi
    printf '%s\n' "$_dest"
}

# Point the link Steam's tool starts at the folder in $1. The link is made beside the old one
# and renamed over it, so there is never a moment without one. Only ever a link is replaced.
runtime_point() {
    _link="$HADRON_SUPPORT/runtime"
    if [ -e "$_link" ] && [ ! -L "$_link" ]; then
        echo "$_link is not a link; leaving it" >&2
        return 1
    fi
    [ "$(readlink "$_link" 2>/dev/null)" = "$1" ] && return 0
    ln -sfn "$1" "$_link.new.$$" && mv -fh "$_link.new.$$" "$_link"
}

# Whether a process runs from the runtime folder in $1 (its real path): the same test as
# scripts/hadron-procs.sh, for any copy.
runtime_in_use() {
    _real=$(cd "$1" 2>/dev/null && pwd -P) || return 1
    ps -Ao pid=,command= | grep -E 'C:\\windows|C:\\Program Files|Z:\\|wine' | grep -v grep |
        while read -r _pid _rest; do
            lsof -a -p "$_pid" -d txt -Fn 2>/dev/null | grep -qF "$_real/" && exit 3
        done
    [ $? -eq 3 ]
}

# Delete the copies nothing needs any more: every one but the folder in $1, that no process runs
# from and no launch chose in the last ten minutes (a launch marks its copy before it starts
# Wine). Only folders in runtimes/ that carry a BUILD-ID are deleted, never through a link. Call
# with the lock held.
runtime_prune() {
    for _dir in "$HADRON_RUNTIMES"/*; do
        [ -d "$_dir" ] && [ ! -L "$_dir" ] && [ -f "$_dir/BUILD-ID" ] || continue
        [ "$_dir" = "$1" ] && continue
        [ -n "$(find "$_dir/.used" -mmin -10 2>/dev/null)" ] && continue
        runtime_in_use "$_dir" && continue
        rm -rf "$_dir"
    done
    # What a launch that died left half copied.
    for _dir in "$HADRON_RUNTIMES"/.new.*; do
        [ -d "$_dir" ] || continue
        kill -0 "${_dir##*.new.}" 2>/dev/null || rm -rf "$_dir"
    done
}

# The runtime folder a Wine session that is running in $WINEPREFIX runs from, if there is one.
# A second program started in a prefix joins its session, and has to be the same Wine. The
# session's wineserver works in a folder named after the prefix's device and inode.
runtime_of_session() {
    [ -d "$WINEPREFIX" ] || return 0
    _server="server-$(stat -f '%Xd-%Xi' "$WINEPREFIX" | tr 'A-F' 'a-f')"
    for _pid in $(pgrep -x wineserver); do
        case "$(lsof -a -p "$_pid" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p')" in
            */"$_server")
                lsof -a -p "$_pid" -d txt -Fn 2>/dev/null | sed -n 's/^n//p' |
                    sed -n 's|/dist/bin/wineserver$||p' | head -1
                return 0 ;;
        esac
    done
}

# The copy a game starting now should run from, made if need be, with the link moved to it,
# for a launch that began in the runtime folder in $1. Nothing is printed when that folder is
# the one to run from.
#
# The build to run is the one in the installed app, whose runtime folder scripts/steam-install
# wrote down. A launch that began in a folder that is not a copy began in an app itself: the
# link still names it, as Hadron 0.2.0 and older set it up. That app is taken as the installed
# one from then on. An app that cannot be read changes nothing.
runtime_latest() {
    case "$1" in
        "$HADRON_RUNTIMES"/*) _source=$(sed -n 1p "$HADRON_SUPPORT/source" 2>/dev/null) ;;
        *) _source="$1" ;;
    esac
    _wanted=$(runtime_id "${_source:-/nonexistent}")
    [ -n "$_wanted" ] || return 0
    case "$1" in
        "$HADRON_RUNTIMES/$_wanted") return 0 ;;
    esac
    runtime_lock || return 0
    if _copy=$(runtime_copy "$_source") && runtime_point "$_copy"; then
        [ "$_source" = "$(sed -n 1p "$HADRON_SUPPORT/source" 2>/dev/null)" ] || printf '%s\n' "$_source" > "$HADRON_SUPPORT/source"
        touch "$_copy/.used"
        printf '%s\n' "$_copy"
    fi
    runtime_unlock
}
