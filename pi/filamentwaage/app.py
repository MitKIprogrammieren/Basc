"""FilamentLager-Waage auf dem Raspberry Pi: Dienst mit Web-Oberfläche.

Start:  python3 -m filamentwaage [config.toml]
Die Oberfläche läuft unter http://localhost:8080 im Chromium-Kiosk auf dem TFT50.
Nur Standardbibliothek plus pyserial (USB/NFC) bzw. lgpio (HX711 am Pi).
"""
from __future__ import annotations

import json
import logging
import mimetypes
import os
import queue
import sys
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11 (z. B. Debian 11 auf dem BTT CB1)
    try:
        import tomli as tomllib
    except ModuleNotFoundError:
        tomllib = None

from . import modell
from .ha import HaClient, HaFehler
from .sensoren import SensorDemo, SensorGpio, SensorUsb, SensorWlan

VERSION = "1.1.0"
WEB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
log = logging.getLogger("waage")

STANDARD = {
    "home_assistant": {"url": "http://homeassistant.local:8123", "token": "", "todo": "todo.filament",
                       "zertifikat_pruefen": True},
    "sensor": {"art": "usb", "usb_port": "/dev/ttyACM0", "wlan_adresse": "filamentwaage.local", "wlan_port": 4711, "hx711_dout": 23, "hx711_sck": 24,
               "nfc_links": "/dev/ttyAMA3", "nfc_rechts": "/dev/ttyAMA4",
               "kalibrierung": "/var/lib/filamentwaage/kalibrierung.json"},
    "waage": {"leerspule_g": 250, "lager_neu_laden_s": 60},
    "server": {"adresse": "127.0.0.1", "port": 8080},
}


def config_laden(pfad):
    cfg = json.loads(json.dumps(STANDARD))
    if pfad and os.path.exists(pfad):
        if tomllib is None:
            raise SystemExit("Python 3.11 oder neuer nötig – oder Paket python3-tomli installieren")
        with open(pfad, "rb") as f:
            eigene = tomllib.load(f)
        for abschnitt, werte in eigene.items():
            cfg.setdefault(abschnitt, {}).update(werte)
    return cfg


