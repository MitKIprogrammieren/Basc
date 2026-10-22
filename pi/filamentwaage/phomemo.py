"""Etiketten auf einem Phomemo-Etikettendrucker (M110/M120/M220/M20-Familie) per Bluetooth.

Das Etikett wird in der Oberfläche gezeichnet (gleiches Bild wie die Vorschau) und kommt hier
als 1-Bit-Raster an: Zeilen von links nach rechts, 8 Punkte je Byte, höchstes Bit zuerst,
1 = schwarz. Dieses Modul baut daraus die Druckbefehle und schickt sie an den Drucker.

Verbindungen:
- ble     Bluetooth Low Energy (Standard bei den Phomemo-Etikettendruckern), braucht python3-bleak
- rfcomm  klassisches Bluetooth (serielle Verbindung), nur Standardbibliothek
- datei   schreibt die Befehle in eine Datei – zum Testen ohne Drucker
"""
from __future__ import annotations

import asyncio
import logging
import socket
import time

log = logging.getLogger("etikett")

# GATT-Dienst und Schreib-Kennung der Phomemo-Drucker
DIENST = "0000ff00-0000-1000-8000-00805f9b34fb"
SCHREIBEN = "0000ff02-0000-1000-8000-00805f9b34fb"

PAPIER_LUECKE = 0x0A      # Etiketten mit Lücke (Standardrollen)
PAPIER_ENDLOS = 0x0B
PAPIER_MARKE = 0x26       # Etiketten mit schwarzer Markierung


class DruckFehler(Exception):
    pass


def befehle(breite_px: int, hoehe_px: int, daten: bytes, dichte: int = 10, tempo: int = 5,
            papier: int = PAPIER_LUECKE) -> bytes:
    """Druckauftrag im Format der Phomemo-Etikettendrucker (ESC/POS-Raster)."""
    if breite_px <= 0 or breite_px % 8:
        raise DruckFehler("Breite muss ein Vielfaches von 8 Punkten sein")
    bpr = breite_px // 8
    if len(daten) != bpr * hoehe_px:
        raise DruckFehler(f"Rasterdaten passen nicht: {len(daten)} statt {bpr * hoehe_px} Bytes")
    if hoehe_px <= 0 or hoehe_px > 0xFFFF:
        raise DruckFehler("Etikett zu hoch")
    dichte = max(1, min(15, int(dichte)))
    tempo = max(1, min(5, int(tempo)))
    out = bytearray()
    out += bytes([0x1B, 0x4E, 0x0D, tempo])          # Geschwindigkeit
    out += bytes([0x1B, 0x4E, 0x04, dichte])         # Schwärzung
    out += bytes([0x1F, 0x11, papier])               # Papierart
    out += bytes([0x1D, 0x76, 0x30, 0x00, bpr & 0xFF, bpr >> 8, hoehe_px & 0xFF, hoehe_px >> 8])
    out += daten
    out += bytes([0x1F, 0xF0, 0x05, 0x00])           # bis zur nächsten Lücke vorschieben
    out += bytes([0x1F, 0xF0, 0x03, 0x00])
    return bytes(out)


def auf_kopfbreite(breite_px: int, hoehe_px: int, daten: bytes, kopf_px: int):
    """Raster mittig auf die Breite des Druckkopfs bringen (beschneiden oder auffüllen)."""
    kopf_px -= kopf_px % 8
    if breite_px == kopf_px:
        return breite_px, daten
    bpr_alt, bpr_neu = breite_px // 8, kopf_px // 8
    zeilen = []
    for y in range(hoehe_px):
        zeile = daten[y * bpr_alt:(y + 1) * bpr_alt]
        if bpr_alt > bpr_neu:
            start = (bpr_alt - bpr_neu) // 2
            zeile = zeile[start:start + bpr_neu]
        else:
            links = (bpr_neu - bpr_alt) // 2
            zeile = bytes(links) + zeile + bytes(bpr_neu - bpr_alt - links)
        zeilen.append(zeile)
    return kopf_px, b"".join(zeilen)


