#!/usr/bin/env bash
# Baut filamentwaage-sonicpad.zip: Ordner "filamentwaage" zum Kopieren aufs Sonic Pad (per scp).
#   tools/sonicpad_paket.sh [ausgabe.zip]
set -euo pipefail
WURZEL="$(cd "$(dirname "$0")/.." && pwd)"
AUS="$(realpath -m "${1:-$WURZEL/filamentwaage-sonicpad.zip}")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
P="$TMP/filamentwaage"
mkdir -p "$P/testdaten"
cp -r "$WURZEL/pi/filamentwaage" "$P/"
[ -d "$WURZEL/pi/hersteller" ] && cp -r "$WURZEL/pi/hersteller" "$P/"
find "$P" -name __pycache__ -prune -exec rm -rf {} +
cp "$WURZEL/pi/sonicpad/install-sonicpad.sh" "$WURZEL/pi/sonicpad/kiosk-sonicpad.sh" \
   "$WURZEL/pi/sonicpad/filamentwaage-kiosk.service" "$WURZEL/pi/sonicpad/filamentwaage.service" \
   "$WURZEL/pi/sonicpad/config.sonicpad.toml" "$WURZEL/pi/sonicpad/ANLEITUNG.md" "$P/"
cp "$WURZEL/testdaten/"*.json "$P/testdaten/"
# Versionsnummer wie die Firmware der Sensorplatine (die Oberfläche zeigt sie unter Einstellungen → System)
sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' "$WURZEL/firmware/sensor-s3zero/include/config.h" > "$P/VERSION"
rm -f "$AUS"
(cd "$TMP" && zip -qr "$AUS" filamentwaage)
echo "$AUS"
