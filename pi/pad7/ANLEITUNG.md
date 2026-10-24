# FilamentLager-Waage mit dem BTT Pad 7

Das Pad 7 (mit CB1) ist nur der Bildschirm. In der Waage steckt die Sensorplatine
(ESP32-S3-Zero mit Wägezelle und NFC) und funkt per WLAN. Zwischen Waage und Pad 7 gibt es kein Kabel.

```
 Waage (ESP32-S3-Zero)  ──WLAN, Port 4711──▶  Pad 7 / CB1  ──▶  Home Assistant (todo.filament)
 Wägezelle + 2× NFC                          Oberfläche im Vollbild
```

## 1. Sensorplatine flashen und ins WLAN bringen

```bash
cd firmware/sensor-s3zero
pio run -t upload          # BOOT gedrückt halten, USB einstecken
```

1. Nach dem Start blinkt die LED **orange** und die Platine öffnet das WLAN **FilamentWaage-Setup**.
2. Mit dem Handy verbinden. Die Einrichtungsseite öffnet sich von selbst (sonst http://192.168.4.1).
3. **Configure WiFi** → Heim-WLAN wählen, Passwort eingeben, speichern.
4. Die LED wird **blau** (bzw. rot, solange nicht kalibriert). Die Waage heißt im Netz `filamentwaage.local`.

WLAN neu einrichten: **BOOT-Taste 5 s halten** (LED wird lila), die Platine startet wieder mit dem Setup-WLAN.

Tipp: In der FritzBox der Waage „immer dieselbe IPv4-Adresse zuweisen“. Die Adresse kann man dann
in `config.pad7.toml` statt `filamentwaage.local` eintragen – das ist die robusteste Variante.

## 2. Pad 7: fertiges Image (empfohlen)

Das fertige Image `FilamentLager-Pad7.img.xz` (aus `tools/pad7_image.sh`, liegt als Release im Repo) enthält
das BTT-CB1-System, die Waage-Software und eine automatische Ersteinrichtung.

1. Mit **balenaEtcher** oder dem **Raspberry Pi Imager** (*Eigenes Image*) auf die microSD schreiben.
2. Karte neu einstecken, Partition **BOOT** öffnen:
   - **system.cfg**: bei `WIFI_SSID` und `WIFI_PASSWD` das `#` entfernen und WLAN eintragen, in einfachen
     Anführungszeichen, z. B. `WIFI_SSID='FRITZ!Box 7590 XY'`.
   - **filamentwaage/config.pad7.toml**: Home-Assistant-Token eintragen (und ggf. die IP der Waage).
3. Karte ins Pad 7 (Schalter **CS-Choose** auf **CB1**), einschalten.
   Der Bildschirm zeigt „FilamentLager-Waage – Ersteinrichtung“. Das Pad 7 lädt Browser und Bildschirm-Pakete
   (5–15 Minuten, Internet nötig), startet neu und zeigt danach die Waage.

Änderungen an `config.pad7.toml` auf der Karte werden beim nächsten Start übernommen. Was du im Menü der Waage
einstellst (z. B. Home Assistant), bleibt erhalten, solange die Datei auf der Karte nicht geändert wird. Anmeldung per SSH: `biqu` / `biqu`
(Passwort danach ändern: `passwd`). Protokoll der Ersteinrichtung: `/var/log/filamentwaage-erststart.log`.

## 2b. Ohne fertiges Image: SD-Karte selbst vorbereiten

1. Image von BIGTREETECH laden: [github.com/bigtreetech/CB1/releases](https://github.com/bigtreetech/CB1/releases),
   die Datei mit **Klipper** im Namen (`CB1_Debian…_Klipper_….img.xz`). Das Klipper-Image bringt den
   Bildschirm-Treiber und die Pad-7-Funktionen schon mit; Klipper selbst wird später abgeschaltet.
2. Mit **balenaEtcher** oder dem **Raspberry Pi Imager** auf die microSD schreiben.
3. Karte neu einstecken, die Partition **BOOT** öffnen und dort:

   **system.cfg** – WLAN und Pad 7 (Zeilen ohne `#` am Anfang sind aktiv):
   ```
   WIFI_SSID='Dein WLAN'
   WIFI_PASSWD='Dein Passwort'
   hostname='FilamentLager-Pad7'
   ```

   **armbianEnv.txt** nicht ändern – im aktuellen BTT-Image ist 1024×600 schon eingestellt
   (`disp_mode=1024x600p60`). Nur bei schwarzem Bild: `extraargs=video=HDMI-A-1:1024x600-24@60` ohne `#`.

4. Den Ordner **filamentwaage** aus `filamentwaage-pad7.zip` auf die Partition **BOOT** kopieren.
5. In `filamentwaage/config.pad7.toml` das **Home-Assistant-Token** eintragen
   (HA → Profil → Sicherheit → *Langlebige Zugriffstokens*). Bei Bedarf die HA-Adresse anpassen.
6. Karte auswerfen, ins Pad 7 (CB1) stecken. Im Pad 7 muss der Schalter **CS-Choose** auf **CB1** stehen.

## 3. Einmal installieren (nur bei 2b)

Pad 7 einschalten, ca. 2 Minuten warten (beim ersten Start vergrößert sich das System auf der Karte).
Dann vom PC aus (Passwort `biqu`):

```bash
ssh biqu@FilamentLager-Pad7.local
sudo bash /boot/filamentwaage/install-pad7.sh
```

Findet der PC den Namen nicht, die IP-Adresse des Pad 7 in der FritzBox nachsehen und `ssh biqu@<IP>` nehmen.

Das Skript installiert Bildschirm und Browser, schaltet Klipper, Moonraker und KlipperScreen ab
(sie bleiben installiert) und startet neu. Danach zeigt das Pad 7 direkt die Waage.

**Danach:** am Pad 7 **Einstellungen → Tara** bei leerem Teller, bekanntes Gewicht auflegen, **Kalibrieren**.
Die Kalibrierung liegt in der Sensorplatine.

## Wenn etwas nicht klappt

| Anzeige | Abhilfe |
|---|---|
| „Sensor antwortet nicht“ | LED an der Waage prüfen (orange = kein WLAN). In `/etc/filamentwaage/config.toml` die IP der Waage eintragen, dann `sudo systemctl restart filamentwaage` |
| Lager bleibt leer | Token / HA-Adresse in `/etc/filamentwaage/config.toml` prüfen |
| Bild schwarz | `extraargs=video=…` in `armbianEnv.txt` prüfen; `journalctl -u filamentwaage-kiosk -b` |
| Protokoll | `journalctl -u filamentwaage -f` |

Geänderte `config.pad7.toml` auf der SD-Karte übernehmen: das Installationsskript einfach noch einmal starten.

Zurück zu Klipper:
```bash
sudo systemctl disable filamentwaage-kiosk filamentwaage
sudo systemctl enable KlipperScreen klipper moonraker getty@tty1
sudo reboot
```
