"""Konstanten der Druckbrücke (Phomemo M20 Pro und M110-Familie)."""

DOMAIN = "m20_druckbruecke"
CONF_ADDRESS = "address"
CONF_MODELL = "modell"
STANDARD_ADRESSE = "B6:61:E9:A5:D5:EB"

# BLE-Datenkanal der Phomemo-Drucker (am Mac per bleak ermittelt und mit Testdruck bestätigt)
UUID_SCHREIBEN = "0000ff02-0000-1000-8000-00805f9b34fb"
UUID_ANTWORT = "0000ff03-0000-1000-8000-00805f9b34fb"

API_PFAD = "/api/m20_druckbruecke"

# Modelle: Kennung → Anzeigename, Druckersprache, Auflösung, Druckkopf in Punkten.
# „tspl“ = M20 Pro (Text-Befehle, 300 dpi); „escpos“ = M110/M120/M220 … (Raster, 203 dpi).
MODELLE = {
    "m20pro": {"name": "M20 Pro", "sprache": "tspl", "dpi": 300, "kopf_px": 384},
    "m110": {"name": "M110", "sprache": "escpos", "dpi": 203, "kopf_px": 384},
    "m120": {"name": "M120", "sprache": "escpos", "dpi": 203, "kopf_px": 384},
    "m200": {"name": "M200", "sprache": "escpos", "dpi": 203, "kopf_px": 576},
    "m220": {"name": "M220", "sprache": "escpos", "dpi": 203, "kopf_px": 576},
    "m221": {"name": "M221", "sprache": "escpos", "dpi": 203, "kopf_px": 576},
}
STANDARD_MODELL = "m20pro"


def modell_aus_name(name: str | None) -> str | None:
    """Bluetooth-Name → Modellkennung, z. B. „M20 Pro-Q864“ → „m20pro“, „M110-A1B2“ → „m110“."""
    n = (name or "").upper().replace(" ", "").replace("-", "").replace("_", "")
    if n.startswith("M20PRO"):
        return "m20pro"
    for k in ("m110", "m120", "m200", "m220", "m221"):
        if n.startswith(k.upper()):
            return k
    return None


INFO_FRAGEN = (b"SSSGETTYPE\r\nSSSGETVERSION\r\nSSSGETSN\r\nSSSGETRFID\r\n"
               b"SSSGETPAPERCAPACITY\r\nSSSGETPAPERTOTALITY\r\n")

TEST = (b"SIZE 40 mm,30 mm\r\nGAP 4.6 mm, 0 mm\r\nDIRECTION 0,0\r\nDENSITY 7\r\nCLS\r\n"
        b"BAR 40,40,392,60\r\nBAR 40,150,200,40\r\nPRINT 1,1\r\n")

SIGNAL_UPDATE = f"{DOMAIN}_update"
