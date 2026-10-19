"""Gewicht und NFC-Tags – drei Quellen mit gleicher Schnittstelle:

- SensorUsb:   Sensorplatine (ESP32-S3-Zero) per USB, gleiche Firmware wie beim 5"-Display.
               Empfohlen: genaue Messung, Pi muss nichts Zeitkritisches tun.
- SensorGpio:  HX711 und zwei PN532 direkt am Raspberry Pi.
- SensorDemo:  ohne Hardware, zum Ausprobieren der Oberfläche.
"""
from __future__ import annotations

import json
import logging
import os
import threading
import time
from dataclasses import dataclass

log = logging.getLogger("sensor")


@dataclass
class Messwert:
    gramm: float = 0.0
    stabil: bool = False
    kalibriert: bool = False
    tag_l: str = ""
    tag_r: str = ""
    verbunden: bool = False


class Sensor:
    """Basis: Rückmeldungen zu Tara/Kalibrieren landen in self.rueckmeldung(ok, text)."""

    def __init__(self):
        self.rueckmeldung = lambda ok, text: None
        self._lock = threading.Lock()
        self._wert = Messwert()

    def wert(self):
        with self._lock:
            return Messwert(**self._wert.__dict__)

    def start(self):
        raise NotImplementedError

    def tara(self):
        raise NotImplementedError

    def kalibrieren(self, gramm):
        raise NotImplementedError


