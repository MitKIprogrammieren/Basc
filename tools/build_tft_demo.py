#!/usr/bin/env python3
"""Baut pi/demo/tft-demo.html: die echte Web-Oberfläche plus simulierte Waage, NFC und Home Assistant
in einer einzigen Datei, die ohne Server im Browser läuft.

    python3 tools/build_tft_demo.py
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "pi", "filamentwaage", "web")


def lesen(*p):
    with open(os.path.join(ROOT, *p), encoding="utf-8") as f:
        return f.read()


def main():
    style = lesen("pi", "filamentwaage", "web", "style.css")
    style = re.sub(r"@font-face[^}]*}\s*", "", style)
    # globale Regeln der Kiosk-Seite entfernen, die Demo-Seite soll scrollen und einen Mauszeiger haben
    style = re.sub(r"html, body \{[^}]*}\s*", "", style)
    style = re.sub(r"body \{[^}]*}\s*", "", style)
    style = re.sub(r"body\.maus \{[^}]*}\s*", "", style)

    index = lesen("pi", "filamentwaage", "web", "index.html")
    start = index.index('<div id="app"')
    ende = index.index('<script src="/static/app.js">')
    app_html = index[start:ende].rstrip()

    appjs = lesen("pi", "filamentwaage", "web", "app.js")
    alt = re.search(r"function skalieren\(\) \{.*?\n\}", appjs, re.S).group(0)
    appjs = appjs.replace(alt, """function skalieren() {
  const box = document.getElementById("sim-screen");
  const f = box.clientWidth / 800;
  $("app").style.transform = `scale(${f})`;
  box.style.height = `${480 * f}px`;
}""")

    daten = json.loads(lesen("testdaten", "todo_filament.json"))
    seite = lesen("tools", "tft_demo_vorlage.html")
    for k, v in {"{{STYLE}}": style, "{{APP_HTML}}": app_html, "{{APPJS}}": appjs,
                 "{{DATEN}}": json.dumps(daten, ensure_ascii=False)}.items():
        seite = seite.replace(k, v)
    ziel = os.path.join(ROOT, "pi", "demo", "tft-demo.html")
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    with open(ziel, "w", encoding="utf-8") as f:
        f.write(seite)
    print(ziel, len(seite) // 1024, "KB")


if __name__ == "__main__":
    main()
