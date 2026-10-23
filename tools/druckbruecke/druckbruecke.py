#!/usr/bin/env python3
"""FilamentLager-Druckbrücke: nimmt Etiketten von der Waage an und druckt sie auf dem Phomemo M20 Pro.

Der M20 Pro druckt nur über klassisches Bluetooth (SPP/RFCOMM) und spricht TSPL (siehe
docs/PHOMEMO-M20PRO.md). Die Sensorplatine (ESP32-S3) hat kein klassisches Bluetooth – deshalb
schickt sie das Etikett per WLAN hierher.

    POST /drucken  {"breite": 320, "hoehe": 240, "daten": "<Base64, 1 Bit je Punkt, 1 = schwarz>",
                    "kopien": 1, "dichte": 10}
    POST /test     zwei schwarze Balken
    GET  /status   Drucker, Verbindung, letzter Druck, Antworten des Druckers
    POST /freigeben  Dauerverbindung für 10 Minuten lösen (z. B. für die Phomemo-App)

Der Status enthält außerdem, was der Drucker über sich meldet (Restetiketten der Rolle, RFID, Firmware,
Seriennummer), die Signalstärke, Verbindungsabbrüche und den Druckverlauf (/var/lib/filamentwaage-druck).

Mit "dauerhaft": true (Standard) bleibt die Bluetooth-Verbindung offen und wird nach jedem Abbruch
sofort neu aufgebaut; alle 30 s geht eine harmlose Abfrage als Lebenszeichen an den Drucker.

Nur Python-Standardbibliothek (läuft auch auf älterem Raspberry Pi OS neben Pi-hole).
Einstellungen in /etc/filamentwaage-druck.json: {"drucker": "B6:61:E9:A5:D5:EB", "kanal": 1, "port": 8765}
"""
import base64
import json
import logging
import os
import queue
import re
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

VERSION = "1.2.2"
CFG_PFAD = os.environ.get("DRUCK_CONFIG", "/etc/filamentwaage-druck.json")
STANDARD = {"drucker": "", "kanal": 1, "port": 8765, "dpi": 300, "dauerhaft": True}
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
INFO = {}        # was der Drucker meldet: TYPE, VERSION, SN, RFID, PAPERCAPACITY, PAPERTOTALITY …
SIGNAL = {"rssi": None, "zeit": ""}
LOCK = threading.Lock()
DATEN_ORDNER = os.environ.get("DRUCK_DATEN", "/var/lib/filamentwaage-druck")
INFO_FRAGEN = (b"SSSGETTYPE\r\nSSSGETVERSION\r\nSSSGETSN\r\nSSSGETRFID\r\n"
               b"SSSGETPAPERCAPACITY\r\nSSSGETPAPERTOTALITY\r\n")
# Den Befehl für den Akkustand kennen wir noch nicht (die App fragt ihn im Mitschnitt nicht ab).
# Unbekannte Befehle beantwortet der Drucker mit „Cmd error:…“ – harmlos. Nach dem Verbinden einmal
# durchprobieren; was eine echte Antwort liefert, wird gemerkt und künftig mit abgefragt.
AKKU_KANDIDATEN = ["BATTERY", "BAT", "BATTERYLEVEL", "BATTERYVOL", "POWER", "ELECTRICITY", "VOLTAGE", "CHARGE"]
AKKU_BEFEHL = None