# ---------------------------------------------------------------- Messfilter (wie Firmware)
class Filter:
    MEDIAN_N, STABIL_N = 5, 10

    def __init__(self, glaettung=0.25, stabil_band=1.5):
        self.glaettung, self.band = glaettung, stabil_band
        self.roh, self.verlauf = [], []
        self.gramm, self.gueltig, self.stabil = 0.0, False, False
        self.median = 0

    def neu(self, roh, offset, faktor):
        self.roh = (self.roh + [roh])[-self.MEDIAN_N:]
        self.median = sorted(self.roh)[len(self.roh) // 2]
        if not faktor:
            self.gramm, self.stabil = 0.0, False
            return
        g = (self.median - offset) / faktor
        if not self.gueltig or abs(g - self.gramm) > 30:
            self.gramm = g
        else:
            self.gramm += self.glaettung * (g - self.gramm)
        self.gueltig = True
        self.verlauf = (self.verlauf + [self.gramm])[-self.STABIL_N:]
        self.stabil = len(self.verlauf) == self.STABIL_N and max(self.verlauf) - min(self.verlauf) < self.band

    def zuruecksetzen(self):
        self.gueltig, self.verlauf = False, []


# ---------------------------------------------------------------- USB
class SensorUsb(Sensor):
    def __init__(self, port="/dev/ttyACM0"):
        super().__init__()
        self.port = port
        self._befehle = []

    def start(self):
        threading.Thread(target=self._lauf, daemon=True, name="sensor-usb").start()

    def tara(self):
        with self._lock:
            self._befehle.append({"c": "tara"})

    def kalibrieren(self, gramm):
        with self._lock:
            self._befehle.append({"c": "kal", "g": int(gramm)})

    def _lauf(self):
        import serial

        while True:
            try:
                with serial.Serial(self.port, 115200, timeout=0.05) as ser:
                    log.info("Sensorplatine an %s", self.port)
                    self._sitzung(ser)
            except (OSError, serial.SerialException) as e:
                log.warning("Sensorplatine nicht erreichbar (%s), neuer Versuch …", e)
            with self._lock:
                self._wert.verbunden = False
            time.sleep(2)

    def _sitzung(self, ser):
        puffer = b""
        letzte_antwort = 0.0
        while True:
            with self._lock:
                befehl = self._befehle.pop(0) if self._befehle else {"c": "p"}
            ser.write((json.dumps(befehl) + "\n").encode())
            ende = time.monotonic() + 0.1
            while time.monotonic() < ende:
                puffer += ser.read(256)
                while b"\n" in puffer:
                    zeile, puffer = puffer.split(b"\n", 1)
                    if self._zeile(zeile.strip()):
                        letzte_antwort = time.monotonic()
            with self._lock:
                self._wert.verbunden = time.monotonic() - letzte_antwort < 1.0

    def _zeile(self, z):
        if not z.startswith(b"{"):
            return False
        try:
            d = json.loads(z)
        except ValueError:
            return False
        t = d.get("t")
        if t == "s":
            with self._lock:
                self._wert.gramm = float(d.get("g", 0))
                self._wert.stabil = bool(d.get("st"))
                self._wert.kalibriert = bool(d.get("cal"))
                self._wert.tag_l = d.get("L", "")
                self._wert.tag_r = d.get("R", "")
        elif t in ("ok", "err"):
            c = d.get("c")
            if t == "err":
                self.rueckmeldung(False, "Sensor: " + d.get("msg", "Fehler"))
            else:
                self.rueckmeldung(True, {"tara": "Tara gesetzt", "kal": "Kalibriert"}.get(c, "OK"))
        return True


# ---------------------------------------------------------------- GPIO
class SensorGpio(Sensor):
    PROBEN = 15

    def __init__(self, hx_dout, hx_sck, nfc_links, nfc_rechts, datei, tag_weg_s=1.2):
        super().__init__()
        self.hx_pins = (hx_dout, hx_sck)
        self.nfc_ports = (nfc_links, nfc_rechts)
        self.datei = datei
        self.tag_weg_s = tag_weg_s
        self.offset, self.faktor = 0, 0.0
        self.filter = Filter()
        self._vorgang = None  # ("tara"|"kal", gramm, [proben])
        self._laden()

    def _laden(self):
        try:
            with open(self.datei, encoding="utf-8") as f:
                d = json.load(f)
            self.offset, self.faktor = int(d.get("offset", 0)), float(d.get("faktor", 0))
        except (OSError, ValueError):
            pass

    def _speichern(self):
        os.makedirs(os.path.dirname(self.datei) or ".", exist_ok=True)
        with open(self.datei, "w", encoding="utf-8") as f:
            json.dump({"offset": self.offset, "faktor": self.faktor}, f)

    def start(self):
        threading.Thread(target=self._waage, daemon=True, name="hx711").start()
        threading.Thread(target=self._nfc, daemon=True, name="nfc").start()

    def tara(self):
        with self._lock:
            self._vorgang = ("tara", 0, [])

    def kalibrieren(self, gramm):
        if gramm < 50:
            self.rueckmeldung(False, "Kalibriergewicht fehlt")
            return
        with self._lock:
            self._vorgang = ("kal", int(gramm), [])

    def _waage(self):
        from .hx711 import Hx711

        hx = Hx711(*self.hx_pins)
        while True:
            roh = hx.lesen()
            if roh is None:
                with self._lock:
                    self._wert.verbunden = False
                time.sleep(0.5)
                continue
            fertig = None
            with self._lock:
                self.filter.neu(roh, self.offset, self.faktor)
                self._wert.gramm = round(self.filter.gramm, 1)
                self._wert.stabil = self.filter.stabil
                self._wert.kalibriert = self.faktor != 0
                self._wert.verbunden = True
                if self._vorgang:
                    art, g, proben = self._vorgang
                    proben.append(roh)
                    if len(proben) >= self.PROBEN:
                        fertig = (art, g, sum(proben) / len(proben))
                        self._vorgang = None
            if fertig:
                self._abschliessen(*fertig)
            time.sleep(0.01)

    def _abschliessen(self, art, gramm, mittel):
        if art == "tara":
            self.offset = int(mittel)
            self.filter.zuruecksetzen()
            self._speichern()
            self.rueckmeldung(True, "Tara gesetzt")
            return
        f = (mittel - self.offset) / gramm
        if abs(f) < 1:
            self.rueckmeldung(False, "Kein Gewicht erkannt – Tara gesetzt?")
            return
        self.faktor = f
        self.filter.zuruecksetzen()
        self._speichern()
        self.rueckmeldung(True, "Kalibriert")

    def _nfc(self):
        from .pn532 import Pn532Hsu

        leser = []
        for port in self.nfc_ports:
            if not port:
                leser.append(None)
                continue
            try:
                p = Pn532Hsu(port)
                if not p.begin():
                    log.warning("PN532 an %s antwortet nicht", port)
                leser.append(p)
            except OSError as e:
                log.warning("PN532 an %s: %s", port, e)
                leser.append(None)
        zuletzt = [0.0, 0.0]
        tags = ["", ""]
        while True:
            for i, p in enumerate(leser):
                if not p:
                    continue
                uid = p.lese_uid()
                jetzt = time.monotonic()
                if uid:
                    tags[i], zuletzt[i] = uid, jetzt
                elif tags[i] and jetzt - zuletzt[i] > self.tag_weg_s:
                    tags[i] = ""
            with self._lock:
                self._wert.tag_l, self._wert.tag_r = tags
            time.sleep(0.04)


# ---------------------------------------------------------------- Demo
class SensorDemo(Sensor):
    """Spielt Szenen ab: leer → bekannte Spule → neue Spule → ohne Tag."""

    def __init__(self, tags_bekannt=("04C0FFEE112233",)):
        super().__init__()
        self.tags = list(tags_bekannt) or ["04C0FFEE112233"]
        self.tara_g = 0.0

    def start(self):
        threading.Thread(target=self._lauf, daemon=True, name="demo").start()

    def tara(self):
        self.rueckmeldung(True, "Tara gesetzt (Demo)")

    def kalibrieren(self, gramm):
        self.rueckmeldung(True, f"Kalibriert mit {gramm} g (Demo)")

    def _lauf(self):
        import math

        szenen = [(0.0, "", ""), (812.0, "", self.tags[0]), (655.0, "", "04DEADBEEF0001"), (430.0, "", "")]
        t0 = time.monotonic()
        while True:
            t = time.monotonic() - t0
            g, l, r = szenen[int(t // 12) % len(szenen)]
            phase = t % 12
            rauschen = 0.3 * math.sin(t * 7) if phase > 1.5 else 8 * math.sin(t * 3)
            with self._lock:
                self._wert = Messwert(gramm=round(max(0.0, g + rauschen), 1) if g else round(rauschen * 0.05, 1),
                                      stabil=phase > 1.5, kalibriert=True, tag_l=l, tag_r=r, verbunden=True)
            time.sleep(0.1)
