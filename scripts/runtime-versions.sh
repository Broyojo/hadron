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
# started after an update gets the copy of the new build. A copy is deleted once nothing uses it,
# and is never changed: what a launch notes about it is kept beside the copies, in .launches.
# On one APFS volume a copy is a clone: it takes no time and no space of its own.
#
# A checkout has no BUILD-ID and none of this applies to it: the link names the checkout itself.

# Real paths throughout: a game is told where it runs from by its real path.
_real=$(cd "$HADRON_SUPPORT" 2>/dev/null && pwd -P) && HADRON_SUPPORT="$_real"
HADRON_RUNTIMES="$HADRON_SUPPORT/runtimes"

# The build a runtime folder holds; nothing for a checkout, and nothing for a name that could not
# be a folder's. Every launch asks this, so it starts no other program.
runtime_id() {
    _line=
    [ -f "$1/BUILD-ID" ] && IFS= read -r _line < "$1/BUILD-ID"
    case "$_line" in
        ""|.*|*[!A-Za-z0-9._-]*) ;;
        *) printf '%s\n' "$_line" ;;
    esac
}

# The runtime folder of the installed app, as scripts/steam-install wrote it down.
runtime_source() {
    _line=
    [ -f "$HADRON_SUPPORT/source" ] && IFS= read -r _line < "$HADRON_SUPPORT/source"
    printf '%s\n' "$_line"
}

# One launch at a time chooses, copies, moves the link and deletes. The lock is the system's
# (flock, through lockf), on a file this shell holds open: it goes when its holder ends, however
# that happens. Nothing that outlives the locked part may be started while it is held, since a
# child inherits the open file.
runtime_lock() {
    mkdir -p "$HADRON_RUNTIMES" 2>/dev/null && exec 9>"$HADRON_RUNTIMES/.lock" || return 1
    lockf -s -t 30 9 || { exec 9>&-; return 1; }    # half a minute: something is wrong, go without
}

runtime_unlock() {
    exec 9>&-
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

# A launch says which copy it runs from as soon as it knows: a file named after the copy and
# the launch's process ID, which Steam's tool keeps for as long as its game runs, holding the
# prefix the launch is for. Called by scripts/steam-run with the copy's folder.
runtime_mark() {
    mkdir -p "$HADRON_RUNTIMES/.launches" 2>/dev/null &&
        printf '%s\n' "${WINEPREFIX:-}" > "$HADRON_RUNTIMES/.launches/${1##*/}.$$"
}

runtime_unmark() {
    rm -f "$HADRON_RUNTIMES/.launches/${1##*/}.$$"
}

# The copy another launch that is still alive runs $WINEPREFIX from, if there is one: two
# launches for one prefix have to be the same Wine, also before either has started it. The marks
# of launches that have ended are removed on the way, so that they do not pile up. Call with the
# lock held.
runtime_of_launch() {
    for _mark in "$HADRON_RUNTIMES/.launches"/*.*; do
        [ -f "$_mark" ] || continue
        _pid=${_mark##*.}
        [ "$_pid" != "$$" ] || continue
        kill -0 "$_pid" 2>/dev/null || { rm -f "$_mark"; continue; }
        _prefix=; IFS= read -r _prefix < "$_mark"
        [ -n "$_prefix" ] && [ "$_prefix" = "${WINEPREFIX:-}" ] || continue
        _name=${_mark##*/}
        printf '%s\n' "$HADRON_RUNTIMES/${_name%.*}"
        return 0
    done
}

