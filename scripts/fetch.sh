#!/usr/bin/env bash
# Clone or update upstream sources listed in sources.conf.
# Usage: scripts/fetch.sh [name...]   (default: all)

source "$(dirname "$0")/env.sh"

mkdir -p "$SRC"

fetch() {
    local name="$1" url="$2" ref="$3" dir="$SRC/$1"
    if [[ ! -d "$dir/.git" ]]; then
        log "cloning $name ($ref)"
        git clone --filter=blob:none --no-checkout "$url" "$dir"
    else
        log "updating $name ($ref)"
        git -C "$dir" fetch --tags origin
    fi
    git -C "$dir" checkout -q --detach "origin/$ref" 2>/dev/null || git -C "$dir" checkout -q --detach "$ref"
    git -C "$dir" submodule update --init --recursive --filter=blob:none -q
    log "$name at $(git -C "$dir" rev-parse --short HEAD) ($(git -C "$dir" log -1 --format=%cs))"

    local patches=("$ROOT/patches/$name"/*.patch)
    if [[ -e "${patches[0]}" ]]; then
        log "applying ${#patches[@]} patch(es) to $name"
        git -C "$dir" -c user.name=hadron -c user.email=dev@hadron.invalid am -q --3way "${patches[@]}"
    fi
}

want=("$@")
while read -r name url ref; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    if (( ${#want[@]} )) && [[ ! " ${want[*]} " == *" $name "* ]]; then continue; fi
    fetch "$name" "$url" "$ref"
done < "$ROOT/sources.conf"