# ------------------------------------------------------------------ Druckverlauf
class Verlauf:
    """Letzte Drucke und Zähler, überlebt Neustarts (JSON-Datei)."""

    def __init__(self):
        self.pfad = os.path.join(DATEN_ORDNER, "verlauf.json")
        self.eintraege, self.gesamt, self.abbrueche = [], 0, {}
        try:
            with open(self.pfad, encoding="utf-8") as f:
                d = json.load(f)
            self.eintraege, self.gesamt = d.get("eintraege", []), int(d.get("gesamt", 0))
        except (OSError, ValueError):
            pass

    def _speichern(self):
        try:
            os.makedirs(DATEN_ORDNER, exist_ok=True)
            tmp = self.pfad + ".neu"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump({"eintraege": self.eintraege[-200:], "gesamt": self.gesamt}, f, ensure_ascii=False)
            os.replace(tmp, self.pfad)
        except OSError as e:
            log.warning("Verlauf nicht gespeichert: %s", e)

    def neu(self, titel, ok, text):
        with LOCK:
            self.eintraege.append({"zeit": time.strftime("%Y-%m-%d %H:%M:%S"), "titel": titel, "ok": ok, "text": text})
            if ok:
                self.gesamt += 1
            self._speichern()

    def abbruch(self):
        with LOCK:
            heute = time.strftime("%Y-%m-%d")
            self.abbrueche = {heute: self.abbrueche.get(heute, 0) + 1}

    def zusammenfassung(self):
        heute = time.strftime("%Y-%m-%d")
        with LOCK:
            von_heute = [e for e in self.eintraege if e["zeit"].startswith(heute)]
            return {"heute": sum(1 for e in von_heute if e["ok"]),
                    "fehler_heute": sum(1 for e in von_heute if not e["ok"]),
                    "gesamt": self.gesamt,
                    "abbrueche_heute": self.abbrueche.get(heute, 0),
                    "letzte": list(reversed(self.eintraege[-10:]))}


VERLAUF = Verlauf()


