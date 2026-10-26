# FilamentLager-Waage mit dem Creality Sonic Pad

Das Sonic Pad ist nur der Bildschirm – genau wie das Pad 7. In der Waage steckt die Sensorplatine
(ESP32-S3 mit Wägezelle und NFC) und funkt per WLAN. Zwischen Waage und Sonic Pad gibt es kein Kabel.

```
 Waage (ESP32-S3)  ──WLAN, Port 4711──▶  Sonic Pad (SonicPad-Debian)  ──▶  Home Assistant (todo.filament)
 Wägezelle + NFC                        Oberfläche im Vollbild, 1024×600
```

Die Original-Firmware von Creality (Tina Linux) hat keinen brauchbaren Browser. Deshalb kommt zuerst
**SonicPad-Debian** (Debian 12) aufs Pad. Zurück zur Creality-Firmware geht jederzeit über dasselbe Brenntool.

## 1. Sensorplatine flashen und ins WLAN bringen

Wie beim Pad 7, siehe [../pad7/ANLEITUNG.md](../pad7/ANLEITUNG.md#1-sensorplatine-flashen-und-ins-wlan-bringen)
oder per Webflasher. Test: http://filamentwaage.local im Browser.
Tipp: In der FritzBox der Waage „immer dieselbe IPv4-Adresse zuweisen“.

## 2. SonicPad-Debian flashen

Du brauchst ein **USB-A-auf-USB-A-Kabel** und eine Büroklammer.

1. Von [github.com/Teichoui/SonicPad-Debian/releases](https://github.com/Teichoui/SonicPad-Debian/releases)
   die Datei `debian_r818_sonic_lcd_uart0.zip` laden und entpacken → `debian_r818_sonic_lcd_uart0.img`.
2. **PhoenixSuit** laden (Ordner `docs/tools` im selben Projekt, für Windows und macOS), öffnen,
   Reiter **Firmware** → **Image** → die `.img` wählen.
3. Sonic Pad aus. Kabel in den **unteren Port „CAM“** hinten am Pad und in den Computer stecken.
4. Den **äußeren** versenkten Knopf gedrückt halten und dabei das Pad einschalten. Der Bildschirm bleibt
   schwarz – das Pad ist jetzt im Brennmodus. PhoenixSuit fragt, ob formatiert werden soll: **Ja**.
5. Nach dem Brennen neu starten. Beim Booten erscheint das rote Debian-Logo, danach KlipperScreen.

Ausführlich mit Bildern: [Flash-Anleitung von SonicPad-Debian](https://github.com/Teichoui/SonicPad-Debian/blob/main/docs/flashing.md).

## 3. WLAN und SSH

Auf dem Touchscreen in KlipperScreen **Menü → Netzwerk** das WLAN verbinden und die IP-Adresse notieren
(oder in der FritzBox nachsehen). Dann vom Mac:

```bash
ssh sonic@<IP-des-Sonic-Pad>        # Passwort: pad
passwd                              # bitte gleich ändern
```

Alternativ ohne Touchscreen: `sudo nmcli device wifi connect "Dein WLAN" password "Dein Passwort"` –
oder beim Installieren `--wlan="Dein WLAN"` mitgeben.

## 4. Waage installieren

Am Mac im Repo das Paket bauen und aufs Pad kopieren:

```bash
tools/sonicpad_paket.sh                                   # → filamentwaage-sonicpad.zip
scp filamentwaage-sonicpad.zip sonic@<IP>:
```

Auf dem Sonic Pad:

```bash
sudo apt-get install -y unzip
unzip -o filamentwaage-sonicpad.zip
sudo bash filamentwaage/install-sonicpad.sh --waage=192.168.178.57
```

`--waage=` ist die IP der Sensorplatine (weglassen = `filamentwaage.local`). Das Skript installiert
X-Server, Chromium und die Waage-Software (5–15 Minuten), schaltet Klipper, Moonraker, KlipperScreen und
crowsnest ab (sie bleiben installiert) und startet neu. Danach zeigt das Sonic Pad direkt die Waage.

Den Home-Assistant-Token trägst du entweder vorher in `pi/sonicpad/config.sonicpad.toml` ein oder danach in
der Waage unter **Einstellungen → Home Assistant**.

**Danach:** **Einstellungen → Tara** bei leerem Teller, bekanntes Gewicht auflegen, **Kalibrieren**.
Die Kalibrierung liegt in der Sensorplatine.

**Update:** neues Paket bauen, kopieren, entpacken und `install-sonicpad.sh` noch einmal starten. Die
Konfiguration auf dem Pad bleibt dabei erhalten (`--neue-config` überschreibt sie mit der aus dem Paket).

## Wichtig

**Nicht mit dem Knopf an der Seite ausschalten** – das kann unter Debian das Dateisystem beschädigen.
Ausschalten per SSH: `sudo shutdown now`.

## Wenn etwas nicht klappt

| Anzeige | Abhilfe |
|---|---|
| „Sensor antwortet nicht“ | LED an der Waage prüfen (orange = kein WLAN). In `/etc/filamentwaage/config.toml` die IP der Waage eintragen, dann `sudo systemctl restart filamentwaage` |
| Lager bleibt leer | Token / HA-Adresse in `/etc/filamentwaage/config.toml` prüfen |
| Bild schwarz | `journalctl -u filamentwaage-kiosk -b`; `/etc/X11/xorg.conf` muss den fbdev-Treiber enthalten; `sudo brightness -s 1` |
| Touch reagiert nicht | `groups sonic` muss `input` enthalten, sonst `sudo usermod -aG input sonic` und neu starten |
| Oberfläche träge | Chromium rendert ohne GPU – das ist beim Sonic Pad normal. `htop` zeigt, ob noch Klipper-Dienste laufen |
| Protokoll | `journalctl -u filamentwaage -f` |

Zurück zu Klipper:
```bash
sudo systemctl disable filamentwaage-kiosk filamentwaage
sudo systemctl enable KlipperScreen klipper moonraker getty@tty1
sudo reboot
```

Zurück zur Creality-Firmware: Image aus
[CrealityOfficial/Creality_Sonic_Pad_Firmware](https://github.com/CrealityOfficial/Creality_Sonic_Pad_Firmware)
genauso mit PhoenixSuit brennen.
