# Verkabelung

Es gibt zwei Bauarten, die beide dieselbe Oberfläche und dasselbe Filamentlager nutzen:

| | **ESP32-Variante** | **Pi-Variante** |
|---|---|---|
| Display | Waveshare ESP32-S3-Touch-LCD-5 (800×480) | Raspberry Pi 4 + BTT PI TFT50 V2 |
| Messung | Sensorplatine ESP32-S3-Zero, per RS485 | Sensorplatine per USB **oder** HX711/PN532 direkt am Pi |
| Firmware | `firmware/display-5inch` + `firmware/sensor-s3zero` | `pi/` (+ optional `firmware/sensor-s3zero`) |

> Alle Pins sind auch in den Quelltexten definiert (`include/config.h`, `pi/config.example.toml`).
> Wenn Doku und Code abweichen, gilt der Code.

## Sensorplatine (ESP32-S3-Zero)

Wird in beiden Bauarten empfohlen. Sie sitzt im Sockel der Waage bei Wägezelle und NFC-Lesern.

| Bauteil | Anschluss | S3-Zero |
|---|---|---|
| NAU7802 (Adafruit) | VIN / GND | 3V3 / GND |
| | SDA | GPIO1 |
| | SCL | GPIO2 |
| | AV, DRDY | frei |
| Wägezelle → NAU7802 | rot / schwarz / weiß / grün | E+ / E− / A− / A+ (B± frei) |
| PN532 rechts (V3, HSU) | TXD | GPIO4 |
| | RXD | GPIO5 |
| | RSTPDN (Lötpunkt an der Seite) | GPIO6 |
| | VCC / GND | **5V** / GND |
| PN532 links (V3, HSU) | TXD | GPIO7 |
| | RXD | GPIO8 |
| | RSTPDN | GPIO9 |
| | VCC / GND | **5V** / GND |
| MAX3485-Modul (nur ESP32-Variante) | DI | GPIO43 (TX) |
| | RO | GPIO44 (RX) |
| | DE + /RE (zusammen) | GPIO10 |
| | VCC / GND | 3V3 / GND |

- **Wandler:** Ab 2.10.0 nur noch NAU7802. Die Firmware sucht ihn beim Start und danach alle 5 s; vertauschte SDA/SCL erkennt sie selbst. (Für den HX711: Firmware bis 2.9.x.)
- **ESP32-S3 Super Mini** statt S3-Zero: gleiche GPIO-Nummern, die Zustands-LED sitzt dort an GPIO48 (wird mit angesteuert).
  Ab 2.21.0 funkt die Firmware dauerhaft mit gesenkter Sendeleistung (`WLAN_LEISE`): Mit voller Leistung verbindet
  sich der Super Mini zwar, verliert aber beim Senden größerer Daten so viele Pakete, dass Lager und Oberfläche hängen.
- **ESP32-S3-DevKitC-1 N16R8** (z. B. mit Antennenbuchse): gleiche GPIO-Nummern, LED an GPIO48. Wegen des
  Octal-PSRAM eigene Firmware: **FilamentLager-Webflasher-N16R8.html**. Externe Antenne vor dem Einschalten
  anschrauben – bei manchen Boards ist die eingebaute Antenne dann abgeschaltet.
- **PN532-Schalter:** beide auf **OFF** (= HSU/UART). Auf der Platine stehen an den vier Pins
  oft `SCL/SDA` – im HSU-Modus führen genau diese Pins TXD/RXD.
- **Reset (ab 2.21.0):** Über RSTPDN setzt die Firmware einen Leser zurück, der beim Start nicht antwortet
  oder sich später aufhängt – alle 5 s, bis er wieder da ist. Ohne den Draht geht es auch: dann wird
  nur neu eingerichtet. In dem Fall in `config.h` `NFC_R_RST`/`NFC_L_RST` auf `-1` setzen.
- **PN532 an 5 V:** Der V3 hat einen eigenen Regler und verträgt 3,3–5 V. An 5 V entlasten zwei Leser den
  kleinen 3,3-V-Regler der Sensorplatine; die Datenleitungen bleiben trotzdem auf 3,3 V.