def signal_messen():
    """Signalstärke der Verbindung (hcitool rssi), 0 ist ideal, je negativer desto schwächer."""
    try:
        aus = subprocess.run(["hcitool", "rssi", CFG.get("drucker", "")], capture_output=True, text=True,
                             timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return
    if "RSSI return value:" in aus:
        try:
            SIGNAL.update(rssi=int(aus.split("RSSI return value:")[1].split()[0]), zeit=time.strftime("%H:%M:%S"))
        except (ValueError, IndexError):
            pass


def signal_text(rssi):
    if rssi is None:
        return "–"
    return "sehr gut" if rssi >= -3 else "gut" if rssi >= -8 else "mäßig" if rssi >= -15 else "schwach"
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
class Verbindung:
    """Hält die RFCOMM-Verbindung zum Drucker offen und baut sie bei Abbruch neu auf."""

    def __init__(self):
        self.lock = threading.RLock()
        self.sock = None
        self.puffer = b""          # Antworten des Druckers
        self.seit = ""
        self.fehler = ""
        self.frei_bis = 0.0        # bis dahin keine Dauerverbindung (Phomemo-App)
        self.zuletzt_gesendet = 0.0
        self.zuletzt_info = 0.0
        self.zeilen = b""

    @property
    def verbunden(self):
        return self.sock is not None

    def _verbinden(self):
        adresse = CFG.get("drucker", "")
        if not adresse:
            raise RuntimeError("Kein Drucker eingetragen (/etc/filamentwaage-druck.json)")
        if not hasattr(socket, "AF_BLUETOOTH"):
            raise RuntimeError("Python ohne Bluetooth-Unterstützung")
        letzter = None
        for versuch in range(3):  # Drucker an der Reichweitengrenze antworten oft erst beim zweiten Mal
            s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
            s.settimeout(25)
            try:
                s.connect((adresse, int(CFG.get("kanal", 1))))
                s.settimeout(None)
                self.sock = s
                self.seit = time.strftime("%H:%M:%S")
                self.fehler = ""
                self.puffer = b""
                self.zeilen = b""
                self.zuletzt_info = 0.0  # Infos gleich abfragen
                self.akku_probiert = False
                threading.Thread(target=self._lesen, args=(s,), daemon=True).start()
                log.info("verbunden mit %s", adresse)
                return
            except OSError as e:
                letzter = e
                s.close()
                time.sleep(2)
        self.fehler = str(letzter)
        raise RuntimeError("Drucker nicht erreichbar (%s) – eingeschaltet, in Reichweite, nicht mit Handy/Mac verbunden?"
                           % letzter)

    def _lesen(self, s):
        global AKKU_BEFEHL
        while True:
            try:
                teil = s.recv(512)
            except OSError:
                teil = b""
            if not teil:
                break
            with self.lock:
                self.puffer = (self.puffer + teil)[-4000:]
                self.zeilen += teil
                *fertig, self.zeilen = self.zeilen.split(b"\r\n")
            # Der Drucker hängt Antworten teils ohne Zeilenende aneinander, z. B.
            # „SSSGETBATTERY:9%Cmd error:SSSGETPRINTING“ – daher an jeder Antwort-Marke trennen.
            stuecke = []
            for z in fertig:
                z = z.decode("utf-8", "replace")
                stuecke += re.split(r"(?=SSSGET[A-Z]+:)|(?=Cmd error:)", z)
            for z in stuecke:  # z. B. SSSGETPAPERCAPACITY:105
                z = z.strip()
                if z.startswith("SSSGET") and ":" in z:
                    k, _, v = z[6:].partition(":")
                    v = v.strip()
                    if k in AKKU_KANDIDATEN:
                        v = v.rstrip("%").strip()
                    INFO[k] = v
                    INFO["zeit"] = time.strftime("%H:%M:%S")
                    if k in AKKU_KANDIDATEN and AKKU_BEFEHL is None:
                        AKKU_BEFEHL = k
                        log.info("Akkustand kommt über SSSGET%s: %s", k, v)
        with self.lock:
            if self.sock is s:
                self.sock = None
                log.info("Verbindung zum Drucker getrennt")
                VERLAUF.abbruch()
        try:
            s.close()
        except OSError:
            pass

    def trennen(self):
        with self.lock:
            s, self.sock = self.sock, None
        if s:
            try:
                s.close()
            except OSError:
                pass

    def senden(self, daten):
        with self.lock:
            if not self.sock:
                self._verbinden()
            try:
                for i in range(0, len(daten), 512):
                    self.sock.sendall(daten[i:i + 512])
                    time.sleep(0.01)
                self.zuletzt_gesendet = time.time()
            except OSError:
                # alte Verbindung war tot – einmal neu verbinden und nochmal
                self.trennen()
                self._verbinden()
                for i in range(0, len(daten), 512):
                    self.sock.sendall(daten[i:i + 512])
                    time.sleep(0.01)
                self.zuletzt_gesendet = time.time()

    def drucken(self, auftrag):
        with self.lock:
            self.puffer = b""  # nur Antworten zu diesem Auftrag
            start = 0
            self.senden(b"SSSGETTYPE\r\n" + auftrag)
        ende = time.time() + 25
        gefragt = 0.0
        while time.time() < ende:
            time.sleep(0.5)
            with self.lock:
                neu = self.puffer[start:] if len(self.puffer) >= start else self.puffer
            if b"PRINTING:DONE" in neu:
                break
            if time.time() - gefragt > 3:
                gefragt = time.time()
                try:
                    self.senden(b"SSSGETPRINTING\r\n")
                except RuntimeError:
                    break
        with self.lock:
            antworten = self.puffer[start:] if len(self.puffer) >= start else self.puffer
        try:
            with self.lock:
                self.senden(INFO_FRAGEN)  # Restetiketten aktualisieren
            time.sleep(0.8)
        except (RuntimeError, OSError):
            pass
        if not CFG.get("dauerhaft", True):
            self.trennen()
        return antworten.decode("utf-8", "replace").strip()

    def waechter(self):
        """Hält die Verbindung offen (wenn dauerhaft) – mit wachsenden Pausen bei Fehlschlägen."""
        pause = 5
        while True:
            time.sleep(1)
            if not CFG.get("dauerhaft", True) or time.time() < self.frei_bis:
                if self.verbunden and time.time() < self.frei_bis:
                    self.trennen()
                continue
            if not self.verbunden:
                with self.lock:
                    try:
                        self._verbinden()
                        pause = 5
                    except RuntimeError:
                        pass
                if not self.verbunden:
                    time.sleep(pause)
                    pause = min(60, pause * 2)
                continue
            if time.time() - self.zuletzt_info > 60:  # Rolle, RFID usw. – zugleich Lebenszeichen
                self.zuletzt_info = time.time()
                fragen = INFO_FRAGEN
                if AKKU_BEFEHL:
                    fragen += b"SSSGET" + AKKU_BEFEHL.encode() + b"\r\n"
                elif not getattr(self, "akku_probiert", False):
                    self.akku_probiert = True
                    fragen += b"".join(b"SSSGET" + k.encode() + b"\r\n" for k in AKKU_KANDIDATEN)
                try:
                    with self.lock:
                        self.senden(fragen)
                except (RuntimeError, OSError):
                    pass
                signal_messen()
                self.zuletzt_signal = time.time()
                continue
            if time.time() - getattr(self, "zuletzt_signal", 0) >= 10:  # Funksignal alle 10 s
                self.zuletzt_signal = time.time()
                signal_messen()
            if time.time() - self.zuletzt_gesendet > 30:  # Lebenszeichen
                try:
                    with self.lock:
                        self.senden(b"SSSGETPRINTING\r\n")
                except (RuntimeError, OSError):
                    pass


VERBINDUNG = Verbindung()


def arbeiter():
    while True:
        art, auftrag, titel = AUFTRAEGE.get()
        zeit = time.strftime("%H:%M:%S")
        try:
            antworten = VERBINDUNG.drucken(auftrag)
            ok, text = True, "%s gedruckt (%d Bytes)" % (art, len(auftrag))
        except Exception as e:  # noqa: BLE001 – Dienst soll weiterlaufen
            ok, text, antworten = False, str(e), ""
        VERLAUF.neu(titel, ok, "gedruckt" if ok else text)
        log.info("%s: %s %s", zeit, text, antworten.replace("\r\n", " | "))
        with LOCK:
            ZUSTAND.update(letzter=text, ok=ok, antworten=antworten, zeit=zeit)


# ------------------------------------------------------------------ HTTP
SEITE = """<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>FilamentLager Druckbrücke</title>
<style>
:root{--bg:#0d0f12;--panel:#15181c;--linie:#23272d;--rand:#2a2f36;--rand2:#3a4048;--text:#f1f3f5;--sub:#a3aab3;
--gruen:#4caf50;--gruen-t:#9fe3a2;--orange:#ffb74d;--rot:#ff8a80;color-scheme:dark}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Montserrat,system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:18px 16px 28px;display:flex;flex-direction:column;gap:12px}
.kopf{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px}
h1{font-size:20px;font-weight:600;margin:0}
.pill{padding:4px 12px;border-radius:999px;font-size:13px;background:var(--linie);color:var(--sub)}
.pill.an{background:#1e3024;color:var(--gruen-t)}.pill.aus{background:#3a1f1f;color:var(--rot)}.pill.frei{background:#3a2e1a;color:var(--orange)}
.kacheln{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px}
.k{background:var(--panel);border:2px solid var(--rand);border-radius:16px;padding:12px 14px;min-width:0}
.ue{font-size:11px;letter-spacing:.08em;color:var(--sub);text-transform:uppercase}
.gross{font-size:26px;font-weight:600;margin-top:4px;font-variant-numeric:tabular-nums}.gross small{font-size:14px;color:var(--sub);font-weight:500}
.balken{height:6px;background:var(--linie);border-radius:3px;margin-top:8px;overflow:hidden}.balken div{height:100%;background:var(--gruen)}
.klein{font-size:12px;color:var(--sub);margin-top:6px}
.striche{display:flex;gap:3px;align-items:flex-end;margin-top:8px;height:18px}.striche i{width:6px;border-radius:2px;background:var(--rand2)}.striche i.an{background:var(--gruen)}
.zeile{display:flex;justify-content:space-between;gap:10px;font-size:13px;padding:6px 0;border-bottom:1px solid var(--linie)}.zeile:last-child{border:0}
.ok{color:var(--gruen-t)}.fehler{color:var(--rot)}
.fuss{display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px;font-size:12px;color:var(--sub)}
button{font:inherit;font-size:14px;padding:9px 15px;border-radius:10px;cursor:pointer}
.primaer{background:var(--gruen);color:var(--bg);border:0;font-weight:600}.rahmen{background:none;color:var(--text);border:2px solid var(--rand2)}
pre{color:var(--sub);font-size:12px;white-space:pre-wrap;margin:0}
</style>
<main>
 <div class="kopf"><h1>Druckbrücke · M20 Pro</h1><span id="verb" class="pill">lade …</span></div>
 <div class="kacheln">
  <div class="k"><div class="ue">Etikettenrolle</div><div class="gross" id="rolle">–</div>
   <div class="balken"><div id="rolle-b" style="width:0"></div></div><div class="klein" id="rolle-t"></div></div>
  <div class="k" id="akku-k" hidden><div class="ue">Akku</div><div class="gross" id="akku">–</div><div class="klein" id="akku-t"></div></div>
  <div class="k"><div class="ue">Funksignal</div><div class="gross" id="sig">–</div>
   <div class="striche" id="sig-s"><i style="height:6px"></i><i style="height:10px"></i><i style="height:14px"></i><i style="height:18px"></i></div>
   <div class="klein" id="sig-t"></div></div>
  <div class="k"><div class="ue">Gedruckt</div><div class="gross" id="dr">–</div><div class="klein" id="dr-t"></div></div>
 </div>
 <div class="k"><div class="ue" style="margin-bottom:4px">Letzte Drucke</div><div id="verlauf"><span class="klein">noch keine</span></div></div>
 <div class="fuss"><span id="geraet"></span><span id="ver"></span></div>
 <div style="display:flex;gap:8px;flex-wrap:wrap"><button class="primaer" id="t">Testdruck</button>
  <button class="rahmen" id="f">Für Phomemo-App freigeben (10 min)</button></div>
 <details><summary class="klein">Antworten des Druckers beim letzten Druck</summary><pre id="a"></pre></details>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
async function lade(){
 let z; try{z=await (await fetch('/status',{cache:'no-store'})).json();}catch(e){$('verb').textContent='Druckbrücke nicht erreichbar';$('verb').className='pill aus';return;}
 const v=$('verb');
 if(z.verbunden){v.textContent='verbunden seit '+z.verbunden_seit;v.className='pill an';}
 else if(z.freigegeben_s){v.textContent='für die App freigegeben · noch '+Math.ceil(z.freigegeben_s/60)+' min';v.className='pill frei';}
 else{v.textContent='nicht verbunden';v.className='pill aus';v.title=z.verbindungsfehler||'';}
 const r=z.rolle||{};
 if(r.rest!=null){$('rolle').innerHTML=r.rest+(r.gesamt?' <small>von '+r.gesamt+'</small>':'');
  const p=r.gesamt?Math.round(r.rest*100/r.gesamt):0;$('rolle-b').style.width=p+'%';
  $('rolle-b').style.background=p<10?'var(--rot)':p<25?'var(--orange)':'var(--gruen)';}
 $('rolle-t').textContent=r.rfid?'RFID '+r.rfid:'';
 const ak=z.akku||{};$('akku-k').hidden=!ak.wert;
 if(ak.wert){$('akku').textContent=ak.wert+(/^\\d+$/.test(ak.wert)&&+ak.wert<=100?' %':'');$('akku-t').textContent='über SSSGET'+ak.befehl;}
 const s=z.signal||{};$('sig').textContent=s.text||'–';
 const stufe=s.rssi==null?0:s.rssi>=-3?4:s.rssi>=-8?3:s.rssi>=-15?2:1;
 [...$('sig-s').children].forEach((i,n)=>i.className=n<stufe?'an':'');
 const d=z.drucke||{};
 $('sig-t').textContent=(s.rssi!=null?s.rssi+' dB · ':'')+(d.abbrueche_heute||0)+' Abbrüche heute';
 $('dr').innerHTML=(d.heute??0)+' <small>heute</small>';
 $('dr-t').textContent=(d.gesamt??0)+' insgesamt'+(d.fehler_heute?' · '+d.fehler_heute+' Fehler heute':'');
 $('verlauf').innerHTML=(d.letzte||[]).length?(d.letzte||[]).map(e=>'<div class="zeile"><span>'+esc(e.zeit.slice(11,16))+' · '+esc(e.titel)+
  '</span><span class="'+(e.ok?'ok':'fehler')+'">'+esc(e.ok?'gedruckt':e.text)+'</span></div>').join(''):'<span class="klein">noch keine</span>';
 const g=z.geraet||{};
 $('geraet').textContent=[g.typ,g.firmware&&'Firmware '+g.firmware,g.seriennummer&&'SN '+g.seriennummer].filter(Boolean).join(' · ');
 $('ver').textContent='Druckbrücke '+z.version;
 $('a').textContent=z.antworten||'';
}
$('t').onclick=()=>fetch('/test',{method:'POST'}).then(lade);
$('f').onclick=()=>fetch('/freigeben',{method:'POST'}).then(lade);
lade();setInterval(lade,3000);
</script>"""


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
        if self.path.split("?")[0] == "/":
            body = SEITE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            return self.wfile.write(body)
        if self.path.split("?")[0] == "/status":
            with LOCK:
                z = dict(ZUSTAND)
            frei = max(0, int(VERBINDUNG.frei_bis - time.time()))

            def zahl(k):
                try:
                    return int(INFO.get(k, ""))
                except ValueError:
                    return None
            return self._json({"version": VERSION, "drucker": CFG.get("drucker", ""),
                               "verbunden": VERBINDUNG.verbunden, "verbunden_seit": VERBINDUNG.seit,
                               "dauerhaft": bool(CFG.get("dauerhaft", True)), "freigegeben_s": frei,
                               "verbindungsfehler": VERBINDUNG.fehler,
                               "rolle": {"rest": zahl("PAPERCAPACITY"), "gesamt": zahl("PAPERTOTALITY"),
                                         "rfid": INFO.get("RFID", "")},
                               "geraet": {"typ": INFO.get("TYPE", ""), "firmware": INFO.get("VERSION", ""),
                                          "seriennummer": INFO.get("SN", ""), "stand": INFO.get("zeit", "")},
                               "akku": {"wert": INFO.get(AKKU_BEFEHL, "") if AKKU_BEFEHL else "",
                                        "befehl": AKKU_BEFEHL or ""},
                               "signal": {"rssi": SIGNAL["rssi"], "text": signal_text(SIGNAL["rssi"]),
                                          "stand": SIGNAL["zeit"]},
                               "drucke": VERLAUF.zusammenfassung(), **z})
        self._json({"ok": False, "text": "nicht gefunden"}, 404)

    def do_POST(self):
        pfad = self.path.split("?")[0]
        laenge = int(self.headers.get("Content-Length") or 0)
        if laenge > 2 * 1024 * 1024:
            return self._json({"ok": False, "text": "zu groß"}, 413)
        roh = self.rfile.read(laenge)
        try:
            if pfad == "/freigeben":
                VERBINDUNG.frei_bis = time.time() + 600
                VERBINDUNG.trennen()
                return self._json({"ok": True, "text": "Drucker für 10 Minuten freigegeben (Phomemo-App)"})
            if pfad == "/test":
                auftrag, art, titel = TEST, "Test", "Testdruck"
            elif pfad == "/drucken":
                d = json.loads(roh or b"{}")
                breite, hoehe = int(d["breite"]), int(d["hoehe"])
                daten = base64.b64decode(d["daten"])
                if breite <= 0 or breite % 8 or hoehe <= 0 or len(daten) != breite // 8 * hoehe:
                    raise ValueError("Etikett unvollständig")
                auftrag = tspl_auftrag(breite, hoehe, daten, d.get("dichte", 10), d.get("kopien", 1),
                                       int(CFG.get("dpi", 300)))
                art = "Etikett"
                titel = str(d.get("titel") or "Etikett")[:80]
            else:
                return self._json({"ok": False, "text": "nicht gefunden"}, 404)
        except (KeyError, ValueError, TypeError) as e:
            return self._json({"ok": False, "text": "Etikett ungültig (%s)" % e}, 400)
        try:
            AUFTRAEGE.put_nowait((art, auftrag, titel))
        except queue.Full:
            return self._json({"ok": False, "text": "Drucker ist noch beschäftigt"}, 503)
        self._json({"ok": True, "text": "Druckauftrag angenommen"}, 202)


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    threading.Thread(target=arbeiter, daemon=True).start()
    threading.Thread(target=VERBINDUNG.waechter, daemon=True).start()
    port = int(CFG.get("port", 8765))
    log.info("FilamentLager-Druckbrücke %s auf Port %d, Drucker %s", VERSION, port, CFG.get("drucker") or "–")
    Server(("0.0.0.0", port), Handler).serve_forever()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--test-tspl":  # Selbsttest ohne Drucker
        a = tspl_auftrag(320, 240, bytes([0xF0] * 40 * 240))
        print(a[:200], len(a))
    else:
        main()