class Anwendung:
    def __init__(self, cfg):
        self.cfg = cfg
        ha = cfg["home_assistant"]
        self.ha = HaClient(ha["url"], ha["token"], ha["todo"], ha.get("zertifikat_pruefen", True))
        s = cfg["sensor"]
        art = s["art"]
        if art == "gpio":
            self.sensor = SensorGpio(s["hx711_dout"], s["hx711_sck"], s.get("nfc_links"), s.get("nfc_rechts"),
                                     s["kalibrierung"])
        elif art == "demo":
            self.sensor = SensorDemo()
        elif art == "wlan":
            self.sensor = SensorWlan(s["wlan_adresse"], s["wlan_port"])
        else:
            self.sensor = SensorUsb(s["usb_port"])
        self.sensor.rueckmeldung = lambda ok, text: self.meldung(text, ok)
        self.demo_ohne_ha = art == "demo" and not ha["token"]

        self.lock = threading.Lock()
        self.sorten: list[modell.Sorte] = []
        self.lager_version = 0
        self.ha_ok = False
        self.meldungen = deque(maxlen=20)
        self.meldung_id = 0
        self.auftraege: queue.Queue = queue.Queue()

    # ------------------------------------------------------------ Meldungen
    def meldung(self, text, ok=True):
        with self.lock:
            self.meldung_id += 1
            self.meldungen.append({"id": self.meldung_id, "text": text, "fehler": not ok})

    # ------------------------------------------------------------ Lager
    def _lager_setzen(self, sorten):
        with self.lock:
            self.sorten = sorten
            self.lager_version += 1
            self.ha_ok = True

    def _laden(self):
        if self.demo_ohne_ha:
            hier = os.path.dirname(os.path.abspath(__file__))
            kandidaten = [os.path.join(hier, "..", "testdaten", "todo_filament.json"),        # /opt/filamentwaage
                          os.path.join(hier, "..", "..", "testdaten", "todo_filament.json")]  # Repository
            pfad = next((k for k in kandidaten if os.path.exists(k)), kandidaten[-1])
            with open(pfad, encoding="utf-8") as f:
                self._lager_setzen(modell.lager_aus_antwort(json.load(f), "todo.filament"))
            return
        self._lager_setzen(self.ha.lager_laden())

    def _aendern(self, sorte_uid, funktion):
        if self.demo_ohne_ha:
            with self.lock:
                s = modell.finde_uid(self.sorten, sorte_uid)
            if not s:
                raise HaFehler("Sorte nicht mehr im Lager")
            a = funktion(s.beschreibung)
            if a.geaendert:
                with self.lock:
                    neu = [modell.sorte_aus_eintrag(x.uid, x.summary, a.beschreibung) if x.uid == sorte_uid else x
                           for x in self.sorten]
                self._lager_setzen(neu)
            return a.meldung
        frisch = self.ha.lager_laden()
        s = modell.finde_uid(frisch, sorte_uid)
        if not s:
            raise HaFehler("Sorte nicht mehr im Lager")
        a = funktion(s.beschreibung)
        if a.geaendert:
            self.ha.beschreibung_setzen(sorte_uid, a.beschreibung)
        return a.meldung

    def auftrag(self, art, **daten):
        self.auftraege.put((art, daten))

    def _arbeiter(self):
        while True:
            art, d = self.auftraege.get()
            try:
                text = ""
                if art == "rest":
                    text = self._aendern(d["sorte"], lambda b: modell.rest_speichern(b, int(d["netto"]), d.get("tag", "")))
                elif art == "spulen":
                    text = self._aendern(d["sorte"], lambda b: modell.spulen_aendern(b, int(d["delta"])))
                elif art == "zuordnen":
                    tag = d["tag"]
                    with self.lock:
                        andere = [s.uid for s in self.sorten if tag in s.nfc and s.uid != d["sorte"]]
                    for uid in andere:
                        self._aendern(uid, lambda b: modell.tag_entfernen(b, tag))
                    text = self._aendern(d["sorte"], lambda b: modell.tag_zuordnen(b, tag))
                if art == "laden":
                    if not (self.demo_ohne_ha and self.sorten):
                        self._laden()
                elif not self.demo_ohne_ha:
                    self._laden()
                if text:
                    self.meldung(text)
            except HaFehler as e:
                with self.lock:
                    war_ok = self.ha_ok
                    self.ha_ok = False
                # beim regelmäßigen Laden nur den ersten Ausfall melden
                if art != "laden" or war_ok or self.lager_version == 0:
                    self.meldung(str(e), ok=False)
            except Exception as e:  # noqa: BLE001 – Dienst soll weiterlaufen
                log.exception("Auftrag %s fehlgeschlagen", art)
                self.meldung(f"Fehler: {e}", ok=False)

    def _zyklisch_laden(self):
        while True:
            self.auftrag("laden")
            time.sleep(max(10, int(self.cfg["waage"]["lager_neu_laden_s"])))

    def start(self):
        self.sensor.start()
        threading.Thread(target=self._arbeiter, daemon=True, name="ha").start()
        threading.Thread(target=self._zyklisch_laden, daemon=True, name="laden").start()

    # ------------------------------------------------------------ Zustand für die Oberfläche
    def zustand(self):
        w = self.sensor.wert()
        with self.lock:
            return {
                "brutto_g": w.gramm, "stabil": w.stabil, "kalibriert": w.kalibriert,
                "tag_l": w.tag_l, "tag_r": w.tag_r, "sensor_ok": w.verbunden,
                "ha_ok": self.ha_ok, "lager_version": self.lager_version,
                "uhrzeit": time.strftime("%H:%M"), "leer_standard_g": int(self.cfg["waage"]["leerspule_g"]),
                "version": VERSION, "ip": _ip(),
            }

    def lager(self):
        with self.lock:
            return {"version": self.lager_version, "sorten": [s.als_dict() for s in self.sorten]}


_IP = {"t": 0, "ip": ""}


def _ip():
    if time.monotonic() - _IP["t"] > 30:
        import socket

        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect(("10.255.255.255", 1))
                _IP["ip"] = s.getsockname()[0]
        except OSError:
            _IP["ip"] = ""
        _IP["t"] = time.monotonic()
    return _IP["ip"]


