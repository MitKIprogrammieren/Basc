"""Was auf der Sensorplatine die Firmware erledigt, auf dem Pi: Einstellungen der Oberfläche,
WLAN des Pi (NetworkManager), Home-Assistant-Zugang in der config.toml und die AMS-Belegung
der Bambu-Drucker aus Home Assistant.
"""
from __future__ import annotations

import json
import logging
import os
import re
import subprocess
import threading

log = logging.getLogger("system")

ERLAUBT = ("akzent", "helligkeit", "display_aus_min", "kompakt", "leerspule_g", "nfc_stufe", "drucker", "ams_suche",
           "textgroesse_kiosk")


# ------------------------------------------------------------ Einstellungen der Oberfläche
class Einstellungen:
    def __init__(self, ordner: str, leerspule_g: int = 250):
        self.pfad = os.path.join(ordner, "einstellungen.json")
        self.lock = threading.Lock()
        self.werte = {}
        try:
            with open(self.pfad, encoding="utf-8") as f:
                self.werte = json.load(f) or {}
        except (OSError, ValueError):
            pass
        self._standard(leerspule_g)

    def _standard(self, leerspule_g=250):
        w = self.werte
        w.setdefault("akzent", "#4caf50")
        w.setdefault("helligkeit", 100)
        w.setdefault("display_aus_min", 0)
        w.setdefault("kompakt", False)
        w.setdefault("leerspule_g", int(leerspule_g))
        w.setdefault("nfc_stufe", 3)
        if not isinstance(w.get("drucker"), list):
            w["drucker"] = []
        w.setdefault("ams_suche", False)

    def alle(self):
        with self.lock:
            return json.loads(json.dumps(self.werte))

    def setzen(self, d: dict):
        with self.lock:
            for k in ERLAUBT:
                if k in d and d[k] is not None:
                    self.werte[k] = d[k]
            self._standard()
            tmp = self.pfad + ".neu"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(self.werte, f, ensure_ascii=False, indent=1)
            os.replace(tmp, self.pfad)

    def zuruecksetzen(self):
        with self.lock:
            self.werte = {}
            self._standard()
            try:
                os.remove(self.pfad)
            except OSError:
                pass


# ------------------------------------------------------------ Netz des Pi
def netz_art():
    """'wlan', 'lan' oder '' – über welche Schnittstelle der Pi ins Netz geht (Standardroute)."""
    try:
        with open("/proc/net/route", encoding="ascii") as f:
            for zeile in f.readlines()[1:]:
                t = zeile.split()
                if len(t) > 2 and t[1] == "00000000":
                    return "wlan" if t[0].startswith("wl") else "lan"
    except OSError:
        pass
    return ""


