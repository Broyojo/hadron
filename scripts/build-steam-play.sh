#!/usr/bin/env bash
# Build the Steam Play integration: NotProton's Steam client library (GPLv3, src/notproton
# with patches/notproton) and the Valve Windows client files the Steam bridge stages.
#
#   build/notproton/notproton.dylib   injected into Mac Steam by scripts/steam-install
#   build/valve/bridge/               Valve's steamclient DLLs and legacycompat tools, fetched
#                                     from Valve's CDN and hash-checked (never redistributed)
source "$(dirname "$0")/env.sh"

NP="$SRC/notproton"
[[ -d "$NP/dylib" ]] || die "missing $NP, run scripts/fetch.sh notproton"

# Dobby, NotProton's hooking library, at the commit NotProton pins (vendor/README.md).
DOBBY_COMMIT=5dfc8546954ce3b3198132ab13fddb89ee92cdd7
if [[ ! -d "$NP/vendor/dobby/.git" ]]; then
    log "fetching Dobby"
    git clone -q https://github.com/jmpews/Dobby.git "$NP/vendor/dobby"
fi
git -C "$NP/vendor/dobby" checkout -q "$DOBBY_COMMIT"
# Rebuilt whenever the built revision isn't the pinned one, not only when the archive is missing.
if [[ ! -f "$NP/build/dobby/libdobby.a" || "$(cat "$NP/build/dobby/.hadron-revision" 2>/dev/null)" != "$DOBBY_COMMIT" ]]; then
    log "building Dobby"
    rm -rf "$NP/build/dobby"
    make -C "$NP" dobby >/dev/null
    echo "$DOBBY_COMMIT" > "$NP/build/dobby/.hadron-revision"
fi

log "building the Steam client library"
# NotProton's Makefile expects Apple's clang, not Homebrew LLVM's.
PATH="/usr/bin:$PATH" make -C "$NP" out/notproton.dylib >/dev/null
mkdir -p "$BUILD/notproton"
cp "$NP/out/notproton.dylib" "$BUILD/notproton/notproton.dylib"

log "fetching Valve's Windows client files"
BRIDGE_DIR="$BUILD/valve/bridge" WORK="$BUILD/valve/fetch" sh "$NP/bridge/fetch-valve.sh" --install >/dev/null

log "done: $BUILD/notproton/notproton.dylib, $BUILD/valve/bridge"
