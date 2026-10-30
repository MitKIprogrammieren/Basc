"""Datenmodell für das Filamentlager „Ernsthaft gelagert“ (todo.filament).

Gleiche Regeln wie firmware/display-5inch/src/model.cpp – Änderungen an beiden Stellen
nachziehen. Jeder Todo-Eintrag ist eine Sorte, die Daten stehen als JSON in der
Beschreibung. Die Waage ergänzt nur die Felder "nfc", "waage" und optional "leer_g";
alle anderen Felder bleiben unverändert.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field

LEER_GRENZE_G = 10
VOLL_TOLERANZ_G = 15
MESS_TOLERANZ_G = 5


def _int(v, standard):
    if isinstance(v, bool):
        return standard
    if isinstance(v, (int, float)):
        return int(round(v))
    if isinstance(v, str) and v.strip():
        try:
            return int(float(v.replace(",", ".")))
        except ValueError:
            return standard
    return standard


def _reste(v, gewicht):
    out = []
    if isinstance(v, list):
        for x in v:
            g = _int(x, -1)
            if g > 0 and (gewicht <= 0 or g < gewicht):
                out.append(g)
    return out


def _zahl(v, standard=-1.0):
    if isinstance(v, bool):
        return standard
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str) and v.strip():
        try:
            return float(v.replace(",", "."))
        except ValueError:
            return standard
    return standard


def hex_zu_rgb(h, standard="#888888"):
    if isinstance(h, str) and len(h) == 7 and h[0] == "#":
        try:
            int(h[1:], 16)
            return h.lower()
        except ValueError:
            pass
    return standard


# Zusätzliche Werte, die SchichtLohn pro Sorte speichert – nur durchgereicht für die Etikett-Zeilen
DRUCKPROFIL = ("mvs", "k", "kammer", "schwund", "buegeln_tempo", "buegeln_flow", "ams", "ams_modelle", "sku", "sl_id")


@dataclass
class Sorte:
    uid: str
    summary: str
    beschreibung: str
    hersteller: str = ""
    material: str = ""
    farbe: str = ""
    hex: str = "#888888"
    besonderheiten: list = field(default_factory=list)
    gewicht: int = 1000
    spulen: int = 1
    refills: int = 0
    reste: list = field(default_factory=list)
    duese_min: int = -1
    duese_max: int = -1
    bett_min: int = -1
    bett_max: int = -1
    flow: float = -1.0
    td: float = -1.0
    trocknen_c: int = -1
    trocknen_h: int = -1
    durchmesser: float = 1.75
    leer_g: int = -1
    fcode: str = ""
    bambu_id: str = ""
    ort: str = ""
    getrocknet: str = ""
    codes: list = field(default_factory=list)
    nfc: list = field(default_factory=list)
    druckprofil: dict = field(default_factory=dict)  # Werte für die Etikett-Zeilen (aus SchichtLohn)

    @property
    def kurzname(self):
        s = " ".join(x for x in (self.material, self.farbe) if x)
        return s or self.summary

    @property
    def bestand_g(self):
        return max(0, self.spulen - len(self.reste)) * self.gewicht + sum(self.reste)

    def als_dict(self):
        return {
            "uid": self.uid, "summary": self.summary, "hersteller": self.hersteller,
            "material": self.material, "farbe": self.farbe, "hex": self.hex,
            "kurzname": self.kurzname, "besonderheiten": self.besonderheiten,
            "gewicht": self.gewicht, "spulen": self.spulen, "refills": self.refills,
            "reste": self.reste, "duese_min": self.duese_min, "duese_max": self.duese_max,
            "bett_min": self.bett_min, "bett_max": self.bett_max, "flow": self.flow, "td": self.td,
            "trocknen_c": self.trocknen_c, "trocknen_h": self.trocknen_h,
            "durchmesser": self.durchmesser, "leer_g": self.leer_g, "fcode": self.fcode,
            "bambu_id": self.bambu_id, "ort": self.ort, "getrocknet": self.getrocknet, "codes": self.codes, "nfc": self.nfc,
            "bestand_g": self.bestand_g, **self.druckprofil,
        }


def _lesen(beschreibung):
    if not beschreibung:
        return {}
    try:
        d = json.loads(beschreibung)
    except (ValueError, TypeError):
        return None
    return d if isinstance(d, dict) else None


def _schreiben(d):
    return json.dumps(d, ensure_ascii=False, separators=(",", ":"))


def sorte_aus_eintrag(uid, summary, beschreibung):
    """Gibt None zurück für Merkzeilen und Einstellungen der Karte."""
    d = _lesen(beschreibung)
    if d is None:
        d = {}
    if d.get("typ"):
        return None
    gewicht = _int(d.get("gewicht"), 1000)
    if gewicht <= 0:
        gewicht = 1000
    spulen = max(0, _int(d.get("spulen"), 1))
    s = Sorte(uid=uid, summary=summary, beschreibung=beschreibung or "")
    s.hersteller = str(d.get("hersteller") or "")
    s.material = str(d.get("material") or "")
    s.farbe = str(d.get("farbe") or "")
    if not s.material and not s.farbe:
        s.farbe = summary
    s.hex = hex_zu_rgb(d.get("hex"))
    s.besonderheiten = [str(b) for b in d.get("besonderheiten") or [] if isinstance(b, str)]
    s.gewicht = gewicht
    s.spulen = spulen
    s.reste = _reste(d.get("reste"), gewicht)
    s.refills = max(0, min(_int(d.get("refills"), 0), spulen))
    s.duese_min = _int(d.get("duese_min"), -1)
    s.duese_max = _int(d.get("duese_max"), -1)
    s.bett_min = _int(d.get("bett_min"), -1)
    s.bett_max = _int(d.get("bett_max"), -1)
    s.flow = _zahl(d.get("flow"))
    s.td = _zahl(d.get("td"))
    s.trocknen_c = _int(d.get("trocknen_c"), -1)
    s.trocknen_h = _int(d.get("trocknen_h"), -1)
    s.leer_g = _int(d.get("leer_g"), -1)
    if isinstance(d.get("durchmesser"), (int, float)):
        s.durchmesser = float(d["durchmesser"])
    s.fcode = str(d.get("fcode") or "")
    s.bambu_id = str(d.get("bambu_id") or "")
    s.ort = str(d.get("ort") or "")
    s.getrocknet = str(d.get("getrocknet") or "")
    s.codes = [str(c) for c in d.get("codes") or [] if isinstance(c, str)]
    s.nfc = [str(c) for c in d.get("nfc") or [] if isinstance(c, str)]
    s.druckprofil = {k: d[k] for k in DRUCKPROFIL if isinstance(d.get(k), (bool, int, float, str)) and d.get(k) != ""}
    return s


def lager_aus_antwort(antwort, entity):
    """Antwort von todo.get_items (return_response) → Liste von Sorten."""
    items = antwort.get("service_response", {}).get(entity, {}).get("items")
    if items is None:
        raise ValueError(f"keine Einträge für {entity} in der Antwort")
    out = []
    for it in items:
        if it.get("status") == "completed":
            continue
        s = sorte_aus_eintrag(it.get("uid", ""), it.get("summary", ""), it.get("description", ""))
        if s:
            out.append(s)
    return out


def finde_nfc(sorten, tag):
    if not tag:
        return None
    return next((s for s in sorten if tag in s.nfc), None)


def finde_uid(sorten, uid):
    return next((s for s in sorten if s.uid == uid), None)


@dataclass
class Aenderung:
    geaendert: bool = False
    beschreibung: str = ""
    meldung: str = ""


def _naechster(reste, ziel):
    if not reste:
        return -1
    return min(range(len(reste)), key=lambda i: abs(reste[i] - ziel))


def rest_speichern(beschreibung, netto, tag=""):
    d = _lesen(beschreibung)
    if d is None:
        return Aenderung(meldung="Eintrag nicht lesbar")
    gewicht = _int(d.get("gewicht"), 1000)
    if gewicht <= 0:
        gewicht = 1000
    spulen = max(0, _int(d.get("spulen"), 1))
    reste = _reste(d.get("reste"), gewicht)
    waage = d.get("waage") if isinstance(d.get("waage"), dict) else {}
    alt = _int(waage.get(tag), -1) if tag else -1
    a = Aenderung()

    if netto >= gewicht - VOLL_TOLERANZ_G:
        if alt > 0:
            i = _naechster(reste, alt)
            if i >= 0:
                reste.pop(i)
            waage.pop(tag, None)
            a.geaendert, a.meldung = True, "Spule ist voll – Rest gestrichen"
        else:
            return Aenderung(meldung="Spule ist voll – nichts geändert")
    elif netto <= LEER_GRENZE_G:
        i = _naechster(reste, alt) if alt > 0 else -1
        if i < 0 and reste:
            i = reste.index(min(reste))
        if i >= 0:
            reste.pop(i)
        spulen = max(0, spulen - 1)
        d["spulen"] = spulen
        if _int(d.get("refills"), 0) > spulen:
            d["refills"] = spulen
        if tag:
            waage.pop(tag, None)
        a.geaendert, a.meldung = True, "Spule leer – ausgebucht"
    else:
        i = _naechster(reste, alt) if alt > 0 else -1
        if i < 0:
            kandidaten = [k for k, r in enumerate(reste) if r >= netto - MESS_TOLERANZ_G]
            if kandidaten:
                i = min(kandidaten, key=lambda k: reste[k])
        if i < 0 and len(reste) < spulen:
            reste.append(netto)
            a.meldung = f"Neue angebrochene Spule: {netto} g"
        else:
            if i < 0:
                i = _naechster(reste, netto)
            if i < 0:
                spulen += 1
                d["spulen"] = spulen
                reste.append(netto)
                a.meldung = f"Spule ergänzt: {netto} g"
            else:
                a.meldung = f"Rest {reste[i]} g → {netto} g"
                reste[i] = netto
        if tag:
            waage[tag] = netto
        a.geaendert = True

    d["reste"] = reste
    if waage:
        d["waage"] = waage
    else:
        d.pop("waage", None)
    a.beschreibung = _schreiben(d)
    return a


def tag_zuordnen(beschreibung, tag):
    d = _lesen(beschreibung)
    if d is None or not tag:
        return Aenderung(meldung="Eintrag nicht lesbar")
    nfc = d.get("nfc") if isinstance(d.get("nfc"), list) else []
    if tag in nfc:
        return Aenderung(meldung="Tag war schon zugeordnet")
    d["nfc"] = nfc + [tag]
    return Aenderung(True, _schreiben(d), "Tag gespeichert")


def tag_entfernen(beschreibung, tag):
    d = _lesen(beschreibung)
    if d is None:
        return Aenderung(meldung="Eintrag nicht lesbar")
    nfc = d.get("nfc") if isinstance(d.get("nfc"), list) else []
    if tag not in nfc:
        return Aenderung()
    nfc = [x for x in nfc if x != tag]
    if nfc:
        d["nfc"] = nfc
    else:
        d.pop("nfc", None)
    if isinstance(d.get("waage"), dict):
        d["waage"].pop(tag, None)
        if not d["waage"]:
            d.pop("waage")
    return Aenderung(True, _schreiben(d), "Tag entfernt")


def spulen_aendern(beschreibung, delta):
    d = _lesen(beschreibung)
    if d is None:
        return Aenderung(meldung="Eintrag nicht lesbar")
    gewicht = _int(d.get("gewicht"), 1000)
    spulen = max(0, _int(d.get("spulen"), 1))
    neu = max(0, spulen + delta)
    if neu == spulen:
        return Aenderung(meldung="Keine Spule mehr da")
    d["spulen"] = neu
    reste = _reste(d.get("reste"), gewicht)
    if len(reste) > neu:
        reste.sort()
        reste.pop(0)
        d["reste"] = reste
    if _int(d.get("refills"), 0) > neu:
        d["refills"] = neu
    return Aenderung(True, _schreiben(d), f"{neu} Spule" + ("" if neu == 1 else "n"))


def feld_setzen(beschreibung, feld, wert):
    """Lagerort ("ort") oder Trocknungsdatum ("getrocknet", JJJJ-MM-TT) setzen; leer = löschen."""
    if feld not in ("ort", "getrocknet"):
        return Aenderung(meldung="Unbekanntes Feld")
    wert = str(wert or "").strip()
    if len(wert) > 60:
        return Aenderung(meldung="Text zu lang")
    if feld == "getrocknet" and wert:
        import re
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", wert):
            return Aenderung(meldung="Datum ungültig")
    d = _lesen(beschreibung)
    if d is None:
        return Aenderung(meldung="Eintrag nicht lesbar")
    if str(d.get(feld) or "") == wert:
        return Aenderung(meldung="Unverändert")
    if wert:
        d[feld] = wert
    else:
        d.pop(feld, None)
    if feld == "ort":
        text = "Lagerort gespeichert" if wert else "Lagerort entfernt"
    else:
        text = "Als getrocknet vermerkt" if wert else "Trocknungsdatum entfernt"
    return Aenderung(True, _schreiben(d), text)
