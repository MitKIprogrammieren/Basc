# FilamentLager-Waage (bambu-scale)

Filamentwaage mit NFC, die direkt mit dem Filamentlager **„Ernsthaft gelagert“** (`todo.filament`)
in Home Assistant arbeitet. Spule auflegen, die Waage erkennt sie am NFC-Tag, zeigt den Rest an und
trägt ihn mit einem Tipp ins Lager ein. Eigenes Design auf 800×480.

![Bildschirme der Waage](docs/bilder/uebersicht.png)

## Drei Bauarten

| | ESP32-Variante | Pi-Variante | Pad-7-Variante |
|---|---|---|---|
| Display | Waveshare ESP32-S3-Touch-LCD-5 (5", 800×480) | Raspberry Pi 4 + BTT PI TFT50 V2 | BTT Pad 7 mit CB1 (7", 1024×600) |
| Oberfläche | LVGL, direkt auf dem ESP32 | Web-Oberfläche im Chromium-Kiosk | wie Pi-Variante |
| Messung | Sensorplatine (ESP32-S3-Zero) per RS485 | Sensorplatine per USB oder HX711/PN532 am Pi | Sensorplatine per **WLAN** – kein Kabel zum Display |
| Ordner | `firmware/display-5inch`, `firmware/sensor-s3zero` | `pi/` | `pi/` + `pi/pad7/`, `firmware/sensor-s3zero` |

Beide zeigen dieselben Bildschirme: **Wiegen**, **Lager** (Suche, Filter, angebrochene Rollen),
**Sorte im Detail** und **Einstellungen** (Design, WLAN, Home Assistant, Bambu Lab mit AMS-Belegung, Waage, System).

## Teile

| Anz. | Teil | ESP32 | Pi |
|---|---|---|---|
| 1 | Waveshare ESP32-S3-Touch-LCD-5 (800×480, Touch) | ✓ | |
| 1 | Raspberry Pi 4 + BTT PI TFT50 V2 | | ✓ |
| 1 | Waveshare ESP32-S3-Zero-M (Sensorplatine) | ✓ | empfohlen |
| 2 | PN532 V3 mit Stiftleiste und Modusschalter | ✓ | ✓ |
| 1 | 5-kg-Wägezelle + HX711 | ✓ | ✓ |
| 1 | RS485-Modul 3,3 V (MAX3485) | ✓ | |
| 1 | Netzteil 5 V / ≥ 2 A | ✓ | ✓ (Pi-Netzteil) |

Verkabelung: [docs/VERKABELUNG.md](docs/VERKABELUNG.md)

## ESP32-Variante einrichten

