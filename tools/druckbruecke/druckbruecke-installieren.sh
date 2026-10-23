#!/bin/bash
# FilamentLager-Druckbrücke installieren – auf dem Raspberry Pi (z. B. dem Pi-hole) ausführen:
#   sudo bash druckbruecke-installieren.sh [Drucker-Adresse]
# Richtet einen kleinen Dienst ein (Port 8765), der Etiketten von der Waage per klassischem Bluetooth
# an den Phomemo M20 Pro schickt. Pi-hole bleibt unberührt.
set -e
DRUCKER="${1:-B6:61:E9:A5:D5:EB}"
PORT=8765
ZIEL=/opt/filamentwaage-druck

[ "$(id -u)" = 0 ] || { echo "Bitte mit sudo starten: sudo bash $0"; exit 1; }
echo "== FilamentLager-Druckbrücke für $DRUCKER"

# 1. Bluetooth vorhanden?
if ! ls /sys/class/bluetooth/hci* >/dev/null 2>&1; then
  for C in /boot/firmware/config.txt /boot/config.txt; do
    if [ -f "$C" ] && grep -qE '^\s*dtoverlay=(pi3-)?disable-bt' "$C"; then
      echo "!! Bluetooth ist in $C abgeschaltet (dtoverlay=disable-bt)."
      echo "   Zeile mit # auskommentieren, Pi neu starten, dann dieses Skript nochmal starten."
      exit 1
    fi
  done
  echo "!! Dieser Pi hat kein Bluetooth (erst ab Pi 3 / Pi Zero W) – oder es ist ausgeschaltet."
  exit 1
fi

# 2. Pakete
NEU=""
command -v bluetoothctl >/dev/null || NEU="$NEU bluez"
command -v python3 >/dev/null || NEU="$NEU python3"
if [ -n "$NEU" ]; then echo "== installiere:$NEU"; apt-get update -qq && apt-get install -y -qq $NEU; fi
systemctl enable --now bluetooth >/dev/null 2>&1 || true
python3 -c 'import socket,sys; sys.exit(0 if hasattr(socket,"AF_BLUETOOTH") else 1)' \
  || { echo "!! Python hier kann kein Bluetooth (AF_BLUETOOTH fehlt)"; exit 1; }

# 3. Dienst
mkdir -p "$ZIEL"
cat > "$ZIEL/druckbruecke.py" <<'PYTHON_ENDE'
#!/usr/bin/env python3
"""FilamentLager-Druckbrücke: nimmt Etiketten von der Waage an und druckt sie auf dem Phomemo M20 Pro.

Der M20 Pro druckt nur über klassisches Bluetooth (SPP/RFCOMM) und spricht TSPL (siehe
docs/PHOMEMO-M20PRO.md). Die Sensorplatine (ESP32-S3) hat kein klassisches Bluetooth – deshalb
schickt sie das Etikett per WLAN hierher.

    POST /drucken  {"breite": 320, "hoehe": 240, "daten": "<Base64, 1 Bit je Punkt, 1 = schwarz>",
                    "kopien": 1, "dichte": 10}
    POST /test     zwei schwarze Balken
    GET  /status   Drucker, letzter Druck, Antworten des Druckers

Nur Python-Standardbibliothek (läuft auch auf älterem Raspberry Pi OS neben Pi-hole).
Einstellungen in /etc/filamentwaage-druck.json: {"drucker": "B6:61:E9:A5:D5:EB", "kanal": 1, "port": 8765}
"""
import base64
import json
import logging
import os
import queue
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

VERSION = "1.0.0"
CFG_PFAD = os.environ.get("DRUCK_CONFIG", "/etc/filamentwaage-druck.json")
STANDARD = {"drucker": "", "kanal": 1, "port": 8765, "dpi": 300}
log = logging.getLogger("druckbruecke")


def cfg_laden():
    cfg = dict(STANDARD)
    try:
        with open(CFG_PFAD, encoding="utf-8") as f:
            cfg.update(json.load(f))
    except (OSError, ValueError):
        pass
    return cfg


CFG = cfg_laden()
ZUSTAND = {"letzter": "noch nichts gedruckt", "ok": None, "antworten": "", "zeit": ""}
LOCK = threading.Lock()
AUFTRAEGE = queue.Queue(maxsize=8)


