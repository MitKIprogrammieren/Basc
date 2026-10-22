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
