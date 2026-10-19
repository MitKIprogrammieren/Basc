"""Schreibt in die FAT-Partition (BOOT) eines Images, ohne sie zu mounten (braucht pyfatfs).

  python3 tools/fat_schreiben.py IMAGE OFFSET ORDNER        # ORDNER nach /<name> kopieren
  python3 tools/fat_schreiben.py IMAGE OFFSET --hostname NAME  # hostname in system.cfg setzen

pyfatfs: https://github.com/nathanhi/pyfatfs (reines Python; mit PYTHONPATH einbinden).
"""
import os
import re
import sys
import warnings

from pyfatfs.PyFatFS import PyFatFS

warnings.filterwarnings("ignore", message="Filesystem was not cleanly unmounted")


def ordner_kopieren(fs, quelle, ziel):
    if fs.exists(ziel):
        fs.removetree(ziel)
    fs.makedir(ziel)
    for wurzel, dirs, dateien in os.walk(quelle):
        dirs.sort()
        rel = os.path.relpath(wurzel, quelle)
        z = ziel if rel == "." else ziel + "/" + rel.replace(os.sep, "/")
        for d in dirs:
            fs.makedir(z + "/" + d)
        for f in sorted(dateien):
            with open(os.path.join(wurzel, f), "rb") as q:
                fs.writebytes(z + "/" + f, q.read())


def hostname_setzen(fs, name):
    t = fs.readbytes("/system.cfg").decode("utf-8")
    zeile = f"hostname='{name}'"
    if re.search(r"^#?hostname=.*$", t, re.M):
        t = re.sub(r"^#?hostname=.*$", zeile, t, count=1, flags=re.M)
    else:
        t += "\n" + zeile + "\n"
    if "## FilamentLager:" not in t:
        t = t.replace("## wifi name\n",
                      "## FilamentLager: unten die zwei Zeilen WIFI_SSID / WIFI_PASSWD ohne # eintragen,\n"
                      "## z. B. WIFI_SSID='FRITZ!Box 7590 XY' und WIFI_PASSWD='geheim'\n## wifi name\n", 1)
    fs.writebytes("/system.cfg", t.encode("utf-8"))


def main():
    bild, offset = sys.argv[1], int(sys.argv[2])
    fs = PyFatFS(bild, offset=offset, preserve_case=True)
    try:
        if sys.argv[3] == "--hostname":
            hostname_setzen(fs, sys.argv[4])
        else:
            quelle = sys.argv[3].rstrip("/")
            ordner_kopieren(fs, quelle, "/" + os.path.basename(quelle))
    finally:
        fs.close()


if __name__ == "__main__":
    main()
