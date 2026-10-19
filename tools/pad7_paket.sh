#!/usr/bin/env bash
# Baut filamentwaage-pad7.zip: Ordner "filamentwaage" zum Kopieren auf die BOOT-Partition der Pad-7-SD-Karte.
#   tools/pad7_paket.sh [ausgabe.zip]
set -euo pipefail
WURZEL="$(cd "$(dirname "$0")/.." && pwd)"
AUS="$(realpath -m "${1:-$WURZEL/filamentwaage-pad7.zip}")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
P="$TMP/filamentwaage"
mkdir -p "$P/testdaten"
cp -r "$WURZEL/pi/filamentwaage" "$P/"
find "$P" -name __pycache__ -prune -exec rm -rf {} +
cp "$WURZEL/pi/pad7/install-pad7.sh" "$WURZEL/pi/pad7/kiosk-pad7.sh" \
   "$WURZEL/pi/pad7/filamentwaage-kiosk.service" "$WURZEL/pi/pad7/config.pad7.toml" \
   "$WURZEL/pi/pad7/ANLEITUNG.md" "$WURZEL/pi/pad7/filamentwaage.service" "$WURZEL/pi/pad7/erststart.sh" "$P/"
cp "$WURZEL/testdaten/"*.json "$P/testdaten/"
rm -f "$AUS"
(cd "$TMP" && zip -qr "$AUS" filamentwaage)
echo "$AUS"
