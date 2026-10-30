"""TSPL-Erzeugung für den M20 Pro – unverändert aus druckbruecke.py (Pi) übernommen."""


def tspl_auftrag(breite, hoehe, daten, dichte=10, kopien=1, dpi=300, quelle_dpi=203):
    """Raster der Oberfläche (8 Punkte/mm, 1 = schwarz) → TSPL für den M20 Pro (300 dpi, 1 = weiß).

    Das Bild geht in Streifen zu 24 Zeilen als einzelne BITMAP-Befehle; ganz weiße Streifen fallen weg.
    """
    # Schon in Druckerauflösung gezeichnet (Waage/SchichtLohn ab 2.20.4): 1:1, nichts hochrechnen – scharfe Kanten, gleichmäßiger QR-Code
    f = 1.0 if quelle_dpi == dpi else dpi / 203.2
    pmm = quelle_dpi / 25.4 if quelle_dpi == dpi else 8
    b_mm, h_mm = int(breite / pmm + 0.5), int(hoehe / pmm + 0.5)
    nb, nh = int(breite * f + 0.5), int(hoehe * f + 0.5)
    nbpr, bpr = (nb + 7) // 8, breite // 8
    tdichte = max(1, min(15, (int(dichte) * 7 + 5) // 10))  # 10 („Normal“) → 7 wie in der Phomemo-App
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