# ------------------------------------------------------------------ TSPL
def tspl_auftrag(breite, hoehe, daten, dichte=10, kopien=1, dpi=300):
    """Raster der Oberfläche (8 Punkte/mm, 1 = schwarz) → TSPL für den M20 Pro (300 dpi, 1 = weiß).

    Das Bild geht in Streifen zu 24 Zeilen als einzelne BITMAP-Befehle; ganz weiße Streifen fallen weg.
    """
    f = dpi / 203.2
    b_mm, h_mm = (breite + 4) // 8, (hoehe + 4) // 8
    nb, nh = int(breite * f + 0.5), int(hoehe * f + 0.5)
    nbpr, bpr = (nb + 7) // 8, breite // 8
    tdichte = max(1, min(15, (int(dichte) * 7 + 5) // 10))  # 10 („Normal“) → 7 wie in der Phomemo-App
    # Spaltenzuordnung einmal vorrechnen
    sx = [min(breite - 1, int(x / f)) for x in range(nb)]
    bild = bytearray(b"\xff" * (nbpr * nh))
    for y in range(nh):
        sy = min(hoehe - 1, int(y / f))
        z = daten[sy * bpr:(sy + 1) * bpr]
        o = y * nbpr
        for x in range(nb):
            s = sx[x]
            if z[s >> 3] & (0x80 >> (s & 7)):
                bild[o + (x >> 3)] &= ~(0x80 >> (x & 7)) & 0xFF
    out = bytearray(
        ("SPEED 1\r\nSIZE %d mm,%d mm\r\nREFERENCE 0,0\r\nGAP 4.6 mm, 0 mm\r\nOFFSET 0 mm\r\n"
         "DIRECTION 0,0\r\nDENSITY %d\r\nCLS\r\n" % (b_mm, h_mm, tdichte)).encode())
    streifen = 24
    for y0 in range(0, nh, streifen):
        zeilen = min(streifen, nh - y0)
        teil = bytes(bild[y0 * nbpr:(y0 + zeilen) * nbpr])
        if teil.count(0xFF) == len(teil):
            continue
        out += b"BITMAP 0,%d,%d,%d,0," % (y0, nbpr, zeilen) + teil + b"\r\n"
    out += b"PRINT 1,%d\r\n" % max(1, min(10, int(kopien)))
    return bytes(out)


TEST = (b"SIZE 40 mm,30 mm\r\nGAP 4.6 mm, 0 mm\r\nDIRECTION 0,0\r\nDENSITY 7\r\nCLS\r\n"
        b"BAR 40,40,392,60\r\nBAR 40,150,200,40\r\nPRINT 1,1\r\n")


# ------------------------------------------------------------------ Bluetooth (RFCOMM)
def senden(auftrag):
    adresse = CFG.get("drucker", "")
    if not adresse:
        raise RuntimeError("Kein Drucker eingetragen (/etc/filamentwaage-druck.json)")
    if not hasattr(socket, "AF_BLUETOOTH"):
        raise RuntimeError("Python ohne Bluetooth-Unterstützung")
    antworten = b""
    with socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM) as s:
        s.settimeout(15)
        try:
            s.connect((adresse, int(CFG.get("kanal", 1))))
        except OSError as e:
            raise RuntimeError("Drucker nicht erreichbar (%s) – eingeschaltet? Mit Handy/Mac verbunden?" % e)
        s.sendall(b"SSSGETTYPE\r\n")
        for i in range(0, len(auftrag), 512):
            s.sendall(auftrag[i:i + 512])
            time.sleep(0.01)
        # Antworten mitlesen, bis der Drucker fertig meldet (höchstens 20 s)
        s.settimeout(1.0)
        ende = time.time() + 20
        while time.time() < ende:
            try:
                teil = s.recv(512)
            except socket.timeout:
                if b"GETTYPE" in antworten and b"PRINTING" not in antworten:
                    s.sendall(b"SSSGETPRINTING\r\n")
                continue
            if not teil:
                break
            antworten += teil
            if b"PRINTING:DONE" in antworten:
                break
    return antworten.decode("utf-8", "replace").strip()


def arbeiter():
    while True:
        art, auftrag = AUFTRAEGE.get()
        zeit = time.strftime("%H:%M:%S")
        try:
            antworten = senden(auftrag)
            ok, text = True, "%s gedruckt (%d Bytes)" % (art, len(auftrag))
        except Exception as e:  # noqa: BLE001 – Dienst soll weiterlaufen
            ok, text, antworten = False, str(e), ""
        log.info("%s: %s %s", zeit, text, antworten.replace("\r\n", " | "))
        with LOCK:
            ZUSTAND.update(letzter=text, ok=ok, antworten=antworten, zeit=zeit)


# ------------------------------------------------------------------ HTTP
class Server(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class Handler(BaseHTTPRequestHandler):
    server_version = "FilamentLager-Druck/" + VERSION

    def log_message(self, fmt, *args):
        log.debug(fmt, *args)

    def _json(self, daten, code=200):
        body = json.dumps(daten, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        if self.path.split("?")[0] in ("/", "/status"):
            with LOCK:
                z = dict(ZUSTAND)
            return self._json({"version": VERSION, "drucker": CFG.get("drucker", ""), **z})
        self._json({"ok": False, "text": "nicht gefunden"}, 404)

    def do_POST(self):
        pfad = self.path.split("?")[0]
        laenge = int(self.headers.get("Content-Length") or 0)
        if laenge > 2 * 1024 * 1024:
            return self._json({"ok": False, "text": "zu groß"}, 413)
        roh = self.rfile.read(laenge)
        try:
            if pfad == "/test":
                auftrag, art = TEST, "Test"
            elif pfad == "/drucken":
                d = json.loads(roh or b"{}")
                breite, hoehe = int(d["breite"]), int(d["hoehe"])
                daten = base64.b64decode(d["daten"])
                if breite <= 0 or breite % 8 or hoehe <= 0 or len(daten) != breite // 8 * hoehe:
                    raise ValueError("Etikett unvollständig")
                auftrag = tspl_auftrag(breite, hoehe, daten, d.get("dichte", 10), d.get("kopien", 1),
                                       int(CFG.get("dpi", 300)))
                art = "Etikett"
            else:
                return self._json({"ok": False, "text": "nicht gefunden"}, 404)
        except (KeyError, ValueError, TypeError) as e:
            return self._json({"ok": False, "text": "Etikett ungültig (%s)" % e}, 400)
        try:
            AUFTRAEGE.put_nowait((art, auftrag))
        except queue.Full:
            return self._json({"ok": False, "text": "Drucker ist noch beschäftigt"}, 503)
        self._json({"ok": True, "text": "Druckauftrag angenommen"}, 202)


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    threading.Thread(target=arbeiter, daemon=True).start()
    port = int(CFG.get("port", 8765))
    log.info("FilamentLager-Druckbrücke %s auf Port %d, Drucker %s", VERSION, port, CFG.get("drucker") or "–")
    Server(("0.0.0.0", port), Handler).serve_forever()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--test-tspl":  # Selbsttest ohne Drucker
        a = tspl_auftrag(320, 240, bytes([0xF0] * 40 * 240))
        print(a[:200], len(a))
    else:
        main()
PYTHON_ENDE
cat > /etc/filamentwaage-druck.json <<JSON
{"drucker": "$DRUCKER", "kanal": 1, "port": $PORT, "dpi": 300}
JSON
cat > /etc/systemd/system/filamentwaage-druck.service <<UNIT
[Unit]
Description=FilamentLager-Druckbrücke (Phomemo M20 Pro)
After=bluetooth.target network-online.target
Wants=bluetooth.target

[Service]
ExecStart=/usr/bin/python3 $ZIEL/druckbruecke.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now filamentwaage-druck >/dev/null
systemctl restart filamentwaage-druck

# 4. Drucker koppeln (falls er das verlangt; Phomemo-App vorher am Handy/Mac trennen)
echo "== suche den Drucker (15 s) – er muss eingeschaltet sein"
bluetoothctl --timeout 15 scan on >/dev/null 2>&1 || true
bluetoothctl pair "$DRUCKER" 2>&1 | grep -E "Pairing successful|Failed|AlreadyExists|not available" || true
bluetoothctl trust "$DRUCKER" >/dev/null 2>&1 || true

# 5. Testdruck
sleep 2
IP=$(hostname -I | awk '{print $1}')
echo "== Testdruck (zwei schwarze Balken)"
curl -s -X POST "http://127.0.0.1:$PORT/test" || python3 -c "import urllib.request;print(urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:$PORT/test',b'',method='POST')).read().decode())"
echo
sleep 12
curl -s "http://127.0.0.1:$PORT/status" || true
echo
echo
echo "== Fertig. In der Waage unter Einstellungen → Etiketten → Druckbrücke eintragen:"
echo "   http://$IP:$PORT"
echo "   Status jederzeit: http://$IP:$PORT/status    Protokoll: journalctl -u filamentwaage-druck -f"
