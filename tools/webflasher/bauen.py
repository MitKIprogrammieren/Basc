#!/usr/bin/env python3
"""Baut den Webflasher als eine einzige HTML-Datei (Firmware + esptool-js eingebettet, läuft offline).

    python3 tools/webflasher/bauen.py <sensor-s3zero.bin> <esptool-js.mjs> <fw-version> <ausgabe.html>

Die Datei wird direkt in Chrome/Edge geöffnet (Doppelklick). Web Serial gibt es nur dort.
"""
import base64
import pathlib
import re
import sys

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


def main() -> None:
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    bin_pfad, esptool_pfad, fw_version, ausgabe = sys.argv[1:5]
    html = (HIER / "vorlage.html").read_text(encoding="utf-8")
    firmware = pathlib.Path(bin_pfad).read_bytes()
    if firmware[:1] != b"\xe9":
        raise SystemExit("Firmware beginnt nicht mit einem ESP-Image-Kopf (0xE9) – ist es das zusammengefügte Image ab 0x0?")
    html = html.replace("/*ESPTOOL_JS*/", esptool_als_inline(pathlib.Path(esptool_pfad).read_text(encoding="utf-8")))
    html = html.replace("__FIRMWARE_B64__", base64.b64encode(firmware).decode())
    html = html.replace("__FW_VERSION__", fw_version)
    pathlib.Path(ausgabe).write_text(html, encoding="utf-8")
    print(f"{ausgabe}: {len(html) // 1024} KB, Firmware {fw_version}, {len(firmware) // 1024} KB")


if __name__ == "__main__":
    main()
