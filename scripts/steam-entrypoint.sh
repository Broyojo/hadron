# Resolve an explicitly selected entry point without redirecting Steam's helpers.
steam_entrypoint() {  # <verb> <install root> <original program> <optional filename> [original relative entry]
    if [ "$1" != waitforexitandrun ] || [ -z "$4" ]; then
        printf '%s\n' "$3"
        return 0
    fi
    if [ -n "${5:-}" ]; then
        case "/$5/" in
            *"/../"*|*"/./"*|*"//"*|*\\*) echo 'steam-run: HADRON_LAUNCHER_EXE must be a relative path without dot components' >&2; return 1 ;;
        esac
        if [ "$3" != "$2/$5" ]; then printf '%s\n' "$3"; return 0; fi
    elif [ "$(dirname "$3")" != "$2" ]; then
        printf '%s\n' "$3"
        return 0
    fi
    case "$4" in
        */*|*\\*|.|..) echo 'steam-run: HADRON_GAME_EXE must be a filename in the install folder' >&2; return 1 ;;
    esac
    if [ ! -f "$2/$4" ] || [ -L "$2/$4" ]; then
        echo "steam-run: alternate executable missing or a symlink: $2/$4" >&2
        return 1
    fi
    printf '%s\n' "$2/$4"
}
