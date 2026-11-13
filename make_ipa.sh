#!/usr/bin/env bash
# Packages the .app inside an .xcarchive as an IPA.  Usage: make_ipa.sh <archive> <output.ipa>
# The result is only installable once it has been signed (Xcode export, Sideloadly, AltStore, ...).
set -euo pipefail
ARCHIVE="$1"; OUT="$2"
APP="$ARCHIVE/Products/Applications/SculptForge.app"
[ -d "$APP" ] || { echo "error: $APP not found"; exit 1; }
[ -f "$APP/SculptForge" ] || { echo "error: app binary missing"; exit 1; }
[ -f "$APP/default.metallib" ] || { echo "error: compiled Metal shaders missing"; exit 1; }
WORK="$(mktemp -d)"
mkdir -p "$WORK/Payload"
cp -R "$APP" "$WORK/Payload/"
OUT_ABS="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
rm -f "$OUT_ABS"
(cd "$WORK" && zip -qry "$OUT_ABS" Payload)
rm -rf "$WORK"
echo "wrote $OUT ($(du -h "$OUT" | cut -f1))"
unzip -l "$OUT" | tail -n 1
