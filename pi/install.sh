#!/usr/bin/env bash
# Installation der FilamentLager-Waage auf Raspberry Pi OS (Bookworm, Desktop).
# Als normaler Benutzer im Ordner pi/ ausführen:  ./install.sh [--wlan[=ADRESSE]] [--gpio] [--tft50] [--konsole]
#   --wlan     Sensorplatine (ESP32-S3-Zero) per WLAN, Standard-Adresse filamentwaage.local
#   --gpio     UARTs für zwei PN532 direkt am Pi einschalten (uart3, uart4)
#   --tft50    Display-Overlay für das BTT PI TFT50 eintragen (nur nötig, wenn das Bild fehlt)
#   --druckbruecke[=DRUCKER]  Druckbrücke für den Phomemo M20 Pro auf diesem Pi einrichten
#   --konsole  Oberfläche ohne Desktop starten (X-Server + Chromium auf tty1); ohne Desktop automatisch
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
ZIEL=/opt/filamentwaage
BENUTZER="${SUDO_USER:-$USER}"
GPIO=0
TFT50=0
WLAN=""
KONSOLE=0
BRUECKE=""
for a in "$@"; do
  case "$a" in
    --gpio) GPIO=1 ;;
    --tft50) TFT50=1 ;;
    --wlan) WLAN="filamentwaage.local" ;;
    --wlan=*) WLAN="${a#--wlan=}" ;;
    --konsole) KONSOLE=1 ;;
    --druckbruecke) BRUECKE="ja" ;;
    --druckbruecke=*) BRUECKE="${a#--druckbruecke=}" ;;
    *) echo "Unbekannte Option: $a"; exit 1 ;;
  esac
done

# Ohne Desktop (Raspberry Pi OS Lite, oder Start in die Konsole) läuft die
# Oberfläche über einen eigenen X-Server auf tty1, wie beim Pad 7.
if [ "$KONSOLE" = 0 ]; then
  if ! command -v labwc >/dev/null && ! command -v wayfire >/dev/null; then KONSOLE=1; fi
  [ "$(systemctl get-default 2>/dev/null)" = "multi-user.target" ] && KONSOLE=1
fi

echo "== Pakete"
sudo apt-get update -qq
sudo apt-get install -y python3 python3-serial python3-lgpio python3-bleak bluez curl
command -v chromium-browser >/dev/null || command -v chromium >/dev/null || sudo apt-get install -y chromium-browser || sudo apt-get install -y chromium
if [ "$KONSOLE" = 1 ]; then
  sudo apt-get install -y --no-install-recommends xserver-xorg xserver-xorg-legacy xinit x11-xserver-utils
fi

echo "== Ältere Waagen-Software abschalten (bleibt installiert)"
# Die frühere Pi-Version (SpoolmanScaleatPi) belegt dasselbe Display.
for d in spoolmanscale filamentlager-ha-bridge; do
  if systemctl cat "$d.service" >/dev/null 2>&1; then
    sudo systemctl disable --now "$d.service" 2>/dev/null && echo "   $d aus" || true
  fi
done

echo "== Programm nach $ZIEL"
sudo mkdir -p "$ZIEL"
sudo rm -rf "$ZIEL/filamentwaage"
sudo cp -r "$HIER/filamentwaage" "$ZIEL/"
sudo rm -rf "$ZIEL/hersteller"
[ -d "$HIER/hersteller" ] && sudo cp -r "$HIER/hersteller" "$ZIEL/"
sudo mkdir -p "$ZIEL/testdaten"
sudo cp "$HIER/../testdaten/todo_filament.json" "$ZIEL/testdaten/" 2>/dev/null || true
sudo install -m 755 "$HIER/kiosk.sh" "$ZIEL/kiosk.sh"
sudo install -m 755 "$HIER/pad7/kiosk-pad7.sh" "$ZIEL/kiosk-x.sh"
# Dieselbe Versionsnummer wie die Firmware der Sensorplatine
sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' "$HIER/../firmware/sensor-s3zero/include/config.h" | sudo tee "$ZIEL/VERSION" >/dev/null || true

echo "== Konfiguration"
sudo mkdir -p /etc/filamentwaage
if [ ! -f /etc/filamentwaage/config.toml ]; then
  sudo install -m 600 -o "$BENUTZER" "$HIER/config.example.toml" /etc/filamentwaage/config.toml
  echo "   /etc/filamentwaage/config.toml angelegt – Token eintragen!"
else
  echo "   vorhandene /etc/filamentwaage/config.toml bleibt"
fi
if [ -n "$WLAN" ]; then
  echo "   Sensor per WLAN: $WLAN"
  sudo sed -i -e 's/^art = .*/art = "wlan"/' -e "s/^wlan_adresse = .*/wlan_adresse = \"$WLAN\"/" /etc/filamentwaage/config.toml
fi

echo "== Dienst"
sudo usermod -aG dialout,gpio,bluetooth "$BENUTZER" || true
sed "s/@BENUTZER@/$BENUTZER/" "$HIER/filamentwaage.service" | sudo tee /etc/systemd/system/filamentwaage.service >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable --now filamentwaage.service

if [ -n "$BRUECKE" ]; then
  echo "== Druckbrücke für den Phomemo M20 Pro auf diesem Pi"
  if [ "$BRUECKE" = "ja" ]; then sudo bash "$HIER/../tools/druckbruecke/druckbruecke-installieren.sh"
  else sudo bash "$HIER/../tools/druckbruecke/druckbruecke-installieren.sh" "$BRUECKE"; fi
  if grep -q '^bruecke = ' /etc/filamentwaage/config.toml; then
    sudo sed -i 's#^bruecke = .*#bruecke = "http://127.0.0.1:8765"#' /etc/filamentwaage/config.toml
  else
    sudo sed -i '/^\[etikett\]/a bruecke = "http://127.0.0.1:8765"' /etc/filamentwaage/config.toml
  fi
  sudo systemctl restart filamentwaage.service
fi

if [ "$KONSOLE" = 1 ]; then
  echo "== Bildschirm ohne Desktop: X-Server + Chromium auf tty1"
  # Xorg.wrap (xserver-xorg-legacy) startet den X-Server mit Rechten auf tty1,
  # ohne ihn lief er auf Trixie als Benutzer: "Cannot open virtual console 1".
  printf 'allowed_users=anybody\nneeds_root_rights=yes\n' | sudo tee /etc/X11/Xwrapper.config >/dev/null
  sudo usermod -aG video,input,tty "$BENUTZER" || true
  sed -e "s/@BENUTZER@/$BENUTZER/" -e "s#/opt/filamentwaage/kiosk-pad7.sh#$ZIEL/kiosk-x.sh#" \
      -e "s/(Pad 7)/(TFT50)/" "$HIER/pad7/filamentwaage-kiosk.service" \
    | sudo tee /etc/systemd/system/filamentwaage-kiosk.service >/dev/null
  sudo systemctl disable getty@tty1.service 2>/dev/null || true
  sudo systemctl daemon-reload
  sudo systemctl enable filamentwaage-kiosk.service
  sudo systemctl restart filamentwaage-kiosk.service || true
fi

if [ "$KONSOLE" = 0 ]; then
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
