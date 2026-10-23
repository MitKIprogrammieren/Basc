"""Etiketten: Einstellungen, Herstellerbilder und Druckwarteschlange.

Das Etikett selbst zeichnet die Oberfläche (web/etikett.js). Hier landen das fertige
1-Bit-Raster zum Drucken und die Bilder, die du pro Hersteller hinterlegst.
"""
from __future__ import annotations

import base64
import json
import logging
import os
import queue
import re
import threading
import urllib.error
import urllib.request

from . import phomemo

log = logging.getLogger("etikett")

FORMATE = ("classic", "expanded", "compact", "vertikal")
BILD_MAX = 4 * 1024 * 1024
BILD_ARTEN = {"png": "image/png", "jpg": "image/jpeg", "webp": "image/webp"}


def bildart(daten: bytes):
    if daten.startswith(b"\x89PNG\r\n\x1a\n"):
        return "png"
    if daten.startswith(b"\xff\xd8\xff"):
        return "jpg"
    if len(daten) > 12 and daten[:4] == b"RIFF" and daten[8:12] == b"WEBP":
        return "webp"
    return None


def kurz(name: str) -> str:
    """Dateiname für einen Hersteller: „Bambu Lab“ → „bambu-lab“."""
    n = name.strip().lower()
    n = n.replace("ä", "ae").replace("ö", "oe").replace("ü", "ue").replace("ß", "ss")
    n = re.sub(r"[^a-z0-9]+", "-", n).strip("-")
    return n[:60]


