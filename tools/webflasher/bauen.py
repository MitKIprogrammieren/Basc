#!/usr/bin/env python3
"""Baut den Webflasher als eine einzige HTML-Datei (Firmware + esptool-js eingebettet, läuft offline).

    python3 tools/webflasher/bauen.py --version 2.0.0 --esptool esptool-js.mjs \
        --teil 0x0=bootloader.bin --teil 0x8000=partitions.bin --teil 0xe000=boot_app0.bin \
        --teil 0x10000=firmware.bin --teil 0x290000=littlefs.bin --aus FilamentLager-Webflasher.html

Die Teile werden einzeln geschrieben, nicht als ein Abbild ab 0x0: So bleibt der NVS-Bereich
(Kalibrierung, WLAN, Einstellungen) beim Aktualisieren erhalten.

Die Datei wird direkt in Chrome/Edge geöffnet (Doppelklick). Web Serial gibt es nur dort.
"""
import argparse
import base64
import gzip
import json
import pathlib
import re

HIER = pathlib.Path(__file__).resolve().parent


def esptool_als_inline(quelle: str) -> str:
    """Macht aus dem ESM-Bundle ein Inline-Skript, das ESPLoader und Transport an window hängt."""
    treffer = list(re.finditer(r"export\s*\{([^}]*)\}\s*;?", quelle))
    if not treffer:
        raise SystemExit("esptool-js: export-Anweisung nicht gefunden")
    letzter = treffer[-1]
    namen = {}
    for teil in letzter.group(1).split(","):
        teil = teil.strip()
        if not teil:
            continue
        lokal, _, aussen = teil.partition(" as ")
        namen[(aussen or lokal).strip()] = lokal.strip()
    for noetig in ("ESPLoader", "Transport"):
        if noetig not in namen:
            raise SystemExit(f"esptool-js: {noetig} wird nicht exportiert ({sorted(namen)})")
    ersatz = "window.__esptool={ESPLoader:%s,Transport:%s};" % (namen["ESPLoader"], namen["Transport"])
    code = quelle[: letzter.start()] + ersatz + quelle[letzter.end():]
    if re.search(r"^\s*(import|export)\s", code, re.M):
        raise SystemExit("esptool-js: weitere import/export-Anweisungen – Bundle passt nicht")
    return code.replace("</script", "<\\/script")


def teile_lesen(liste: list[str], packen: bool) -> list[dict]:
    """ADRESSE=DATEI → {adresse, name, b64} bzw. gzip-gepackt {adresse, name, gz} (Installer: kleiner, der Browser
    entpackt mit DecompressionStream)."""
    teile = []
    for t in liste:
        adresse, _, pfad = t.partition("=")
        daten = pathlib.Path(pfad).read_bytes()
        teil = {"adresse": int(adresse, 0), "name": pathlib.Path(pfad).name}
        if packen:
            teil["gz"] = base64.b64encode(gzip.compress(daten, 9, mtime=0)).decode()
        else:
            teil["b64"] = base64.b64encode(daten).decode()
        teile.append(teil)
        print(f"  {teil['adresse']:#08x}  {teil['name']}  {len(daten) // 1024} KB")
    if teile[0]["adresse"] != 0:
        raise SystemExit("Der erste Teil muss der Bootloader ab 0x0 sein")
    if pathlib.Path(liste[0].partition("=")[2]).read_bytes()[:1] != b"\xe9":
        raise SystemExit("Bootloader beginnt nicht mit einem ESP-Image-Kopf (0xE9)")
    return teile


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--version", default="", help="Firmware-Version für die Anzeige")
    p.add_argument("--esptool", required=True, help="bundle.js aus dem npm-Paket esptool-js (0.5.x)")
    p.add_argument("--varianten", metavar="DATEI.json",
                   help="Installer mit Auswahl: Liste [{id, name, text, art, version, teile: [\"ADRESSE=DATEI\", …]}] "
                        "statt --teil/--art/--version – alle Geräte in einer Datei, gzip-gepackt")
    p.add_argument("--teil", action="append", metavar="ADRESSE=DATEI",
                   help="z. B. 0x10000=firmware.bin; mehrfach angeben")
    p.add_argument("--aus", required=True, help="Ausgabedatei (.html)")
    p.add_argument("--art", choices=["sensor", "anzeige", "direkt"], default="sensor",
                   help="sensor = Sensorplatine (nur WLAN), anzeige = Display (WLAN, SchichtLohn-Server, Sensor-IP), "
                        "direkt = Display ohne Sensorplatine (WLAN, SchichtLohn-Server)")
    p.add_argument("--geraet", default="Sensorplatine", help="Name in der Kopfzeile, z. B. „Display SC01 Plus“")
    a = p.parse_args()
    varianten = None
    if a.varianten:
        varianten = []
        for v in json.loads(pathlib.Path(a.varianten).read_text(encoding="utf-8")):
            print(f"{v['name']} (FW {v['version']}):")
            varianten.append({k: v[k] for k in ("id", "name", "text", "art", "version")} |
                             {"teile": teile_lesen(v["teile"], packen=True)})
        teile, a.art, a.geraet = [], varianten[0]["art"], "Installer"
    else:
        if not a.teil or not a.version:
            raise SystemExit("--teil und --version angeben (oder --varianten)")
        teile = teile_lesen(a.teil, packen=False)
    html = (HIER / "vorlage.html").read_text(encoding="utf-8")
    html = html.replace("/*ESPTOOL_JS*/", esptool_als_inline(pathlib.Path(a.esptool).read_text(encoding="utf-8")))
    html = html.replace('"__TEILE__"', json.dumps(teile))
    html = html.replace('"__VARIANTEN__"', json.dumps(varianten))
    html = html.replace("__FW_VERSION__", a.version)
    html = html.replace("__ART__", a.art).replace("__GERAET__", a.geraet)
    pathlib.Path(a.aus).write_text(html, encoding="utf-8")
    print(f"{a.aus}: {len(html) // 1024} KB, Firmware {a.version}")


if __name__ == "__main__":
    main()
