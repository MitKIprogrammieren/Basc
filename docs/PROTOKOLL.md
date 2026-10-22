# Protokoll der Sensorplatine

Die Sensorplatine (ESP32-S3-Zero) spricht dasselbe Protokoll über **USB** (Pi), **RS485** (5"-Display)
und **WLAN** (TCP-Port 4711, z. B. für das BTT Pad 7).

- USB/RS485: 115200 Baud, 8N1
- WLAN: TCP-Verbindung zu `filamentwaage.local:4711`, immer nur eine Gegenstelle (eine neue löst die alte ab)
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

Per USB und bei jeder neuen WLAN-Verbindung sendet die Platine einmal `{"t":"hallo","fw":"1.0.0","nfc_l":true,"nfc_r":true,"cal":1}`.
Auf RS485 erscheinen nach dem Einschalten ein paar Zeichen des ESP32-Bootloaders. Zeilen,
die nicht mit `{` beginnen, werden ignoriert.

## WLAN

- Erststart ohne gespeichertes WLAN: Die Platine öffnet das WLAN **FilamentWaage-Setup** mit Einrichtungsseite
  (WiFiManager). Die Waage misst währenddessen weiter und antwortet über USB/RS485.
- BOOT-Taste 5 s halten löscht die WLAN-Daten.
- LED orange blinkend = kein WLAN.
- Der Pi bzw. das Pad 7 trennt die Verbindung, wenn 3 s keine Antwort kommt, und verbindet nach 2 s neu.
- `WLAN_AKTIV 0` in `include/config.h` schaltet WLAN ganz ab.

Zum Ausprobieren am PC: `nc filamentwaage.local 4711`, dann `{"c":"p"}` eingeben.

## Webseite (ab FW 1.2.0)

Sobald die Platine im Heim-WLAN ist, zeigt sie unter **http://filamentwaage.local** (oder ihrer IP aus der FritzBox)
eine eigene Seite: Live-Gewicht, NFC links/rechts, Tara, Kalibrieren, Zustand der Sensoren, WLAN neu einrichten, Neustart.
Die Seite liegt im Flash der Platine und läuft parallel zur Verbindung auf Port 4711.

| Adresse | Methode | Antwort |
|---|---|---|
| `/api/status` | GET | `{"g":812.4,"st":1,"cal":1,"L":"","R":"04…","fw":"1.2.0","hx711":true,"nfc_l":true,"nfc_r":true,"ip":"…","rssi":-58,"pad":true,"vorgang":"","meldung":""}` |
| `/api/tara` | POST | `{"ok":true,"msg":"Tara läuft…"}`, Ergebnis danach in `meldung` |
| `/api/kal?g=1000` | POST | wie Tara |
| `/api/wlan-reset` | POST | löscht die WLAN-Daten und startet neu |
| `/api/neustart` | POST | startet neu |

`pad` = gerade ist ein Pad 7/Pi über Port 4711 verbunden. Während das Setup-WLAN offen ist, gibt es die Seite noch nicht
(dort läuft die WLAN-Einrichtung auf Port 80).

## Ausprobieren

Mit angeschlossener Platine am PC, z. B. mit `pio device monitor` oder einem Terminal:

```
{"c":"info"}
{"c":"tara"}
{"c":"p"}
```
