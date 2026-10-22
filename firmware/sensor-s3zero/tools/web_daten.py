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
        # mtime=0: gleiche Eingabe ergibt gleiches Abbild
        with open(aus, "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", compresslevel=9, mtime=0) as gz:
            gz.write(datei.read_bytes())
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
        if summe + groesse > 1200 * 1024:
            raise SystemExit("web_daten: Oberfläche und Bilder sind zusammen größer als 1,2 MB – das Dateisystem hat 1,4 MB. "
                             "Weniger Hersteller in pi/hersteller/auf-der-waage.txt eintragen.")
