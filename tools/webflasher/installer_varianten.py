#!/usr/bin/env python3
"""Stellt die Geräteliste für den gemeinsamen Installer zusammen (Eingabe für bauen.py --varianten).

Aufruf:  installer_varianten.py SENSOR_ORDNER DISPLAY_ORDNER > varianten.json

SENSOR_ORDNER  = Inhalt des Branches webflasher-bin (Sensorplatine: Teile oben, N16R8 in n16r8/, version.json)
DISPLAY_ORDNER = Inhalt des Branches webflasher-display (je Display-Variante ein Ordner, version.json)
Fehlt eine Variante (z. B. noch nicht gebaut), wird sie ausgelassen.
"""
import json
import pathlib
import sys

# Reihenfolge = Reihenfolge im Installer; die erste ist vorausgewählt
DISPLAY = [
    ("sc01plus_direkt", "direkt", "Waage mit SC01 Plus – ohne Sensorplatine",
     "Wägezelle (NAU7802) und NFC-Leser hängen direkt am 3,5\"-Display WT32-SC01 Plus."),
    ("sc01plus", "anzeige", "Display SC01 Plus – mit Sensorplatine",
     "3,5\"-Display WT32-SC01 Plus, Wägezelle und NFC an einer eigenen Sensorplatine."),
    ("ws5", "anzeige", "Display 5\" (Waveshare) – mit Sensorplatine",
     "5\"-Display von Waveshare, Wägezelle und NFC an einer eigenen Sensorplatine."),
]
SENSOR = [
    ("n16r8", "n16r8", "Sensorplatine ESP32-S3-DevKitC (N16R8)",
     "ESP32-S3-Kit mit 16 MB Flash und 8 MB PSRAM – mit Web-Oberfläche, Wägezelle und NFC-Lesern."),
    ("s3zero", ".", "Sensorplatine ESP32-S3-Zero / Super Mini",
     "Kleine ESP32-S3-Platine ohne PSRAM – mit Web-Oberfläche, Wägezelle und NFC-Lesern."),
]


def teile(ordner: pathlib.Path, littlefs: str | None) -> list[str] | None:
    namen = [("0x0", "bootloader.bin"), ("0x8000", "partitions.bin"), ("0xe000", "boot_app0.bin"), ("0x10000", "firmware.bin")]
    if not all((ordner / n).is_file() for _, n in namen):
        return None
    t = [f"{a}={ordner / n}" for a, n in namen]
    if littlefs and (ordner / "littlefs.bin").is_file():
        t.append(f"{littlefs}={ordner / 'littlefs.bin'}")
    return t


def main() -> None:
    sensor, display = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    sv = json.loads((sensor / "version.json").read_text())
    dv = json.loads((display / "version.json").read_text())
    liste = []
    for ordner, art, name, text in DISPLAY:
        # Display-Firmware: Oberfläche im LittleFS nur, wo die Variante eine hat (Partition „spiffs“ ab 0xC90000)
        t = teile(display / ordner, "0xc90000")
        if t:
            liste.append({"id": ordner, "art": art, "name": name, "text": text, "version": dv["fw"], "teile": t})
    for vid, ordner, name, text in SENSOR:
        t = teile(sensor / ordner, sv.get("littlefs"))
        if t:
            liste.append({"id": vid, "art": "sensor", "name": name, "text": text, "version": sv["fw"], "teile": t})
    if not liste:
        raise SystemExit("keine Firmware-Teile gefunden")
    json.dump(liste, sys.stdout, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
