#!/usr/bin/env bash
# FilamentLager-Waage auf dem BIGTREETECH Pad 7 mit CB1 einrichten.
#
# Grundlage: BTT-Image "CB1_Debian…_Klipper_….img.xz" (Benutzer biqu / Passwort biqu).
# Das Pad 7 wird zum reinen Bildschirm der Waage: Klipper, Moonraker und KlipperScreen werden
# abgeschaltet (nicht gelöscht), die Waage-Oberfläche startet im Vollbild.
# Die Messung kommt per WLAN von der Sensorplatine (ESP32-S3-Zero) in der Waage.
#
# Aufruf (liegt nach dem Kopieren auf der BOOT-Partition):
#     sudo bash /boot/filamentwaage/install-pad7.sh
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
ZIEL=/opt/filamentwaage
[ "$(id -u)" = 0 ] || { echo "Bitte mit sudo starten: sudo bash $0"; exit 1; }

BENUTZER="${SUDO_USER:-}"
if [ -z "$BENUTZER" ] || [ "$BENUTZER" = root ]; then
  BENUTZER="$(getent passwd 1000 | cut -d: -f1)"
fi
[ -n "$BENUTZER" ] || { echo "Kein normaler Benutzer gefunden (biqu?)"; exit 1; }
echo "== Benutzer: $BENUTZER"

echo "== Pakete (dauert ein paar Minuten)"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
  python3 python3-serial curl \
  xserver-xorg xserver-xorg-legacy xinit x11-xserver-utils xserver-xorg-input-libinput \
  chromium fonts-dejavu-core avahi-daemon libnss-mdns
# TOML-Leser für Python < 3.11 (Debian 11); auf Debian 12 nicht nötig
if ! python3 -c "import tomllib" 2>/dev/null; then
  apt-get install -y python3-tomli || { apt-get install -y python3-pip && pip3 install tomli; }
fi

# Bluetooth für den Etikettendrucker (optional)
apt-get install -y --no-install-recommends bluez python3-bleak || echo "   python3-bleak nicht verfügbar – Etiketten nur per klassischem Bluetooth"

echo "== Programm nach $ZIEL"
mkdir -p "$ZIEL"
rm -rf "$ZIEL/filamentwaage"
cp -r "$HIER/filamentwaage" "$ZIEL/"
rm -rf "$ZIEL/hersteller"
[ -d "$HIER/hersteller" ] && cp -r "$HIER/hersteller" "$ZIEL/"
mkdir -p "$ZIEL/testdaten"
cp "$HIER/testdaten/"*.json "$ZIEL/testdaten/" 2>/dev/null || true
install -m 755 "$HIER/kiosk-pad7.sh" "$ZIEL/kiosk-pad7.sh"
[ -f "$HIER/VERSION" ] && install -m 644 "$HIER/VERSION" "$ZIEL/VERSION"
[ -f "$HIER/erststart.sh" ] && install -m 755 "$HIER/erststart.sh" "$ZIEL/erststart.sh"
find "$ZIEL" -type d -exec chmod 755 {} +
find "$ZIEL/filamentwaage" -type f -exec chmod 644 {} +

echo "== Konfiguration"
mkdir -p /etc/filamentwaage
# Die Datei auf der SD-Karte ist maßgeblich – dort lässt sie sich am PC bequem bearbeiten.
install -m 600 -o "$BENUTZER" "$HIER/config.pad7.toml" /etc/filamentwaage/config.toml
# Stand der SD-Karte merken: beim Start wird sie nur übernommen, wenn sie danach am PC geändert wurde
install -D -m 600 "$HIER/config.pad7.toml" /var/lib/filamentwaage/config.sd.toml
if grep -q '^token = ""' /etc/filamentwaage/config.toml; then
  echo "   ACHTUNG: Kein Home-Assistant-Token in config.pad7.toml – die Waage zeigt dann kein Lager."
fi

echo "== Klipper-Dienste abschalten (bleiben installiert)"
for d in KlipperScreen klipper moonraker crowsnest nginx klipper-mcu sonar; do
  if systemctl cat "$d.service" >/dev/null 2>&1; then
    systemctl disable --now "$d.service" 2>/dev/null && echo "   $d aus" || true
  fi
done

echo "== X-Server für normalen Benutzer erlauben"
cat > /etc/X11/Xwrapper.config <<'EOF'
allowed_users=anybody
needs_root_rights=yes
EOF
usermod -aG video,input,tty,dialout "$BENUTZER" || true

echo "== Dienste"
sed "s/@BENUTZER@/$BENUTZER/" "$HIER/filamentwaage.service" > /etc/systemd/system/filamentwaage.service
sed "s/@BENUTZER@/$BENUTZER/" "$HIER/filamentwaage-kiosk.service" > /etc/systemd/system/filamentwaage-kiosk.service
systemctl disable --now getty@tty1.service 2>/dev/null || true
systemctl daemon-reload
systemctl enable filamentwaage.service filamentwaage-kiosk.service avahi-daemon.service
systemctl restart filamentwaage.service

echo
echo "Fertig. Das Pad 7 startet jetzt neu und zeigt danach die Waage."
echo "Protokoll ansehen:  journalctl -u filamentwaage -f"
echo "Klipper zurück:     sudo systemctl disable filamentwaage-kiosk && sudo systemctl enable KlipperScreen klipper moonraker && sudo reboot"
if [ "${FW_KEIN_NEUSTART:-0}" != 1 ]; then
  sleep 3
  reboot
fi
