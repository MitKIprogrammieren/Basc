"""Druckauftrag für Phomemo M110/M120/M220 (ESC/POS-Raster, 203 dpi).

Gleiche Befehle wie die Waagen-Firmware (drucker.cpp) und pi/filamentwaage/phomemo.py.
Das Raster der Oberfläche hat schon 8 Punkte/mm und wird nur mittig auf den Druckkopf gesetzt.
"""

PAPIER_LUECKE = 0x0A


def auf_kopfbreite(breite: int, hoehe: int, daten: bytes, kopf: int) -> tuple[int, bytes]:
    """Raster mittig auf die Breite des Druckkopfs bringen (beschneiden oder auffüllen)."""
    kopf -= kopf % 8
    if breite == kopf:
        return breite, bytes(daten)
    alt, neu = breite // 8, kopf // 8
    out = bytearray(neu * hoehe)
    for y in range(hoehe):
        z = daten[y * alt:(y + 1) * alt]
        if alt > neu:
            s = (alt - neu) // 2
            out[y * neu:(y + 1) * neu] = z[s:s + neu]
        else:
            s = (neu - alt) // 2
            out[y * neu + s:y * neu + s + alt] = z
    return kopf, bytes(out)


def escpos_auftrag(breite, hoehe, daten, dichte=10, kopien=1, kopf_px=384) -> bytes:
    """1-Bit-Raster (1 = schwarz) → ESC/POS-Auftrag, je Kopie ein Etikett bis zur nächsten Lücke."""
    breite, raster = auf_kopfbreite(int(breite), int(hoehe), daten, int(kopf_px))
    bpr = breite // 8
    dichte = max(1, min(15, int(dichte)))
    einer = bytearray([
        0x1B, 0x4E, 0x0D, 5,              # Geschwindigkeit
        0x1B, 0x4E, 0x04, dichte,         # Schwärzung
        0x1F, 0x11, PAPIER_LUECKE,        # Papierart: Etiketten mit Lücke
        0x1D, 0x76, 0x30, 0x00, bpr & 0xFF, bpr >> 8, hoehe & 0xFF, hoehe >> 8,
    ])
    einer += raster
    einer += bytes([0x1F, 0xF0, 0x05, 0x00, 0x1F, 0xF0, 0x03, 0x00])  # bis zur nächsten Lücke
    return bytes(einer) * max(1, min(10, int(kopien)))


def escpos_test(kopf_px=384) -> bytes:
    """Testetikett 40 × 30 mm: zwei schwarze Balken wie beim M20 Pro."""
    b, h = 320, 240
    bpr = b // 8
    d = bytearray(bpr * h)
    for y in range(h):
        for x in range(b):
            if (40 <= y < 80 and 24 <= x < 296) or (120 <= y < 148 and 24 <= x < 160):
                d[y * bpr + (x >> 3)] |= 0x80 >> (x & 7)
    return escpos_auftrag(b, h, bytes(d), 10, 1, kopf_px)
