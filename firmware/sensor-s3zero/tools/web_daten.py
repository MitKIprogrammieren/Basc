# PlatformIO-Vorschritt: legt die Oberfläche aus pi/filamentwaage/web gzip-komprimiert nach data/.
# Eine Quelle für Pi und ESP32 – data/ wird nicht eingecheckt.
# Läuft bei jedem pio run; "pio run -t buildfs" / "-t uploadfs" bauen daraus das LittleFS-Abbild.
import gzip
import pathlib
import shutil

try:
    Import("env")  # noqa: F821 – von PlatformIO bereitgestellt
    projekt = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:  # direkt aufgerufen: python3 tools/web_daten.py
    projekt = pathlib.Path(__file__).resolve().parent.parent

quelle = projekt.parent.parent / "pi" / "filamentwaage" / "web"
bilder = projekt.parent.parent / "pi" / "hersteller"  # Herstellerbilder für die Etiketten
BILD_MAX = 150 * 1024


def kurz(name):
    """Dateiname für einen Hersteller wie kurz() in etiketten.py: „Bambu Lab“ → „bambu-lab“."""
    import re
    n = name.strip().lower().replace("ä", "ae").replace("ö", "oe").replace("ü", "ue").replace("ß", "ss")
    return re.sub(r"[^a-z0-9]+", "-", n).strip("-")[:60]
ziel = projekt / "data"

# Zeichen, die die Oberfläche braucht: Latein mit Umlauten, Satzzeichen, Pfeile, Symbole wie ✓ ⌫ ⇧ °
ZEICHEN = ("U+0020-007E,U+00A0-00FF,U+0152-0153,U+2010-2027,U+2030-205E,U+20AC,U+2122,"
           "U+1E9E,U+2190-21FF,U+2212,U+2713,U+2715,U+232B,U+25CB,U+25CF")


def schrift_verkleinern(datei):
    """Montserrat auf die gebrauchten Zeichen kürzen (450 KB → gut 30 KB je Schnitt)."""
    try:
        from fontTools import subset
    except ModuleNotFoundError:
        import subprocess
        import sys
        print("web_daten: installiere fonttools (einmalig) …")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", "fonttools"])
        from fontTools import subset
    import io
    opt = subset.Options()
    opt.layout_features = ["kern", "liga", "tnum", "lnum"]
    opt.name_IDs = ["*"]
    opt.notdef_outline = True
    opt.glyph_names = False
    schrift = subset.load_font(str(datei), opt)
    s = subset.Subsetter(opt)
    s.populate(unicodes=subset.parse_unicodes(ZEICHEN))
    s.subset(schrift)
    puffer = io.BytesIO()
    subset.save_font(schrift, puffer, opt)
    return puffer.getvalue()


if not quelle.is_dir():
    print(f"web_daten: {quelle} fehlt – Oberfläche wird nicht aktualisiert")
else:
    if ziel.exists():
        shutil.rmtree(ziel)
    ziel.mkdir()
    summe = 0
    for datei in sorted(quelle.rglob("*")):
        if not datei.is_file() or datei.name.startswith("."):
            continue
        rel = datei.relative_to(quelle)
        aus = ziel / (str(rel) + ".gz")
        aus.parent.mkdir(parents=True, exist_ok=True)
        inhalt = schrift_verkleinern(datei) if datei.suffix.lower() == ".ttf" else datei.read_bytes()
        if rel.as_posix() == "index.html":
            # Cache-Brecher: /static/app.js?v=<Prüfsumme> – nach einem Update holt der Browser sofort die neuen Dateien
            import hashlib
            import re

            def mit_version(m):
                d = quelle / m.group(1).decode()
                h = hashlib.sha1(d.read_bytes()).hexdigest()[:8] if d.is_file() else "0"
                return b"/static/" + m.group(1) + b"?v=" + h.encode() + b'"'

            inhalt = re.sub(rb'/static/([A-Za-z0-9_.-]+[.](?:js|css))"', mit_version, inhalt)
        # mtime=0: gleiche Eingabe ergibt gleiches Abbild
        with open(aus, "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", compresslevel=9, mtime=0) as gz:
            gz.write(inhalt)
        summe += aus.stat().st_size
    print(f"web_daten: Oberfläche nach data/ gelegt ({summe // 1024} KB)")

    # Herstellerbilder (bambu-lab.png …) unverändert dazulegen – sie sind schon komprimiert
    if bilder.is_dir():
        ziel_b = ziel / "hersteller"
        anzahl, groesse = 0, 0
        # Die ganze Bildsammlung passt nicht ins Dateisystem der Platine (1,4 MB) – nur die Hersteller
        # aus auf-der-waage.txt kommen mit (ein Name pro Zeile, wie im Lager geschrieben).
        auswahl = None
        liste = bilder / "auf-der-waage.txt"
        if liste.is_file():
            auswahl = set()
            for zeile in liste.read_text(encoding="utf-8").splitlines():
                zeile = zeile.split("#", 1)[0].strip()
                if zeile:
                    auswahl.add(kurz(zeile))
        for datei in sorted(bilder.iterdir()):
            if datei.suffix.lower() not in (".png", ".jpg", ".webp") or not datei.is_file():
                continue
            if auswahl is not None and datei.stem.lower() not in auswahl:
                continue
            if datei.stat().st_size > BILD_MAX:
                print(f"web_daten: {datei.name} ist größer als {BILD_MAX // 1024} KB – nicht auf die Waage gelegt (bitte verkleinern)")
                continue
            ziel_b.mkdir(exist_ok=True)
            name = datei.stem.lower() + (".jpg" if datei.suffix.lower() == ".jpeg" else datei.suffix.lower())
            shutil.copyfile(datei, ziel_b / name)
            anzahl += 1
            groesse += datei.stat().st_size
        if anzahl:
            print(f"web_daten: {anzahl} Herstellerbilder ({groesse // 1024} KB)")
        if summe + groesse > 330 * 1024:
            raise SystemExit("web_daten: Oberfläche und Bilder sind zusammen größer als 330 KB – das Dateisystem hat 384 KB. "
                             "Weniger Hersteller in pi/hersteller/auf-der-waage.txt eintragen (die übrigen lädt die Waage bei Bedarf).")