- **Stromverteilung:** 5V, 3V3 und GND gibt es an der Platine nur einmal. Am einfachsten über ein
  Mini-Breadboard mit drei Reihen: 5V (AtomS3-Proxy, beide PN532), 3V3 (NAU7802), GND (alle).
- **Kreuzen:** TXD des Lesers geht an den RX-Pin des S3-Zero und umgekehrt (siehe Tabelle).
- **RS485-Module mit automatischer Umschaltung** (ohne DE-Pin): in `firmware/sensor-s3zero/include/config.h`
  `RS485_DE` auf `-1` setzen.
- Die RGB-LED des S3-Zero zeigt den Zustand: rot = nicht kalibriert, blau = bereit, grün = Tag erkannt.
- **Vor dem Löten:** Pins mit dem offiziellen Pinout des S3-Zero vergleichen
  ([Waveshare-Doku](https://docs.waveshare.com/ESP32-S3-Zero)).

## ESP32-Variante: Display ↔ Sensorplatine

| Display (Waveshare 5") | Leitung | Sensorplatine |
|---|---|---|
| RS485-Klemme A | verdrillt | MAX3485 A |
| RS485-Klemme B | verdrillt | MAX3485 B |
| GND | | GND |
| 5 V (USB-C oder 7–36 V an der DC-Klemme) | Netzteil | 5V-Pin des S3-Zero |

- Netzteil: 5 V / mindestens 2 A, gemeinsame Masse für beide Platinen.
- Kommt am Display nichts an, in `firmware/display-5inch/include/config.h` `RS485_RX`/`RS485_TX` tauschen
  oder A und B vertauschen.
- Der Abschlusswiderstand (Schalter am Display) bleibt bei kurzen Leitungen in der Waage aus.

## Pi-Variante

### a) Sensorplatine per USB (empfohlen)

USB-C des S3-Zero an einen USB-Port des Pi. Der Pi sieht sie als `/dev/ttyACM0`.
In `/etc/filamentwaage/config.toml`: `art = "usb"`. Den MAX3485 braucht man dann nicht.

### b) Alles direkt am Pi (`art = "gpio"`)

| Bauteil | Anschluss | Raspberry Pi (BCM / Pin) |
|---|---|---|
| HX711 | VCC / GND | 3V3 (Pin 1) / GND (Pin 6) |
| | DT | GPIO23 (Pin 16) |
| | SCK | GPIO24 (Pin 18) |
| PN532 links | RXD ← | GPIO4 = TX von uart3 (Pin 7) |
| | TXD → | GPIO5 = RX von uart3 (Pin 29) |
| | VCC / GND | 3V3 (Pin 17) / GND (Pin 9) |
| PN532 rechts | RXD ← | GPIO8 = TX von uart4 (Pin 24) |
| | TXD → | GPIO9 = RX von uart4 (Pin 21) |
| | VCC / GND | 3V3 / GND |

- `./install.sh --gpio` trägt `dtoverlay=uart3` und `dtoverlay=uart4` in die `config.txt` ein.
- Der HX711 ist zeitkritisch. Unter Linux gibt es gelegentlich Ausreißer, die der Median-Filter
  abfängt. Wer genaue Messungen will, nimmt Variante a.

### c) Sensorplatine per WLAN (`art = "wlan"`)

Keine Leitung zwischen Waage und Rechner. Die Sensorplatine braucht nur 5 V an USB-C (Netzteil ≥ 1 A).
Verkabelung in der Waage wie oben unter *Sensorplatine*, ohne MAX3485. Für das BTT Pad 7 ist das der vorgesehene Weg,
siehe [pi/pad7/ANLEITUNG.md](../pi/pad7/ANLEITUNG.md).

### BTT PI TFT50 V2

Das TFT50 hängt am DSI-Anschluss. Normalerweise erkennt Raspberry Pi OS es von selbst.
Bleibt das Bild schwarz oder reagiert der Touch nicht:

1. `./install.sh --tft50` trägt `dtoverlay=vc4-kms-dsi-7inch` ein (das TFT50 meldet sich wie das
   offizielle 7"-Display mit 800×480), dann neu starten.
2. Hilft das nicht, BTT-Anleitung zum TFT50 prüfen. Bei älteren Systemen wird teils
   `dtoverlay=rpi-ft5406` für den Touch genannt.
