# Where Hadron's launch scripts keep and find things. Source it after ROOT is set.
#
# A checkout has its build products and logs in build/. An installed runtime (made by
# scripts/package-runtime.sh) has no build/ and may be read-only, so its logs and the files it
# downloads go to the user's Library.
# What Hadron keeps for the user: the link Steam's tool starts, the copies of the runtime games
# run from (scripts/runtime-versions.sh), settings. HADRON_SUPPORT names another folder for a test.
HADRON_SUPPORT="${HADRON_SUPPORT:-$HOME/Library/Application Support/Hadron}"
if [ -d "$ROOT/build" ]; then
    HADRON_LOGS="$ROOT/build/logs"
    HADRON_VALVE="$ROOT/build/valve/bridge"
    HADRON_PREFIX="$ROOT/prefix/release"
    HADRON_OVERLAY_HELPER="$ROOT/build/notproton/overlay-shim.dylib"
else
    HADRON_LOGS="$HOME/Library/Logs/Hadron"
    HADRON_VALVE="$HADRON_SUPPORT/valve/bridge"
    HADRON_PREFIX="$HADRON_SUPPORT/prefix"
    HADRON_OVERLAY_HELPER="$ROOT/steam/overlay-shim.dylib"
fi
# The libraries Wine opens by name (FreeType, GnuTLS, SDL, the Vulkan loader): the runtime's own
# copies when it carries them, Homebrew's in a checkout.
if [ -d "$ROOT/dist/ext/lib" ]; then
    HADRON_LIBPATH="$ROOT/dist/ext/lib"
else
    HADRON_LIBPATH=/opt/homebrew/lib
fi
# The version: VERSION, and in a checkout the commit it is built from as well.
HADRON_VERSION=$(cat "$ROOT/VERSION" 2>/dev/null || echo unknown)
if [ -d "$ROOT/.git" ]; then
    HADRON_VERSION="$HADRON_VERSION-dev.$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
fi
