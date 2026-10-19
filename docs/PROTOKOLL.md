# Protokoll der Sensorplatine

Die Sensorplatine (ESP32-S3-Zero) spricht dasselbe Protokoll über **USB** (Pi) und **RS485** (5"-Display).

- 115200 Baud, 8N1
- eine JSON-Nachricht pro Zeile, abgeschlossen mit `\n`
- Die Sensorplatine sendet **nur als Antwort** auf eine Anfrage. Auf der RS485-Leitung senden so
  nie zwei Geräte gleichzeitig. Das Display bzw. der Pi fragt alle 100 ms.

## Anfragen

| Anfrage | Antwort |
|---|---|
| `{"c":"p"}` | Messwert, siehe unten |
| `{"c":"tara"}` | nach ca. 1,5 s `{"t":"ok","c":"tara"}` |
| `{"c":"kal","g":1000}` | nach ca. 1,5 s `{"t":"ok","c":"kal"}` oder `{"t":"err","c":"kal","msg":"…"}` |
| `{"c":"info"}` | `{"t":"info","fw":"1.0.0","nfc_l":true,"nfc_r":true,"hx711":true,"roh":…,"offset":…,"faktor":…}` |

Während Tara oder Kalibrieren läuft, beantwortet die Platine `{"c":"p"}` weiter normal.

## Messwert

```json
{"t":"s","g":812.4,"st":1,"cal":1,"L":"","R":"04C0FFEE112233"}
```

| Feld | Bedeutung |
|---|---|
| `g` | Gewicht auf dem Teller in Gramm (brutto, inkl. Spule), eine Nachkommastelle |
| `st` | 1 = Gewicht ruhig (Schwankung der letzten 10 Werte unter 1,5 g) |
| `cal` | 1 = kalibriert |
| `L`, `R` | UID am linken / rechten NFC-Leser als Hex-Text, leer = kein Tag |

Ein Tag gilt 1,2 s nach der letzten erfolgreichen Lesung als entfernt.

## Beim Einschalten

Per USB sendet die Platine einmal `{"t":"hallo","fw":"1.0.0","nfc_l":true,"nfc_r":true,"cal":1}`.
Auf RS485 erscheinen nach dem Einschalten ein paar Zeichen des ESP32-Bootloaders. Zeilen,
die nicht mit `{` beginnen, werden ignoriert.

## Ausprobieren

Mit angeschlossener Platine am PC, z. B. mit `pio device monitor` oder einem Terminal:

```
{"c":"info"}
{"c":"tara"}
{"c":"p"}
```
