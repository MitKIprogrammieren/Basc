# Phomemo M20 Pro – Druckbefehle

Aufgezeichnet am 23.10.2026 mit PacketLogger (Phomemo-App am Mac, Firmware des Druckers V1.3.5).

- Die App spricht über **klassisches Bluetooth (SPP/RFCOMM)**. Der Drucker bietet zusätzlich Bluetooth LE an
  (Dienst `ff00`, schreiben `ff02`, Antworten `ff03`) – den nutzt die Sensorplatine, weil der ESP32-S3 kein
  klassisches Bluetooth hat.
- Sprache: **TSPL** (Text, Zeilen mit `\r\n`), 300 dpi.

Abfragen der App (Antwort jeweils `NAME:WERT`):

```
SSSGETTYPE        → SSSGETTYPE:M20 Pro
SSSGETVERSION     → V1.3.5
SSSGETRFID        → 10731            (Etikettenrolle)
SSSGETPAPERCAPACITY / SSSGETPAPERTOTALITY → 106 / 110   (Rest / gesamt)
```

Druckauftrag (Etikett 40 × 30 mm):

```
SPEED 1
SIZE 40 mm,30 mm
REFERENCE 0,0
GAP 4.6 mm, 0 mm
OFFSET 0 mm
DIRECTION 0,0
DENSITY 7
CLS
BITMAP 0,0,59,354,4,<Daten>
PRINT 1,1
```

`BITMAP x,y,Bytes pro Zeile,Zeilen,Modus,Daten`: 59 × 8 = 472 Punkte = 40 mm bei 300 dpi. Die App nutzt Modus 4
(komprimiert, Format unbekannt); die Waage schickt Modus 0 unkomprimiert, 1 = weiß, 0 = schwarz.
Danach meldet der Drucker `SSSGETPRINTING:DOING` und `SSSGETPRINTING:DONE`.
