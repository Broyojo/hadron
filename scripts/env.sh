# Common environment for Hadron build scripts. Source, don't execute.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"        # upstream checkouts (git-ignored)
BUILD="$ROOT/build"    # out-of-tree build dirs (git-ignored)
DIST="$ROOT/dist"      # installed runtime tree (git-ignored)

BREW="$(brew --prefix)"

# Homebrew LLVM provides clang for both the macOS side and the PE cross targets
# (aarch64-windows, arm64ec-windows, i686-windows); lld provides lld-link.
# Homebrew bison is required because macOS ships bison 2.3.
export PATH="$BREW/opt/llvm/bin:$BREW/opt/lld/bin:$BREW/opt/bison/bin:$BREW/opt/flex/bin:$PATH"

JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

[[ "$(uname -m)" == arm64 ]] || die "Hadron targets Apple Silicon (arm64) hosts only"
