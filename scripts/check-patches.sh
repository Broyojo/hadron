#!/usr/bin/env bash
# Verify each patch queue applies cleanly to its pinned upstream base, in a throwaway worktree.
# A submodule's queue (patches/fex/External/rpmalloc/) is checked against the submodule.
source "$(dirname "$0")/env.sh"
status=0
# check NAME QUEUE-DIR REPO: QUEUE-DIR's patches are the last commits of REPO
check() {
    local name=$1 queue=$2 src=$3 n base wt
    n=$(ls "$queue"/*.patch 2>/dev/null | wc -l | tr -d ' ')
    base=$(git -C "$src" rev-parse "HEAD~$n")
    wt="$BUILD/patch-check/$(tr / - <<<"$name")"
    rm -rf "$wt"; git -C "$src" worktree prune
    git -C "$src" worktree add -q --detach "$wt" "$base"
    if git -C "$wt" -c user.name=hadron -c user.email=dev@hadron.invalid am -q --3way "$queue"/*.patch; then
        if git -C "$wt" diff --quiet HEAD "$(git -C "$src" rev-parse HEAD)" -- .; then log "$name: $n patches apply cleanly and match src/$name"
        else log "$name: $n patches apply, but differ from src/$name (uncommitted or unexported changes?)"; status=1; fi
    else
        log "$name: patches FAIL to apply on $base"; git -C "$wt" am --abort; status=1
    fi
    git -C "$src" worktree remove --force "$wt"
}
for dir in "$ROOT"/patches/*/; do
    name=$(basename "$dir"); src="$SRC/$name"
    [[ -d "$src/.git" ]] || { log "skip $name (not fetched)"; continue; }
    check "$name" "${dir%/}" "$src"
    while IFS= read -r queue; do
        path=${queue#"$dir"}
        check "$name/$path" "$queue" "$src/$path"
    done < <(find "$dir" -mindepth 2 -name '*.patch' -exec dirname {} \; | sort -u)
done
exit $status
