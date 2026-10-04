#!/usr/bin/env bash
# Build FFmpeg for Wine's media playback (winedmo: Media Foundation and DirectShow sources), into
# dist/ffmpeg. Build it before Wine, whose configure looks for it there.
#
# Decoding only and under the LGPL: no encoders, muxers, devices or filters, nothing that needs
# --enable-gpl, and no external libraries. That is what a release can carry; Homebrew's FFmpeg
# links two dozen libraries, among them the GPL encoders x264 and x265.
source "$(dirname "$0")/env.sh"

FF="$SRC/ffmpeg"
[[ -x "$FF/configure" ]] || die "missing $FF, run scripts/fetch.sh ffmpeg"
mkdir -p "$BUILD/ffmpeg"
cd "$BUILD/ffmpeg"

if [[ ! -f config.h ]]; then
    log "configuring FFmpeg"
    "$FF/configure" --prefix="$DIST/ffmpeg" --cc=/usr/bin/clang \
        --enable-shared --disable-static --disable-programs --disable-doc --disable-debug \
        --disable-autodetect --disable-network \
        --disable-encoders --disable-muxers --disable-devices --disable-filters \
        --disable-avdevice --disable-avfilter --disable-swscale >/dev/null
fi
log "building FFmpeg"
make -j"$JOBS" >/dev/null
make install >/dev/null
log "installed FFmpeg to $DIST/ffmpeg ($(grep -o 'License: .*' config.h 2>/dev/null || sed -n 's/^#define FFMPEG_LICENSE "\(.*\)"/\1/p' config.h))"
