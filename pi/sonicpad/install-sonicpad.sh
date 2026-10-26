#!/usr/bin/env bash
# FilamentLager-Waage auf dem Creality Sonic Pad einrichten.
#
# Grundlage: SonicPad-Debian (Debian 12, arm64, Benutzer sonic / Passwort pad),
# https://github.com/Teichoui/SonicPad-Debian – mit PhoenixSuit auf das Pad geflasht.
# Das Sonic Pad wird zum reinen Bildschirm der Waage: Klipper, Moonraker, KlipperScreen und
# crowsnest werden abgeschaltet (nicht gelöscht), die Waage-Oberfläche startet im Vollbild.
# Die Messung kommt per WLAN von der Sensorplatine (ESP32-S3) in der Waage.
#
# Aufruf auf dem Sonic Pad im entpackten Ordner:
#     sudo bash install-sonicpad.sh [--wlan=SSID] [--waage=ADRESSE] [--neue-config]
#   --wlan=SSID       Sonic Pad mit diesem WLAN verbinden (fragt nach dem Passwort)
#   --waage=ADRESSE   IP oder Name der Sensorplatine, z. B. 192.168.178.57
#   --neue-config     config.sonicpad.toml aus dem Paket übernehmen, auch wenn schon eine Konfiguration da ist
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
ZIEL=/opt/filamentwaage
[ "$(id -u)" = 0 ] || { echo "Bitte mit sudo starten: sudo bash $0"; exit 1; }

WLAN_SSID=""; WAAGE=""; NEUE_CONFIG=0
for a in "$@"; do
  case "$a" in
    --wlan=*) WLAN_SSID="${a#--wlan=}" ;;
    --waage=*) WAAGE="${a#--waage=}" ;;
    --neue-config) NEUE_CONFIG=1 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "Unbekannte Option: $a"; exit 1 ;;
  esac
done

BENUTZER="${SUDO_USER:-}"
if [ -z "$BENUTZER" ] || [ "$BENUTZER" = root ]; then
  BENUTZER="$(getent passwd 1000 | cut -d: -f1)"
fi
[ -n "$BENUTZER" ] || { echo "Kein normaler Benutzer gefunden (sonic?)"; exit 1; }
echo "== Benutzer: $BENUTZER"

if ! grep -qi "r818\|sun50iw10\|sonic" /proc/device-tree/model /etc/hostname 2>/dev/null; then
  echo "   Hinweis: Das sieht nicht nach einem Sonic Pad aus – es geht trotzdem weiter."
fi

if [ -n "$WLAN_SSID" ]; then
  echo "== WLAN „$WLAN_SSID“"
  read -r -s -p "   Passwort: " WLAN_PW; echo
  nmcli device wifi rescan >/dev/null 2>&1 || true
  sleep 3
  nmcli device wifi connect "$WLAN_SSID" password "$WLAN_PW" ifname wlan0
fi

echo "== Warte auf Internet"
for i in $(seq 1 60); do
  getent hosts deb.debian.org >/dev/null 2>&1 && break
  [ "$i" = 60 ] && { echo "Kein Internet – WLAN prüfen (nmcli device wifi list) und neu starten."; exit 1; }
  sleep 5
done

echo "== Pakete (dauert ein paar Minuten)"
export DEBIAN_FRONTEND=noninteractive
for i in 1 2 3 4 5 6; do
  if apt-get update -q 2>&1 | tee /tmp/apt-update.log | grep -qE '^(E:|Err:)'; then
    echo "   Paketliste unvollständig (Versuch $i), warte 20 s …"; sleep 20
  else
    break
  fi
done
apt-get install -y --fix-missing --no-install-recommends \
  python3 python3-serial curl \
  xserver-xorg xserver-xorg-legacy xserver-xorg-video-fbdev xserver-xorg-input-libinput \
  xinit x11-xserver-utils \
  chromium fonts-dejavu-core avahi-daemon libnss-mdns
apt-get install -y --no-install-recommends bluez python3-bleak \
  || echo "   python3-bleak nicht verfügbar – Etiketten nur per klassischem Bluetooth"

