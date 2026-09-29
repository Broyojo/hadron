#!/usr/bin/env bash
# Install build dependencies and the llvm-mingw toolchain (needed for FEX's
# ARM64EC/WoW64 DLLs, which require a mingw C++ runtime Homebrew LLVM lacks).

source "$(dirname "$0")/env.sh"

LLVM_MINGW_VERSION="${LLVM_MINGW_VERSION:-20260922}"

log "installing Homebrew dependencies"
brew install llvm lld bison meson ninja cmake pkg-config freetype gnutls molten-vk vulkan-headers

TOOLCHAINS="$ROOT/toolchains"
dest="$TOOLCHAINS/llvm-mingw"
if [[ ! -x "$dest/bin/arm64ec-w64-mingw32-clang" ]]; then
    name="llvm-mingw-$LLVM_MINGW_VERSION-ucrt-macos-universal"
    log "downloading $name"
    mkdir -p "$TOOLCHAINS"
    curl -fsSL \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VERSION/$name.tar.xz" \
        | tar -xJ -C "$TOOLCHAINS"
    rm -rf "$dest"
    mv "$TOOLCHAINS/$name" "$dest"
    # Downloaded binaries carry the quarantine attribute; clear it so they run.
    xattr -dr com.apple.quarantine "$dest" 2>/dev/null || true
fi
log "llvm-mingw: $("$dest/bin/arm64ec-w64-mingw32-clang" --version | head -1)"
