#!/usr/bin/env bash
# Write the update feed of a release: the one entry Sparkle in an older Hadron.app reads to learn
# that this version exists, what is new in it and where its disk image is. The entry carries the
# image's EdDSA signature, made with the private half of the key in packaging/update-key.pub; the
# app installs nothing whose signature that key does not confirm.
#
# Usage: scripts/make-appcast.sh <Hadron.app> <Hadron-x.dmg> <notes.md> <appcast.xml>
#
# The key is read from the login keychain, where Sparkle's generate_keys put it. For a rehearsal,
# HADRON_UPDATE_KEY_FILE names a file with another private key and HADRON_UPDATE_URL the address
# the image is served from.
source "$(dirname "$0")/env.sh"

app="${1:-}" dmg="${2:-}" notes="${3:-}" out="${4:-}"
[[ -d "$app" && -f "$dmg" && -f "$notes" && -n "$out" ]] || die "usage: $0 <Hadron.app> <Hadron-x.dmg> <notes.md> <appcast.xml>"
sign="$ROOT/toolchains/sparkle/bin/sign_update"
[[ -x "$sign" ]] || die "missing Sparkle, run scripts/fetch-sparkle.sh"

plist="$app/Contents/Info.plist"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")
build=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist")
minimum=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$plist")
url="${HADRON_UPDATE_URL:-https://github.com/Broyojo/hadron/releases/download/v$version/$(basename "$dmg")}"

# sign_update prints the two attributes of the enclosure: sparkle:edSignature="…" length="…".
signature=$("$sign" ${HADRON_UPDATE_KEY_FILE:+--ed-key-file "$HADRON_UPDATE_KEY_FILE"} "$dmg") ||
    die "cannot sign $dmg: is the update key in the keychain?"
[[ "$signature" == sparkle:edSignature=* ]] || die "unexpected output from sign_update: $signature"

{
    cat <<XML
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Hadron</title>
    <link>https://github.com/Broyojo/hadron</link>
    <item>
      <title>Hadron $version</title>
      <link>https://github.com/Broyojo/hadron/releases/tag/v$version</link>
      <pubDate>$(LC_ALL=C date -u "+%a, %d %b %Y %H:%M:%S +0000")</pubDate>
      <sparkle:version>$build</sparkle:version>
      <sparkle:shortVersionString>$version</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>$minimum</sparkle:minimumSystemVersion>
      <description sparkle:format="markdown"><![CDATA[
XML
    # The notes are the release's, as written; only the end of the CDATA section is escaped.
    sed 's/]]>/]]]]><![CDATA[>/g' "$notes"
    cat <<XML
]]></description>
      <enclosure url="$url" $signature type="application/octet-stream"/>
    </item>
  </channel>
</rss>
XML
} > "$out.new"
xmllint --noout "$out.new" || die "the feed is not well-formed XML"
mv -f "$out.new" "$out"
log "wrote $out: Hadron $version (build $build), $url"
