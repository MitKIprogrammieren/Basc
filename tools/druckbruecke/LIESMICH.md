# Druckbrücke für den Phomemo M20 Pro

Der M20 Pro druckt nur über klassisches Bluetooth – das hat der ESP32-S3 der Waage nicht. Die Druckbrücke
läuft deshalb auf einem Raspberry Pi mit Bluetooth (z. B. dem Pi-hole) und reicht die Etiketten weiter:

    Waage (ESP32) ──WLAN──▶ Druckbrücke auf dem Pi (Port 8765) ──klassisches Bluetooth──▶ M20 Pro

Installieren (vom Mac aus):

    scp druckbruecke-installieren.sh pi@192.168.1.250:
    ssh pi@192.168.1.250 sudo bash druckbruecke-installieren.sh

Das Skript installiert bei Bedarf `bluez`, legt den Dienst `filamentwaage-druck` an, koppelt den Drucker und
druckt zum Test zwei Balken. Danach in der Waage unter *Einstellungen → Etiketten → Druckbrücke*
`http://192.168.1.250:8765` eintragen.

`druckbruecke-installieren.sh` enthält `druckbruecke.py` – nach Änderungen an der .py neu zusammensetzen
(Kopf bis `PYTHON_ENDE`, dann die .py, dann der Rest).