def handler_fuer(app: Anwendung):
    class Handler(BaseHTTPRequestHandler):
        server_version = "FilamentWaage/" + VERSION

        def log_message(self, fmt, *args):
            log.debug(fmt, *args)

        def _json(self, daten, code=200):
            body = json.dumps(daten, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _datei(self, pfad):
            voll = os.path.realpath(os.path.join(WEB, pfad))
            if not voll.startswith(os.path.realpath(WEB) + os.sep) or not os.path.isfile(voll):
                self.send_error(404)
                return
            typ = mimetypes.guess_type(voll)[0] or "application/octet-stream"
            with open(voll, "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", typ + ("; charset=utf-8" if typ.startswith("text/") or typ.endswith("javascript") else ""))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            pfad = urlparse(self.path).path
            if pfad in ("/", "/index.html"):
                return self._datei("index.html")
            if pfad.startswith("/static/"):
                return self._datei(pfad[len("/static/"):])
            if pfad == "/api/zustand":
                return self._json(app.zustand())
            if pfad == "/api/lager":
                return self._json(app.lager())
            if pfad == "/api/ereignisse":
                return self._ereignisse()
            self.send_error(404)

        def _ereignisse(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            with app.lock:
                gesehen = app.meldung_id
            try:
                while True:
                    self.wfile.write(f"event: zustand\ndata: {json.dumps(app.zustand())}\n\n".encode())
                    with app.lock:
                        neu = [m for m in app.meldungen if m["id"] > gesehen]
                    for m in neu:
                        self.wfile.write(f"event: meldung\ndata: {json.dumps(m, ensure_ascii=False)}\n\n".encode())
                        gesehen = m["id"]
                    self.wfile.flush()
                    time.sleep(0.15)
            except (BrokenPipeError, ConnectionResetError):
                return

        def do_POST(self):
            pfad = urlparse(self.path).path
            laenge = int(self.headers.get("Content-Length") or 0)
            try:
                d = json.loads(self.rfile.read(laenge) or b"{}")
            except ValueError:
                return self._json({"ok": False, "fehler": "JSON ungültig"}, 400)
            w = app.sensor.wert()
            if pfad == "/api/tara":
                app.sensor.tara()
            elif pfad == "/api/kalibrieren":
                app.sensor.kalibrieren(int(d.get("g", 1000)))
                app.meldung(f"Kalibriere mit {int(d.get('g', 1000))} g …")
            elif pfad == "/api/rest":
                if not w.stabil:
                    app.meldung("Gewicht noch unruhig – kurz warten", ok=False)
                    return self._json({"ok": False})
                app.meldung("Speichere …")
                app.auftrag("rest", sorte=d["sorte"], netto=int(d["netto"]), tag=d.get("tag", ""))
            elif pfad == "/api/zuordnen":
                app.meldung("Ordne Tag zu …")
                app.auftrag("zuordnen", sorte=d["sorte"], tag=d["tag"])
            elif pfad == "/api/spulen":
                app.auftrag("spulen", sorte=d["sorte"], delta=int(d["delta"]))
            elif pfad == "/api/neu_laden":
                app.meldung("Lade Lager …")
                app.auftrag("laden")
            else:
                return self.send_error(404)
            return self._json({"ok": True}, 202)

    return Handler


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s: %(message)s")
    pfad = argv[0] if argv else os.environ.get("FILAMENTWAAGE_CONFIG", "/etc/filamentwaage/config.toml")
    cfg = config_laden(pfad)
    if not cfg["home_assistant"]["token"] and cfg["sensor"]["art"] != "demo":
        log.warning("Kein Home-Assistant-Token in %s – Lager bleibt leer", pfad)
    app = Anwendung(cfg)
    app.start()
    srv = ThreadingHTTPServer((cfg["server"]["adresse"], int(cfg["server"]["port"])), handler_fuer(app))
    srv.daemon_threads = True
    log.info("Oberfläche: http://%s:%s", cfg["server"]["adresse"], cfg["server"]["port"])
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
