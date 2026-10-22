#!/usr/bin/env bash
# Installation der FilamentLager-Waage auf Raspberry Pi OS (Bookworm, Desktop).
# Als normaler Benutzer im Ordner pi/ ausführen:  ./install.sh [--gpio] [--tft50]
#   --gpio   UARTs für zwei PN532 direkt am Pi einschalten (uart3, uart4)
#   --tft50  Display-Overlay für das BTT PI TFT50 eintragen (nur nötig, wenn das Bild fehlt)
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
ZIEL=/opt/filamentwaage
BENUTZER="${SUDO_USER:-$USER}"
GPIO=0
TFT50=0
for a in "$@"; do
  case "$a" in
    --gpio) GPIO=1 ;;
    --tft50) TFT50=1 ;;
    *) echo "Unbekannte Option: $a"; exit 1 ;;
  esac
done

echo "== Pakete"
sudo apt-get update -qq
sudo apt-get install -y python3 python3-serial python3-lgpio python3-bleak bluez curl
command -v chromium-browser >/dev/null || command -v chromium >/dev/null || sudo apt-get install -y chromium-browser

echo "== Programm nach $ZIEL"
sudo mkdir -p "$ZIEL"
sudo rm -rf "$ZIEL/filamentwaage"
sudo cp -r "$HIER/filamentwaage" "$ZIEL/"
sudo rm -rf "$ZIEL/hersteller"
[ -d "$HIER/hersteller" ] && sudo cp -r "$HIER/hersteller" "$ZIEL/"
sudo mkdir -p "$ZIEL/testdaten"
sudo cp "$HIER/../testdaten/todo_filament.json" "$ZIEL/testdaten/" 2>/dev/null || true
sudo install -m 755 "$HIER/kiosk.sh" "$ZIEL/kiosk.sh"

echo "== Konfiguration"
sudo mkdir -p /etc/filamentwaage
if [ ! -f /etc/filamentwaage/config.toml ]; then
  sudo install -m 600 -o "$BENUTZER" "$HIER/config.example.toml" /etc/filamentwaage/config.toml
  echo "   /etc/filamentwaage/config.toml angelegt – Token eintragen!"
else
  echo "   vorhandene /etc/filamentwaage/config.toml bleibt"
fi

echo "== Dienst"
sudo usermod -aG dialout,gpio,bluetooth "$BENUTZER" || true
sed "s/@BENUTZER@/$BENUTZER/" "$HIER/filamentwaage.service" | sudo tee /etc/systemd/system/filamentwaage.service >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable --now filamentwaage.service

echo "== Kiosk beim Anmelden starten"
HOME_DIR="$(getent passwd "$BENUTZER" | cut -d: -f6)"
# labwc (Standard auf Bookworm ab Ende 2024)
mkdir -p "$HOME_DIR/.config/labwc"
touch "$HOME_DIR/.config/labwc/autostart"
grep -q filamentwaage "$HOME_DIR/.config/labwc/autostart" || echo "$ZIEL/kiosk.sh &" >> "$HOME_DIR/.config/labwc/autostart"
# wayfire (ältere Bookworm-Installationen)
if [ -f "$HOME_DIR/.config/wayfire.ini" ] && ! grep -q filamentwaage "$HOME_DIR/.config/wayfire.ini"; then
  if grep -q "^\[autostart\]" "$HOME_DIR/.config/wayfire.ini"; then
    sed -i "/^\[autostart\]/a filamentwaage = $ZIEL/kiosk.sh" "$HOME_DIR/.config/wayfire.ini"
  else
    printf "\n[autostart]\nfilamentwaage = %s/kiosk.sh\n" "$ZIEL" >> "$HOME_DIR/.config/wayfire.ini"
  fi
fi
# Bildschirm nicht abschalten
command -v raspi-config >/dev/null && sudo raspi-config nonint do_blanking 1 || true

BOOT=/boot/firmware/config.txt
[ -f "$BOOT" ] || BOOT=/boot/config.txt
eintragen() { grep -q "^$1" "$BOOT" || echo "$1" | sudo tee -a "$BOOT" >/dev/null; }
if [ "$GPIO" = 1 ]; then
  echo "== UARTs für die NFC-Leser in $BOOT"
  eintragen "dtoverlay=uart3"
  eintragen "dtoverlay=uart4"
fi
if [ "$TFT50" = 1 ]; then
  echo "== Overlay für das TFT50 in $BOOT"
  eintragen "dtoverlay=vc4-kms-dsi-7inch"
fi

echo
echo "Fertig. Nächste Schritte:"
echo "  1. Token in /etc/filamentwaage/config.toml eintragen"
echo "  2. sudo systemctl restart filamentwaage"
if [ "$GPIO$TFT50" != "00" ]; then echo "  3. Neu starten (config.txt geändert): sudo reboot"; fi
echo "Protokoll ansehen: journalctl -u filamentwaage -f"
