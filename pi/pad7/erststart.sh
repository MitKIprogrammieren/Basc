#!/usr/bin/env bash
# Läuft einmal beim ersten Start des fertigen FilamentLager-Pad7-Images (filamentwaage-erststart.service).
# Wartet auf Internet, startet install-pad7.sh von der SD-Karte und zeigt den Fortschritt auf dem Bildschirm.
set -uo pipefail

MARKE=/var/lib/filamentwaage/installiert
LOG=/var/log/filamentwaage-erststart.log
QUELLE=""
for d in /boot/filamentwaage /boot/firmware/filamentwaage /opt/filamentwaage/sd; do
  [ -f "$d/install-pad7.sh" ] && { QUELLE="$d"; break; }
done

zeige() {  # Text auf dem Pad-7-Bildschirm (Konsole) und ins Protokoll
  printf '%s\n' "$*" | tee -a "$LOG" > /dev/tty1 2>/dev/null || echo "$*" >> "$LOG"
}

printf '\033c' > /dev/tty1 2>/dev/null
zeige ""
zeige "  FilamentLager-Waage – Ersteinrichtung"
zeige "  ======================================"
zeige ""
[ -n "$QUELLE" ] || { zeige "  FEHLER: Ordner filamentwaage fehlt auf der SD-Karte (BOOT)."; exit 1; }

zeige "  Warte auf Internet (WLAN in system.cfg eingetragen?) …"
for i in $(seq 1 120); do
  if getent hosts deb.debian.org >/dev/null 2>&1 && curl -fsI --max-time 8 http://deb.debian.org/debian/ >/dev/null 2>&1; then
    break
  fi
  [ "$i" = 120 ] && { zeige "  Kein Internet nach 10 Minuten – WLAN in system.cfg prüfen, dann neu starten."; exit 1; }
  sleep 5
done
IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
zeige "  Verbunden (IP $IP). Installiere – das dauert ca. 5–15 Minuten."
zeige "  Bitte nicht ausschalten."
zeige ""

if FW_KEIN_NEUSTART=1 bash "$QUELLE/install-pad7.sh" >> "$LOG" 2>&1; then
  mkdir -p "$(dirname "$MARKE")"
  date > "$MARKE"
  zeige "  Fertig. Neustart in die Waage …"
  sleep 3
  systemctl disable filamentwaage-erststart.service >/dev/null 2>&1
  reboot
else
  zeige "  Installation fehlgeschlagen. Protokoll: $LOG"
  zeige "  Neuer Versuch beim nächsten Einschalten."
  exit 1
fi