def _nmcli(*args, timeout=20):
    try:
        r = subprocess.run(["nmcli", *args], capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        return 1, "", str(e)
    return r.returncode, r.stdout, r.stderr.strip()


def _felder(zeile):
    """nmcli -t trennt mit ':' und schreibt ':' im Inhalt als '\\:'."""
    return [x.replace("\\:", ":") for x in re.split(r"(?<!\\):", zeile)]


def wlan_status():
    code, out, _ = _nmcli("-t", "-f", "ACTIVE,SSID,SIGNAL", "dev", "wifi", timeout=8)
    if code:
        return {}
    for z in out.splitlines():
        f = _felder(z)
        if len(f) >= 3 and f[0] == "yes" and f[1]:
            st = {"ssid": f[1], "signal": int(f[2]) if f[2].isdigit() else None}
            c2, o2, _ = _nmcli("-t", "-f", "IP4.ADDRESS", "dev", "show", "wlan0", timeout=5)
            m = re.search(r"IP4\.ADDRESS\[1\]:([\d.]+)", o2) if not c2 else None
            st["ip"] = m.group(1) if m else ""
            return st
    return {}


def wlan_netze():
    code, out, err = _nmcli("-t", "-f", "SSID,SIGNAL,SECURITY", "dev", "wifi", "list", "--rescan", "yes")
    if code:
        log.warning("WLAN-Suche: %s", err)
        return []
    netze = {}
    for z in out.splitlines():
        f = _felder(z)
        if len(f) < 3 or not f[0]:
            continue
        sig = int(f[1]) if f[1].isdigit() else 0
        if f[0] not in netze or netze[f[0]]["signal"] < sig:
            netze[f[0]] = {"ssid": f[0], "signal": sig, "gesichert": f[2] not in ("", "--")}
    return sorted(netze.values(), key=lambda n: -n["signal"])


def wlan_verbinden(ssid, passwort):
    args = ["dev", "wifi", "connect", ssid] + (["password", passwort] if passwort else [])
    code, out, err = _nmcli(*args, timeout=45)
    if code:
        if "not authorized" in err.lower() or "insufficient privileges" in err.lower():
            return False, "Keine Berechtigung – einmal sudo ./install.sh ausführen (Gruppe netdev)"
        return False, "WLAN: " + (err or out or "Verbindung fehlgeschlagen")
    return True, f"Mit {ssid} verbunden"


# ------------------------------------------------------------ config.toml
def toml_setzen(pfad, abschnitt, werte: dict):
    """Setzt Schlüssel in einem Abschnitt der config.toml, ohne Kommentare und Rest zu verlieren."""
    with open(pfad, encoding="utf-8") as f:
        zeilen = f.read().splitlines()
    kopf = f"[{abschnitt}]"
    try:
        start = next(i for i, z in enumerate(zeilen) if z.strip() == kopf)
    except StopIteration:
        zeilen += ["", kopf]
        start = len(zeilen) - 1
    ende = next((i for i in range(start + 1, len(zeilen)) if zeilen[i].lstrip().startswith("[")), len(zeilen))
    for k, v in werte.items():
        neu = f"{k} = {json.dumps(v, ensure_ascii=False)}"
        for i in range(start + 1, ende):
            if re.match(rf"\s*{re.escape(k)}\s*=", zeilen[i]):
                zeilen[i] = neu
                break
        else:
            zeilen.insert(start + 1, neu)
            ende += 1
    # in derselben Datei schreiben: /etc/filamentwaage gehört root, die Datei dem Benutzer
    with open(pfad, "r+", encoding="utf-8") as f:
        f.seek(0)
        f.write("\n".join(zeilen) + "\n")
        f.truncate()


# ------------------------------------------------------------ AMS aus Home Assistant
AMS_VORLAGE = (
    "{% set ns = namespace(s=[], d=[]) %}"
    "{% for s in states.sensor if s.entity_id is search('_(ams_\\d+_slot_\\d+|externalspool\\d*_externe_spule)$')"
    " or s.entity_id is search('_ams_\\d+_(luftfeuchtigkeit|index_der_luftfeuchtigkeit)(_\\d+)?$') %}"
    "{% set a = s.attributes %}"
    "{% set ns.s = ns.s + [{'e': s.entity_id, 's': s.state, 'name': a.get('name'), 'color': a.get('color'),"
    " 'fid': a.get('filament_id'), 'empty': a.get('empty'), 'active': a.get('active'), 'remain': a.get('remain'),"
    " 'tag': a.get('tag_uid'), 'type': a.get('type')}] %}{% endfor %}"
    "{% for s in states.sensor if s.entity_id is search('^sensor\\.[^_]+_[^_]+_druckstatus$') %}"
    "{% set ns.d = ns.d + [{'p': s.entity_id[7:-12], 'name': device_attr(device_id(s.entity_id), 'name_by_user')"
    " or '', 'online': s.state not in ['unavailable', 'unknown']}] %}{% endfor %}"
    "{{ {'slots': ns.s, 'drucker': ns.d,"
    " 'abbuchung': states('automation.ernsthaft_gelagert_abbuchung_nach_druckende')} | to_json }}"
)
_SLOT = re.compile(r"^sensor\.(.+?)_ams_(\d+)_slot_(\d+)$")
_EXT = re.compile(r"^sensor\.(.+?)_externalspool(\d*)_externe_spule$")
_FEUCHTE = re.compile(r"^sensor\.(.+?)_ams_(\d+)_(luftfeuchtigkeit|index_der_luftfeuchtigkeit)(_\d+)?$")


def _drucker_id(teil):
    """Modell_Seriennummer – auch bei doppeltem Präfix wie „h2c_h2c_31b8…“."""
    return "_".join(teil.split("_")[-2:])
_MODELLE = {"h2c": "H2C", "h2s": "H2S", "h2d": "H2D", "x1c": "X1C", "x1e": "X1E", "p1s": "P1S", "p1p": "P1P",
            "p2s": "P2S", "a1": "A1", "a1mini": "A1 mini"}


def _ams_kurz(nr):
    return f"HT{nr - 127}" if nr >= 128 else ("ABCDEFGH"[nr - 1] if 1 <= nr <= 8 else str(nr))


def ams_aufbereiten(daten: dict) -> dict:
    """Template-Antwort → {"drucker": [{id, name, online, ams: [{name, kurz, feuchte, slots: [...]}]}]}"""
    drucker = {}
    for d in daten.get("drucker") or []:
        drucker[d["p"]] = {"id": d["p"], "name": d.get("name") or "", "online": bool(d.get("online")), "ams": {}}
    # Namen ohne eigenen Namen: Modell, bei mehreren gleichen mit Nummer
    gleiche = {}
    for p in sorted(drucker):
        gleiche.setdefault(p.split("_")[0], []).append(p)
    for p, d in drucker.items():
        if not d["name"]:
            m = p.split("_")[0]
            liste = gleiche.get(m, [])
            d["name"] = _MODELLE.get(m, m.upper()) + (f" {liste.index(p) + 1}" if len(liste) > 1 else "")

    feuchte = {}
    for s in daten.get("slots") or []:
        m = _FEUCHTE.match(s["e"])
        if not m:
            continue
        try:
            v = float(s["s"])
        except (TypeError, ValueError):
            continue
        schluessel = (_drucker_id(m.group(1)), int(m.group(2)))
        # Prozent vor Index (1–5)
        if m.group(3) == "luftfeuchtigkeit" or schluessel not in feuchte:
            feuchte[schluessel] = round(v)

    for s in daten.get("slots") or []:
        m, x = _SLOT.match(s["e"]), _EXT.match(s["e"])
        if not (m or x):
            continue
        p = _drucker_id((m or x).group(1))
        zustand = str(s.get("s") or "").lower()
        leer = s.get("empty") is True or zustand in ("empty", "leer", "unavailable", "unknown", "", "?")
        if x and leer:
            continue  # leere externe Spulen nur zeigen, wenn etwas eingelegt ist
        d = drucker.setdefault(p, {"id": p, "name": p, "online": False, "ams": {}})
        if m:
            nr, fach = int(m.group(2)), int(m.group(3))
            a = d["ams"].setdefault(nr, {"name": f"AMS HT {nr - 127}" if nr >= 128 else f"AMS {_ams_kurz(nr)}",
                                         "kurz": _ams_kurz(nr), "feuchte": feuchte.get((p, nr)), "slots": [],
                                         "_nr": nr})
            slot = "" if nr >= 128 else fach
        else:
            a = d["ams"].setdefault(999, {"name": "Externe Spule", "kurz": "Ext", "feuchte": None, "slots": [],
                                          "_nr": 999})
            slot = x.group(2) or ""
        farbe = str(s.get("color") or "#888888")
        rest = s.get("remain")
        a["slots"].append({
            "slot": slot, "leer": leer, "hex": farbe[:7] if farbe.startswith("#") else "#" + farbe[:6],
            "name": s.get("name") or s.get("type") or "", "rest": rest if isinstance(rest, (int, float)) else -1,
            "aktiv": bool(s.get("active")), "tag": "" if not (s.get("tag") or "").strip("0") else s.get("tag"), "filament_id": s.get("fid") or "",
            "_f": fach if m else 0,
        })
    aus = []
    for p in sorted(drucker, key=lambda k: drucker[k]["name"].lower()):
        d = drucker[p]
        ams = []
        for nr in sorted(d["ams"]):
            a = d["ams"][nr]
            a["slots"].sort(key=lambda s: s["_f"])
            for sl in a["slots"]:
                sl.pop("_f", None)
            a.pop("_nr", None)
            ams.append(a)
        d["ams"] = ams
        aus.append(d)
    return {"drucker": aus, "abbuchung_aktiv": daten.get("abbuchung") == "on"}


def abbuchungen(antwort, todo: str, anzahl=6):
    """Die letzten Einträge der Verbrauchsliste ("typ": "verbrauch") für die Oberfläche."""
    try:
        items = antwort["service_response"][todo]["items"] if "service_response" in antwort else antwort[todo]["items"]
    except (KeyError, TypeError):
        return []
    liste = []
    for it in items:
        try:
            d = json.loads(it.get("description") or "{}")
        except ValueError:
            continue
        if d.get("typ") != "verbrauch":
            continue
        z = str(d.get("zeit") or "")
        zeit = f"{z[8:10]}.{z[5:7]}. {z[11:16]}" if len(z) >= 16 else z
        g = d.get("gramm")
        gtext = f"{g:.1f}".replace(".", ",") + " g" if isinstance(g, (int, float)) else ""
        liste.append({"_z": z, "zeit": zeit, "text": " · ".join(x for x in (gtext, d.get("sorte") or "", d.get("ort") or "") if x)})
    liste.sort(key=lambda x: x["_z"], reverse=True)
    for x in liste:
        x.pop("_z")
    return liste[:anzahl]
