# Where Hadron's launch scripts keep and find things. Source it after ROOT is set.
#
# A checkout has its build products and logs in build/. An installed runtime (made by
# scripts/package-runtime.sh) has no build/ and may be read-only, so its logs and the files it
# downloads go to the user's Library.
if [ -d "$ROOT/build" ]; then
    HADRON_LOGS="$ROOT/build/logs"
    HADRON_VALVE="$ROOT/build/valve/bridge"
    HADRON_PREFIX="$ROOT/prefix/release"
else
    HADRON_LOGS="$HOME/Library/Logs/Hadron"
    HADRON_VALVE="$HOME/Library/Application Support/Hadron/valve/bridge"
    HADRON_PREFIX="$HOME/Library/Application Support/Hadron/prefix"
fi
# The libraries Wine opens by name (FreeType, GnuTLS, SDL, the Vulkan loader): the runtime's own
# copies when it carries them, Homebrew's in a checkout.
if [ -d "$ROOT/dist/ext/lib" ]; then
    HADRON_LIBPATH="$ROOT/dist/ext/lib"
else
    HADRON_LIBPATH=/opt/homebrew/lib
fi
# The version: an installed runtime carries it in VERSION, a checkout asks git.
if [ -f "$ROOT/VERSION" ]; then
    HADRON_VERSION=$(cat "$ROOT/VERSION")
else
    HADRON_VERSION=$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo unknown)
fi