echo "== Programm nach $ZIEL"
mkdir -p "$ZIEL"
rm -rf "$ZIEL/filamentwaage"
cp -r "$HIER/filamentwaage" "$ZIEL/"
rm -rf "$ZIEL/hersteller"
[ -d "$HIER/hersteller" ] && cp -r "$HIER/hersteller" "$ZIEL/"
mkdir -p "$ZIEL/testdaten"
cp "$HIER/testdaten/"*.json "$ZIEL/testdaten/" 2>/dev/null || true
install -m 755 "$HIER/kiosk-sonicpad.sh" "$ZIEL/kiosk-sonicpad.sh"
[ -f "$HIER/VERSION" ] && install -m 644 "$HIER/VERSION" "$ZIEL/VERSION"
find "$ZIEL" -type d -exec chmod 755 {} +
find "$ZIEL/filamentwaage" -type f -exec chmod 644 {} +

echo "== Konfiguration"
mkdir -p /etc/filamentwaage
if [ ! -f /etc/filamentwaage/config.toml ] || [ "$NEUE_CONFIG" = 1 ]; then
  install -m 600 -o "$BENUTZER" "$HIER/config.sonicpad.toml" /etc/filamentwaage/config.toml
  echo "   config.sonicpad.toml übernommen"
else
  echo "   vorhandene /etc/filamentwaage/config.toml bleibt (--neue-config zum Überschreiben)"
fi
if [ -n "$WAAGE" ]; then
  sed -i "s/^wlan_adresse = .*/wlan_adresse = \"$WAAGE\"/" /etc/filamentwaage/config.toml
  echo "   Waage: $WAAGE"
fi
if grep -q '^token = ""' /etc/filamentwaage/config.toml; then
  echo "   ACHTUNG: Kein Home-Assistant-Token eingetragen – die Waage zeigt dann kein Lager."
  echo "   Nachtragen in der Waage unter Einstellungen → Home Assistant oder in /etc/filamentwaage/config.toml."
fi

echo "== Klipper-Dienste abschalten (bleiben installiert)"
for d in KlipperScreen klipper moonraker crowsnest nginx klipper-mcu moonraker-obico; do
  if systemctl cat "$d.service" >/dev/null 2>&1; then
    systemctl disable --now "$d.service" 2>/dev/null && echo "   $d aus" || true
  fi
done

echo "== Bildschirm (fbdev wie bei SonicPad-Debian)"
if [ ! -f /etc/X11/xorg.conf ] && [ ! -e /dev/dri/card0 ]; then
  cat > /etc/X11/xorg.conf <<'EOF'
Section "Device"
        Identifier      "Allwinner R818 FBDEV"
        Driver          "fbdev"
        Option          "fbdev" "/dev/fb0"
        Option          "SwapbuffersWait" "true"
EndSection
EOF
  echo "   /etc/X11/xorg.conf angelegt"
fi
cat > /etc/X11/Xwrapper.config <<'EOF'
allowed_users=anybody
needs_root_rights=yes
EOF
# Der Dienst verlangt die Gruppe bluetooth – auch wenn bluez nicht installiert werden konnte
getent group bluetooth >/dev/null || groupadd -r bluetooth
usermod -aG video,input,tty,dialout,bluetooth "$BENUTZER"
# Hintergrundbeleuchtung an (Werkzeug aus SonicPad-Debian)
command -v brightness >/dev/null && brightness -s 1 >/dev/null 2>&1 || true

echo "== Dienste"
sed "s/@BENUTZER@/$BENUTZER/" "$HIER/filamentwaage.service" > /etc/systemd/system/filamentwaage.service
sed "s/@BENUTZER@/$BENUTZER/" "$HIER/filamentwaage-kiosk.service" > /etc/systemd/system/filamentwaage-kiosk.service
systemctl disable --now getty@tty1.service 2>/dev/null || true
systemctl daemon-reload
systemctl enable filamentwaage.service filamentwaage-kiosk.service avahi-daemon.service
systemctl restart filamentwaage.service

echo
echo "Fertig. Das Sonic Pad startet jetzt neu und zeigt danach die Waage."
echo "Protokoll ansehen:  journalctl -u filamentwaage -f"
echo "Klipper zurück:     sudo systemctl disable filamentwaage-kiosk filamentwaage && sudo systemctl enable KlipperScreen klipper moonraker getty@tty1 && sudo reboot"
echo "Bitte das Pad nur mit „sudo shutdown now“ ausschalten, nicht mit dem Knopf an der Seite."
if [ "${FW_KEIN_NEUSTART:-0}" != 1 ]; then
  sleep 3
  reboot
fi
