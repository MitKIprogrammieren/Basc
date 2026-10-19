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
| HX711 | VCC | 3V3 |
| | GND | GND |
| | DT (DOUT) | GPIO1 |
| | SCK | GPIO2 |
| Wägezelle → HX711 | rot / schwarz / weiß / grün | E+ / E− / A− / A+ |
| PN532 rechts (V3, HSU) | TXD | GPIO4 |
| | RXD | GPIO5 |
| | RSTO (optional) | GPIO6 |
| | VCC / GND | 3V3 / GND |
| PN532 links (V3, HSU) | TXD | GPIO7 |
| | RXD | GPIO8 |
| | RSTO (optional) | GPIO9 |
| | VCC / GND | 3V3 / GND |
| MAX3485-Modul (nur ESP32-Variante) | DI | GPIO43 (TX) |
| | RO | GPIO44 (RX) |
| | DE + /RE (zusammen) | GPIO10 |
| | VCC / GND | 3V3 / GND |

- **PN532-Schalter:** beide auf **OFF** (= HSU/UART). Auf der Platine stehen an den vier Pins
  oft `SCL/SDA` – im HSU-Modus führen genau diese Pins TXD/RXD.
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

### BTT PI TFT50 V2

Das TFT50 hängt am DSI-Anschluss. Normalerweise erkennt Raspberry Pi OS es von selbst.
Bleibt das Bild schwarz oder reagiert der Touch nicht:

1. `./install.sh --tft50` trägt `dtoverlay=vc4-kms-dsi-7inch` ein (das TFT50 meldet sich wie das
   offizielle 7"-Display mit 800×480), dann neu starten.
2. Hilft das nicht, BTT-Anleitung zum TFT50 prüfen. Bei älteren Systemen wird teils
   `dtoverlay=rpi-ft5406` für den Touch genannt.
