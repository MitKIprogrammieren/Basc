#!/usr/bin/env bash
# Startet die Oberfläche im Vollbild auf dem TFT50, sobald der Dienst läuft.
URL="${1:-http://127.0.0.1:8080}"
for _ in $(seq 1 60); do
  curl -fs "$URL/api/zustand" >/dev/null && break
  sleep 1
done
BROWSER="$(command -v chromium-browser || command -v chromium)"
exec "$BROWSER" --kiosk --app="$URL/?kiosk" --window-size=800,480 --window-position=0,0 \
  --noerrdialogs --disable-infobars --no-first-run --disable-translate \
  --lang=de --disable-features=Translate,TranslateUI,OverscrollHistoryNavigation --disable-pinch \
  --check-for-update-interval=31536000 --password-store=basic --incognito
