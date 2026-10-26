#!/usr/bin/env bash
# X-Client für das Sonic Pad: Oberfläche der Waage im Vollbild (7", 1024×600, Goodix-Touch).
# Das Sonic Pad hat keine GPU-Treiber für Chromium (fbdev), daher Software-Darstellung.
# Wird von filamentwaage-kiosk.service über xinit gestartet.
URL="${1:-http://127.0.0.1:8080}"

xset s off; xset s noblank; xset -dpms   # Bildschirm nie abschalten
command -v brightness >/dev/null && brightness -s 1 >/dev/null 2>&1   # Beleuchtung an

# Bildschirmgröße bestimmen (Sonic Pad: 1024x600)
GROESSE="$(xrandr 2>/dev/null | awk '/\*/ {print $1; exit}')"
B="${GROESSE%x*}"; H="${GROESSE#*x}"
[[ "$B" =~ ^[0-9]+$ && "$H" =~ ^[0-9]+$ ]] || { B=1024; H=600; }

# Warten, bis der Dienst antwortet
for _ in $(seq 1 90); do
  curl -fs "$URL/api/zustand" >/dev/null && break
  sleep 1
done

PROFIL="$HOME/.cache/filamentwaage-kiosk"
BROWSER="$(command -v chromium || command -v chromium-browser)"
while true; do
  rm -rf "$PROFIL"   # sauberer Start, keine "wiederherstellen?"-Leiste nach Stromausfall
  "$BROWSER" --kiosk --app="$URL/?kiosk" --user-data-dir="$PROFIL" \
    --window-size="$B,$H" --window-position=0,0 --start-fullscreen \
    --touch-events=enabled --disable-pinch --overscroll-history-navigation=0 \
    --noerrdialogs --disable-infobars --no-first-run --disable-translate --disable-sync \
    --lang=de --disable-features=Translate,TranslateUI,OverscrollHistoryNavigation,MediaRouter \
    --check-for-update-interval=31536000 --password-store=basic \
    --disable-gpu --disable-gpu-compositing --disk-cache-size=1 --renderer-process-limit=1 --disable-background-networking \
    2>/dev/null
  sleep 2   # Browser abgestürzt oder beendet: neu starten
done
