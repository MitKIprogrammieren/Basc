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
        for datei in sorted(bilder.iterdir()):
            if datei.suffix.lower() not in (".png", ".jpg", ".webp") or not datei.is_file():
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
            print("web_daten: WARNUNG – Oberfläche und Bilder sind zusammen größer als 1,2 MB, das Dateisystem hat 1,4 MB")