class EtikettDienst:
    def __init__(self, cfg: dict, meldung):
        self.cfg = cfg
        self.meldung = meldung
        self.lock = threading.Lock()
        self.ordner = self._ordner(cfg.get("ordner") or "/var/lib/filamentwaage")
        self.bilder = os.path.join(self.ordner, "hersteller")
        os.makedirs(self.bilder, exist_ok=True)
        # mitgelieferte Bilder aus dem Repository (pi/hersteller → /opt/filamentwaage/hersteller)
        hier = os.path.dirname(os.path.abspath(__file__))
        self.mitgeliefert = [p for p in (cfg.get("bilder_repo") or os.path.join(hier, "..", "hersteller"),)
                             if os.path.isdir(p)]
        self.zustand_pfad = os.path.join(self.ordner, "etikett.json")
        self.werte = {
            "verbindung": cfg.get("verbindung", "ble"),
            "adresse": cfg.get("adresse", ""),
            "name": "",
            "format": cfg.get("format", "expanded"),
            "dichte": int(cfg.get("dichte", 10)),
            # Druckbrücke (tools/druckbruecke) für Drucker, die nur klassisches Bluetooth können (M20 Pro)
            "bruecke": cfg.get("bruecke", ""),
        }
        self._laden()
        self.auftraege: queue.Queue = queue.Queue()
        self.druckt = False
        self.bruecke_stand = -2   # -2 noch nicht gefragt, 1 Drucker an der Brücke verbunden, 0 nicht
        self.letzter_druck = -1   # -1 noch keiner, 1 geklappt, 0 fehlgeschlagen

    # ------------------------------------------------------------ Ablage
    @staticmethod
    def _ordner(wunsch):
        try:
            os.makedirs(wunsch, exist_ok=True)
            if os.access(wunsch, os.W_OK):
                return wunsch
        except OSError:
            pass
        ersatz = os.path.expanduser("~/.local/state/filamentwaage")
        os.makedirs(ersatz, exist_ok=True)
        log.info("%s nicht beschreibbar – Etiketten-Daten liegen in %s", wunsch, ersatz)
        return ersatz

    def _laden(self):
        try:
            with open(self.zustand_pfad, encoding="utf-8") as f:
                d = json.load(f)
            for k in self.werte:
                if k in d:
                    self.werte[k] = d[k]
        except (OSError, ValueError):
            pass

    def _speichern(self):
        tmp = self.zustand_pfad + ".neu"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.werte, f, ensure_ascii=False, indent=1)
        os.replace(tmp, self.zustand_pfad)

    # ------------------------------------------------------------ Einstellungen
    def einstellungen(self, hersteller: list[str]):
        with self.lock:
            w = dict(self.werte)
        w["kopf_px"] = int(self.cfg.get("kopf_px", 384))
        w["formate"] = list(FORMATE)
        w["druckt"] = self.druckt
        w["bilder"] = {h: self.bild_version(h) for h in hersteller if self.bild_pfad(h)}
        w["bilder_eigen"] = [h for h in w["bilder"] if self.bild_pfad(h, nur_eigen=True)]
        return w

    def setzen(self, d: dict):
        with self.lock:
            if d.get("format") in FORMATE:
                self.werte["format"] = d["format"]
            if "dichte" in d:
                self.werte["dichte"] = max(1, min(15, int(d["dichte"])))
            if "adresse" in d:
                self.werte["adresse"] = str(d["adresse"])[:40]
                self.werte["name"] = str(d.get("name", ""))[:40]
            if "bruecke" in d:
                b = str(d["bruecke"]).strip().rstrip("/")[:80]
                if b and not re.match(r"^https?://", b):
                    b = "http://" + b
                if b and not re.search(r":\d+$", b.split("://", 1)[1]):
                    b += ":8765"
                if b != self.werte.get("bruecke"):
                    self.bruecke_stand = -2
                self.werte["bruecke"] = b
            if d.get("verbindung") in ("ble", "rfcomm", "datei"):
                self.werte["verbindung"] = d["verbindung"]
            self._speichern()

    # ------------------------------------------------------------ Herstellerbilder
    def bild_pfad(self, hersteller: str, nur_eigen=False):
        """Hochgeladenes Bild hat Vorrang vor dem mitgelieferten aus dem Repository."""
        k = kurz(hersteller)
        if not k:
            return None
        ordner = [self.bilder] + ([] if nur_eigen else self.mitgeliefert)
        for o in ordner:
            for endung in BILD_ARTEN:
                p = os.path.join(o, f"{k}.{endung}")
                if os.path.isfile(p):
                    return p
        return None

    def bild_version(self, hersteller: str):
        p = self.bild_pfad(hersteller)
        return int(os.path.getmtime(p)) if p else 0

    def bild_speichern(self, hersteller: str, daten: bytes):
        k = kurz(hersteller)
        if not k:
            raise ValueError("Hersteller fehlt")
        if len(daten) > BILD_MAX:
            raise ValueError("Bild ist größer als 4 MB")
        art = bildart(daten)
        if not art:
            raise ValueError("Nur PNG, JPG oder WebP")
        self.bild_loeschen(hersteller)
        ziel = os.path.join(self.bilder, f"{k}.{art}")
        with open(ziel + ".neu", "wb") as f:
            f.write(daten)
        os.replace(ziel + ".neu", ziel)

    def bild_loeschen(self, hersteller: str):
        k = kurz(hersteller)
        for endung in BILD_ARTEN:
            p = os.path.join(self.bilder, f"{k}.{endung}")
            if k and os.path.isfile(p):
                os.remove(p)

    # ------------------------------------------------------------ Drucken
    def drucken(self, d: dict):
        """d: breite, hoehe (Punkte), daten (Base64, 1 Bit je Punkt), kopien."""
        breite, hoehe = int(d["breite"]), int(d["hoehe"])
        daten = base64.b64decode(d["daten"])
        kopien = max(1, min(10, int(d.get("kopien", 1))))
        if breite <= 0 or breite % 8 or hoehe <= 0 or len(daten) != breite // 8 * hoehe:
            raise ValueError("Etikett unvollständig")
        self.auftraege.put((breite, hoehe, daten, kopien))

    def _arbeiter(self):
        while True:
            breite, hoehe, daten, kopien = self.auftraege.get()
            with self.lock:
                w = dict(self.werte)
            self.druckt = True
            try:
                if w.get("bruecke"):
                    self.meldung("Schicke Etikett an die Druckbrücke …")
                    self._an_bruecke(w["bruecke"], breite, hoehe, daten, kopien, w["dichte"])
                    self.meldung("Druckbrücke hat das Etikett angenommen")
                    self.letzter_druck = 1
                    continue
                kb, kd = phomemo.auf_kopfbreite(breite, hoehe, daten, int(self.cfg.get("kopf_px", 384)))
                auftrag = phomemo.befehle(kb, hoehe, kd, w["dichte"]) * kopien
                self.meldung("Drucke Etikett …")
                phomemo.senden(auftrag, w["verbindung"], w["adresse"], int(self.cfg.get("kanal", 1)),
                               self.cfg.get("datei") or os.path.join(self.ordner, "letztes-etikett.bin"))
                self.meldung("Etikett gedruckt" if kopien == 1 else f"{kopien} Etiketten gedruckt")
                self.letzter_druck = 1
            except phomemo.DruckFehler as e:
                self.letzter_druck = 0
                self.meldung(str(e), ok=False)
            except Exception as e:  # noqa: BLE001
                log.exception("Druck fehlgeschlagen")
                self.meldung(f"Druck fehlgeschlagen: {e}", ok=False)
            finally:
                self.druckt = False

    @staticmethod
    def _an_bruecke(basis, breite, hoehe, daten, kopien, dichte):
        """Wie die Sensorplatine: POST /drucken an die Druckbrücke, die per klassischem Bluetooth druckt."""
        body = json.dumps({"breite": breite, "hoehe": hoehe, "kopien": kopien, "dichte": dichte,
                           "titel": "Etikett", "daten": base64.b64encode(daten).decode()}).encode()
        req = urllib.request.Request(basis + "/drucken", data=body, method="POST",
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                r.read()
        except urllib.error.HTTPError as e:
            try:
                text = json.loads(e.read() or b"{}").get("text")
            except ValueError:
                text = None
            raise phomemo.DruckFehler("Druckbrücke: " + (text or f"Fehler {e.code}")) from e
        except (urllib.error.URLError, OSError) as e:
            raise phomemo.DruckFehler(f"Druckbrücke nicht erreichbar ({basis}) – läuft der Dienst?") from e

    def stand(self):
        """Für den Punkt „Drucker“ in der Kopfzeile, wie auf der Sensorplatine:
        0 = kein Drucker / noch unbekannt, 1 = bereit, 2 = Problem."""
        with self.lock:
            bruecke, adresse = self.werte.get("bruecke"), self.werte.get("adresse")
        if bruecke:
            return 1 if self.bruecke_stand == 1 else 0 if self.bruecke_stand == -2 else 2
        if not adresse:
            return 0
        return 2 if self.letzter_druck == 0 else 1

    def _bruecke_fragen(self):
        """Fragt die Druckbrücke alle 15 s, ob der Drucker mit ihr verbunden ist."""
        import time
        while True:
            with self.lock:
                b = self.werte.get("bruecke")
            if b:
                try:
                    with urllib.request.urlopen(b + "/status", timeout=5) as r:
                        self.bruecke_stand = 1 if json.loads(r.read() or b"{}").get("verbunden") else 0
                except (OSError, ValueError):
                    self.bruecke_stand = 0
            else:
                self.bruecke_stand = -2
            time.sleep(15)

    def suchen(self):
        return phomemo.suchen()

    def start(self):
        threading.Thread(target=self._arbeiter, daemon=True, name="etikett").start()
        threading.Thread(target=self._bruecke_fragen, daemon=True, name="druckbruecke").start()
