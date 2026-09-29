#!/usr/bin/env bash
# Build the static LLVM 15 libraries DXMT's shader compiler (airconv) links against.
# DXMT requires exactly LLVM 15. Only the core libraries are built (no targets, no tools).

source "$(dirname "$0")/env.sh"

LLVM_TAG=llvmorg-15.0.7
LLVM_SRC="$SRC/llvm-project-15"
LLVM_PREFIX="$ROOT/toolchains/llvm15"

if [[ -f "$LLVM_PREFIX/lib/libLLVMCore.a" ]]; then
    log "LLVM 15 already installed at $LLVM_PREFIX"
    exit 0
fi

if [[ ! -d "$LLVM_SRC" ]]; then
    log "cloning $LLVM_TAG"
    git clone -q --depth 1 --branch "$LLVM_TAG" https://github.com/llvm/llvm-project.git "$LLVM_SRC"
fi

log "building LLVM 15 (arm64, static)"
cmake -S "$LLVM_SRC/llvm" -B "$BUILD/llvm15" -G Ninja \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_INSTALL_PREFIX="$LLVM_PREFIX" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DLLVM_HOST_TRIPLE=arm64-apple-darwin \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-D_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23" \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_ENABLE_ZSTD=Off \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_BUILD_TOOLS=Off \
    -DLLVM_INCLUDE_BENCHMARKS=Off \
    -DLLVM_INCLUDE_TESTS=Off \
    -DLLVM_INCLUDE_EXAMPLES=Off \
    -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off >/dev/null
cmake --build "$BUILD/llvm15"
cmake --install "$BUILD/llvm15" >/dev/null

# Source and build tree are large and no longer needed once installed.
rm -rf "$BUILD/llvm15" "$LLVM_SRC"
log "done: $LLVM_PREFIX"