# ---------------------------------------------------------------- Übertragung
def senden(auftrag: bytes, verbindung: str, adresse: str = "", kanal: int = 1, datei: str = ""):
    if verbindung == "datei":
        with open(datei or "etikett.bin", "wb") as f:
            f.write(auftrag)
        return
    if not adresse:
        raise DruckFehler("Kein Drucker eingestellt – unter Einstellungen → Etiketten suchen")
    if verbindung == "rfcomm":
        _rfcomm(auftrag, adresse, kanal)
    elif verbindung == "ble":
        asyncio.run(_ble(auftrag, adresse))
    else:
        raise DruckFehler(f"Unbekannte Verbindung: {verbindung}")


def _rfcomm(auftrag, adresse, kanal):
    if not hasattr(socket, "AF_BLUETOOTH"):
        raise DruckFehler("Bluetooth wird von diesem System nicht unterstützt")
    try:
        with socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM) as s:
            s.settimeout(10)
            s.connect((adresse, int(kanal)))
            for i in range(0, len(auftrag), 512):
                s.sendall(auftrag[i:i + 512])
                time.sleep(0.01)
            time.sleep(1.0)  # Drucker erst fertig empfangen lassen
    except OSError as e:
        raise DruckFehler(f"Drucker nicht erreichbar ({e})") from e


def _bleak():
    try:
        import bleak  # noqa: F401
        return bleak
    except ModuleNotFoundError as e:
        raise DruckFehler("Für Bluetooth fehlt python3-bleak (sudo apt install python3-bleak)") from e


async def _ble(auftrag, adresse):
    bleak = _bleak()
    try:
        async with bleak.BleakClient(adresse, timeout=15) as c:
            ziel = None
            for dienst in c.services:
                for k in dienst.characteristics:
                    if k.uuid.lower() == SCHREIBEN:
                        ziel = k
            if ziel is None:  # andere Firmware: erste beschreibbare Kennung nehmen
                ziel = next((k for d in c.services for k in d.characteristics
                             if "write-without-response" in k.properties), None)
            if ziel is None:
                raise DruckFehler("Drucker bietet keinen Schreibkanal an")
            stueck = max(20, min(180, (getattr(ziel, "max_write_without_response_size", 0) or 100)))
            for i in range(0, len(auftrag), stueck):
                await c.write_gatt_char(ziel, auftrag[i:i + stueck], response=False)
                await asyncio.sleep(0.015)
            await asyncio.sleep(1.0)
    except DruckFehler:
        raise
    except Exception as e:  # noqa: BLE001 – bleak wirft viele verschiedene Fehler
        raise DruckFehler(f"Drucker nicht erreichbar ({e})") from e


def suchen(sekunden: float = 6.0):
    """Bluetooth-Geräte in der Nähe; wahrscheinliche Phomemo-Drucker zuerst."""
    bleak = _bleak()

    async def los():
        return await bleak.BleakScanner.discover(timeout=sekunden)

    try:
        geraete = asyncio.run(los())
    except Exception as e:  # noqa: BLE001
        raise DruckFehler(f"Bluetooth-Suche fehlgeschlagen ({e})") from e
    out = []
    for g in geraete:
        name = (g.name or "").strip()
        if not name:
            continue
        out.append({"name": name, "adresse": g.address, "phomemo": wahrscheinlich_phomemo(name)})
    out.sort(key=lambda x: (not x["phomemo"], x["name"].lower()))
    return out


def wahrscheinlich_phomemo(name: str) -> bool:
    n = name.lower()
    if "phomemo" in n:
        return True
    # Phomemo meldet sich meist nur mit dem Modell, z. B. „M110“, „M220“, „M20Pro“, „Q30“
    return len(n) >= 3 and n[0] in "mqd" and n[1].isdigit()
