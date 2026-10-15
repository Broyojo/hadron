#!/usr/bin/env bash
# Build GStreamer for Wine's Media Foundation and DirectShow decoders (winegstreamer), into
# dist/gstreamer. Build it after FFmpeg and before Wine, whose configure looks for it there.
#
# Only what winegstreamer uses, as Proton ships it: the core, the base plugins that convert and
# resample and autoplug (decodebin), the demuxers for the common containers (MP4, Matroska and
# WebM, AVI, WAV, ASF), the stream parsers, and the decoders of gst-libav, which decode with
# Hadron's own FFmpeg (scripts/build-ffmpeg.sh). LGPL throughout: no GPL plugins, nothing
# external but GLib, which comes from Homebrew and is bundled with the runtime like the others.
source "$(dirname "$0")/env.sh"

GST="$SRC/gstreamer/subprojects"
[[ -d "$GST/gstreamer" ]] || die "missing $GST, run scripts/fetch.sh gstreamer"
[[ -f "$DIST/ffmpeg/lib/pkgconfig/libavfilter.pc" ]] || die "FFmpeg without libavfilter: run scripts/build-ffmpeg.sh"

export PKG_CONFIG_PATH="$DIST/gstreamer/lib/pkgconfig:$DIST/ffmpeg/lib/pkgconfig:$BREW/opt/glib/lib/pkgconfig:$BREW/opt/libffi/lib/pkgconfig:$BREW/opt/pcre2/lib/pkgconfig"
common=(--prefix="$DIST/gstreamer" --libdir=lib --buildtype=release --wrap-mode=nofallback
        -Dauto_features=disabled -Dtests=disabled -Ddoc=disabled)
# What all but gst-libav have. Orc, which speeds up conversion, is left out for now.
most=(-Dnls=disabled)

build() {  # <subproject> <its options...>
    local name="$1"; shift
    local dir="$BUILD/gstreamer/$name"
    if [[ ! -f "$dir/build.ninja" || "$(cat "$dir/.hadron-options" 2>/dev/null)" != "${common[*]} $*" ]]; then
        log "configuring $name"
        rm -rf "$dir"
        meson setup "$dir" "$GST/$name" "${common[@]}" "$@" >/dev/null || die "configuring $name failed"
        echo "${common[*]} $*" > "$dir/.hadron-options"
    fi
    log "building $name"
    meson install -C "$dir" >/dev/null || die "building $name failed"
}

build gstreamer "${most[@]}" -Dexamples=disabled -Dtools=enabled -Dintrospection=disabled \
    -Dptp-helper=disabled -Dcheck=disabled -Dlibunwind=disabled -Dlibdw=disabled \
    -Dbash-completion=disabled -Dbenchmarks=disabled -Dcoretracers=disabled
build gst-plugins-base "${most[@]}" -Dexamples=disabled -Dtools=disabled -Dintrospection=disabled \
    -Dorc=disabled -Dapp=enabled -Daudioconvert=enabled -Daudioresample=enabled -Dplayback=enabled \
    -Dtypefind=enabled -Dvideoconvertscale=enabled -Dpbtypes=enabled
build gst-plugins-good "${most[@]}" -Dexamples=disabled -Dorc=disabled \
    -Disomp4=enabled -Dmatroska=enabled -Davi=enabled -Dwavparse=enabled -Daudioparsers=enabled \
    -Did3demux=enabled -Dapetag=enabled -Dvideofilter=enabled -Ddeinterlace=enabled
build gst-plugins-bad "${most[@]}" -Dexamples=disabled -Dtools=disabled -Dintrospection=disabled \
    -Dorc=disabled -Dvideoparsers=enabled
build gst-plugins-ugly "${most[@]}" -Dorc=disabled -Dasfdemux=enabled
build gst-libav

log "installed GStreamer $(git -C "$SRC/gstreamer" describe --tags 2>/dev/null) to $DIST/gstreamer:" \
    "$(ls "$DIST/gstreamer/lib/gstreamer-1.0" | grep -c '^libgst.*\.dylib$') plugins"