# Whether the copy in $1 is in use: a launch that marked it is still alive, or a process has
# files of it mapped (the test of scripts/hadron-procs.sh, by the copy's real path; it also finds
# a game whose launch was killed). When the processes cannot be looked at, the answer is yes:
# a copy kept too long costs space, one deleted too soon costs a game.
runtime_in_use() {
    for _mark in "$HADRON_RUNTIMES/.launches/${1##*/}".*; do
        [ -f "$_mark" ] || continue
        kill -0 "${_mark##*.}" 2>/dev/null && return 0
        rm -f "$_mark"
    done
    _real=$(cd "$1" 2>/dev/null && pwd -P) || return 0
    _wine=$(ps -U "$(id -u)" -o pid=,command= 2>/dev/null) || return 0
    printf '%s\n' "$_wine" | grep -E 'C:\\windows|C:\\Program Files|Z:\\|wine' |
        while read -r _pid _rest; do
            if _files=$(lsof -a -p "$_pid" -d txt -Fn 2>/dev/null); then
                printf '%s\n' "$_files" | grep -qF "$_real/" && exit 3
            else
                # lsof says nothing of a process that has ended; one that is still there could
                # not be looked at.
                kill -0 "$_pid" 2>/dev/null && exit 3
            fi
        done
    [ $? -eq 3 ]
}

# Delete the copies nothing needs any more: every one but the folder in $1 that is not in use.
# Only folders in runtimes/ that carry a BUILD-ID are deleted, never through a link. Call with
# the lock held.
runtime_prune() {
    for _dir in "$HADRON_RUNTIMES"/*; do
        [ -d "$_dir" ] && [ ! -L "$_dir" ] && [ -f "$_dir/BUILD-ID" ] || continue
        [ "$_dir" = "$1" ] && continue
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
    _servers=$(pgrep -x wineserver) || return 0     # the usual case: no Wine session at all
    _server="server-$(stat -f '%Xd-%Xi' "$WINEPREFIX" | tr 'A-F' 'a-f')"
    for _pid in $_servers; do
        case "$(lsof -a -p "$_pid" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p')" in
            */"$_server")
                lsof -a -p "$_pid" -d txt -Fn 2>/dev/null | sed -n 's/^n//p' |
                    sed -n 's|/dist/bin/wineserver$||p' | head -1
                return 0 ;;
        esac
    done
}

# The copy a game starting now should run from, made if need be, with the link moved to it,
# for a launch that began in the runtime folder in $1. Call with the lock held. Nothing is printed when that folder is
# the one to run from.
#
# The build to run is the one in the installed app, whose runtime folder scripts/steam-install
# wrote down. A launch that began in a folder that is not a copy began in an app itself: the
# link still names it, as Hadron 0.2.0 and older set it up. That app is taken as the installed
# one from then on. An app that cannot be read changes nothing.
runtime_latest() {
    case "$1" in
        "$HADRON_RUNTIMES"/*) _source=$(runtime_source) ;;
        *) _source="$1" ;;
    esac
    _wanted=$(runtime_id "${_source:-/nonexistent}")
    [ -n "$_wanted" ] || return 0
    case "$1" in
        "$HADRON_RUNTIMES/$_wanted") return 0 ;;
    esac
    if _copy=$(runtime_copy "$_source") && runtime_point "$_copy"; then
        [ "$_source" = "$(runtime_source)" ] || printf '%s\n' "$_source" > "$HADRON_SUPPORT/source"
        printf '%s\n' "$_copy"
    fi
}

# Which copy the launch that began in the runtime folder in $1 runs from, printed when it is
# another one, which is then marked for this launch. In turn: the copy a live launch for the same
# prefix runs from, the copy a Wine session running in the prefix runs from, the copy of the
# build in the installed app. One launch decides at a time.
runtime_choose() {
    runtime_lock || return 0
    _chosen=$(runtime_of_launch)
    [ -n "$_chosen" ] || _chosen=$(runtime_of_session)
    [ -n "$_chosen" ] || _chosen=$(runtime_latest "$1")
    if [ -n "$_chosen" ] && [ "$_chosen" != "$1" ] && [ -x "$_chosen/scripts/steam-run" ]; then
        runtime_mark "$_chosen"
        runtime_unmark "$1"
        printf '%s\n' "$_chosen"
    fi
    runtime_unlock
}
