#!/usr/bin/env bash
# Verify each patch queue applies cleanly to its pinned upstream base, in a throwaway worktree.
source "$(dirname "$0")/env.sh"
status=0
for dir in "$ROOT"/patches/*/; do
    name=$(basename "$dir"); src="$SRC/$name"
    [[ -d "$src/.git" ]] || { log "skip $name (not fetched)"; continue; }
    n=$(ls "$dir"*.patch 2>/dev/null | wc -l | tr -d ' ')
    base=$(git -C "$src" rev-parse "HEAD~$n")
    wt="$BUILD/patch-check/$name"
    rm -rf "$wt"; git -C "$src" worktree prune
    git -C "$src" worktree add -q --detach "$wt" "$base"
    if git -C "$wt" -c user.name=hadron -c user.email=dev@hadron.invalid am -q --3way "$dir"*.patch; then
        if git -C "$wt" diff --quiet HEAD "$(git -C "$src" rev-parse HEAD)" -- .; then log "$name: $n patches apply cleanly and match src/$name"
        else log "$name: $n patches apply, but differ from src/$name (uncommitted or unexported changes?)"; status=1; fi
    else
        log "$name: patches FAIL to apply on $base"; git -C "$wt" am --abort; status=1
    fi
    git -C "$src" worktree remove --force "$wt"
done
exit $status