Voraussetzung: [PlatformIO](https://platformio.org/install/cli).

```bash
# Sensorplatine: BOOT gedrückt halten, USB einstecken
cd firmware/sensor-s3zero
pio run -t upload

# Display
cd ../display-5inch
cp include/secrets.example.h include/secrets.h   # WLAN, HA-Adresse und Token eintragen
pio run -t upload
```

Danach am Display: **Einstellungen → Tara** bei leerem Teller, Gewicht auflegen, **Kalibrieren**.

## Pi-Variante einrichten

Raspberry Pi OS Bookworm (Desktop) auf dem Pi 4, dann:

```bash
git clone https://github.com/MitKIprogrammieren/bambu-scale
cd bambu-scale/pi
./install.sh            # mit --gpio für PN532/HX711 direkt am Pi, --tft50 falls das Display dunkel bleibt
nano /etc/filamentwaage/config.toml   # Token eintragen, Sensor-Art wählen
sudo systemctl restart filamentwaage
```

Die Oberfläche startet nach dem Anmelden im Vollbild. Ohne Hardware ausprobieren:
`art = "demo"` in der Konfiguration – dann laufen Testszenen mit Testdaten.

Auch am PC (Python 3.11+): `cd pi && python3 -m filamentwaage demo.toml`, dann
http://127.0.0.1:8080 im Browser öffnen.

## Pad-7-Variante einrichten

Das Pad 7 ist nur Bildschirm, die Sensorplatine in der Waage funkt per WLAN.
Schritt für Schritt: [pi/pad7/ANLEITUNG.md](pi/pad7/ANLEITUNG.md).

**Fertiges Image:** `FilamentLager-Pad7.img.xz` unter *Releases* flashen, auf der Partition BOOT WLAN
(`system.cfg`) und HA-Token (`filamentwaage/config.pad7.toml`) eintragen, einschalten – das Pad 7 richtet sich
beim ersten Start selbst ein. Gebaut mit `tools/pad7_image.sh` (braucht [pyfatfs](https://github.com/nathanhi/pyfatfs)).

Ohne fertiges Image:

1. Sensorplatine flashen, über das WLAN **FilamentWaage-Setup** ins Heimnetz bringen.
2. BTT-CB1-Image (Klipper-Version) auf die SD-Karte, WLAN in `system.cfg` eintragen.
3. `tools/pad7_paket.sh` baut `filamentwaage-pad7.zip` – den Ordner daraus auf die Partition BOOT kopieren,
   Token in `config.pad7.toml` eintragen.
4. Am Pad 7 einmal `sudo bash /boot/filamentwaage/install-pad7.sh` – danach startet es direkt in die Waage.

## Home Assistant

- Langlebiges Zugriffstoken anlegen: HA → Profil → Sicherheit → *Langlebige Zugriffstokens*.
- Die Waage braucht nur die Dienste `todo.get_items` und `todo.update_item` für `todo.filament`.
- Was genau geschrieben wird: [docs/LAGER.md](docs/LAGER.md)

## Mac-App: FilamentLager Studio

Für den Schreibtisch gibt es die Mac-App in [`desktop/`](desktop/README.md): Lager aus Home Assistant, Live-Gewicht von der
Waage, alle Bambu-Drucker mit AMS-Belegung, Regal-Plan und Etikettendruck (Phomemo M20Pro). Die fertige `.dmg` baut
GitHub Actions bei jedem Push (*Actions → FilamentLager Studio (Mac)*).

## Entwicklung und Tests

```bash
# Python (Pi): Modell, HA-Anbindung
cd pi && python3 -m unittest discover -s tests

# ESP32-Oberfläche am PC rendern (braucht LVGL 8.3 und ArduinoJson 7 als Quellen)
firmware/display-5inch/test/host/build.sh /pfad/zu/deps ausgabe/

# Schriften neu erzeugen (nach Änderung der Zeichen)
python3 tools/gen_lvgl_fonts.py <Montserrat-ttf-Ordner> firmware/display-5inch/src/fonts
```

Das Datenmodell existiert zweimal – `firmware/display-5inch/src/model.cpp` und
`pi/filamentwaage/modell.py` – mit denselben Regeln und denselben Testfällen.

Protokoll der Sensorplatine: [docs/PROTOKOLL.md](docs/PROTOKOLL.md)

## Stand

| Teil | Geprüft |
|---|---|
| Datenmodell C++ und Python | Tests mit echten Lagerdaten |
| ESP32-Oberfläche | am PC mit LVGL 8.3 gerendert |
| Pi-Dienst und Web-Oberfläche | im Demo-Modus und gegen nachgebautes Home Assistant |
| WLAN-Verbindung Pi/Pad 7 ↔ Sensorplatine (Python) | Tests gegen nachgebaute Sensorplatine |
| ESP32-Display-Treiber, Touch, WLAN, Sensor-Firmware | **noch nicht auf echter Hardware** – Pins und Display-Timing beim ersten Start prüfen |
| Pad-7-Installation (CB1) | **noch nicht auf echter Hardware** |
| Mac-App (desktop/) | Datenmodell gegen modell.py verglichen, HA und Waage gegen Nachbauten getestet, Oberfläche im Browser geprüft – **Phomemo-Bluetooth-Druck noch nicht am echten Gerät** |

## Schrift

Montserrat, SIL Open Font License 1.1 (`OFL.txt` neben den Schriftdateien).
