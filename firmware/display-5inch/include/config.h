// Pins und Einstellungen der Display-Firmware.
//  - Waveshare ESP32-S3-Touch-LCD-5 (800×480), env:ws5
//  - WT32-SC01 Plus (3,5", 480×320), env:sc01plus → BOARD_SC01PLUS
#pragma once

#define FW_VERSION "1.9.0"

#if defined(BOARD_SC01PLUS)
// ================================================================ WT32-SC01 Plus
// Quelle: Wireless-Tag-Datenblatt V1.5 und Arduino_GFX-Beispiel „ZX3D50CE02S“.
// ESP32-S3 (16 MB Flash, 2 MB PSRAM), ST7796 über 8-Bit-Parallelbus, Touch FT6336U.

// ---- Display ----
#define LCD_W 480
#define LCD_H 320
#define LCD_DC 0
#define LCD_WR 47
#define LCD_RST 4    // setzt auch den Touch-Controller zurück
#define LCD_BL 45    // Hintergrundlicht (PWM)
#define LCD_D0 9
#define LCD_D1 46
#define LCD_D2 3
#define LCD_D3 8
#define LCD_D4 18
#define LCD_D5 17
#define LCD_D6 16
#define LCD_D7 15
// 1 = quer, 3 = quer um 180° gedreht (USB-Buchse auf der anderen Seite).
#define LCD_DREHUNG 1
// Farben falsch (Negativ-Bild)? Dann 0.
#define LCD_IPS 1

// ---- Touch FT6336U ----
#define I2C_SDA 6
#define I2C_SCL 5
#define TOUCH_INT 7
#define FT6336_ADDR 0x38
// Trifft der Finger nicht, hier gegensteuern (Rohwerte mit TOUCH_PROTOKOLL 1 im seriellen Monitor):
#define TOUCH_SPIEGEL_X 0
#define TOUCH_SPIEGEL_Y 0
#define TOUCH_PROTOKOLL 0  // 1 = Rohwerte im seriellen Monitor ausgeben

// ---- RS485-Buchse ----
// Der Transceiver braucht eine Senderfreigabe (RTS → GPIO2), die übernimmt der UART selbst.
#define RS485_RX 1
#define RS485_TX 42
#define RS485_DE 2
#define RS485_BAUD 115200

// ---- Ohne Sensorplatine: NAU7802 und zwei PN532 direkt am SC01 Plus (env:sc01plus_direkt) ----
// NAU7802 komplett an der DEBUG-Buchse (3V3, GND, IO43, IO44), beide PN532 an der I/O-Buchse (IO10–IO14).
// Pin 8 der I/O-Buchse (IO21) bleibt frei – die üblichen 7-adrigen MX1.25-Kabel erreichen ihn nicht.
// Verkabelung: docs/VERKABELUNG.md, Abschnitt „SC01 Plus ohne Sensorplatine“.
#if SENSOR_DIREKT
// Web-Oberfläche der Sensorplatine mitbauen: aus – auf dem SC01 Plus reicht der interne Speicher dafür nicht
// zuverlässig (Display, WLAN und Webserver zusammen). Der Code bleibt drin, falls es später ein größeres Board wird.
#ifndef DIREKT_WEB
#define DIREKT_WEB 0
#endif
// I²C auf eigenem Bus (Wire1), Wire gehört dem Touch. Vertauschte SDA/SCL erkennt die Firmware selbst.
// IO43 ist TXD0: Beim Start schreibt der Bootloader dort Meldungen raus. Ohne Takt auf SCL sind das für den
// NAU7802 nur Start-/Stopp-Bedingungen ohne Adresse – er ignoriert sie, danach richtet die Firmware ihn ein.
#define WAAGE_SDA 43  // DEBUG 3 gelb
#define WAAGE_SCL 44  // DEBUG 4 grün
#define NFC_R_RX 10   // I/O 3 gelb – an PN532 rechts TXD
#define NFC_R_TX 11   // I/O 4 grün – an PN532 rechts RXD
#define NFC_R_RST 12  // I/O 5 blau – an PN532 rechts RSTPDN
#define NFC_L_RX 13   // I/O 6 weiß – an PN532 links TXD
#define NFC_L_TX 14   // I/O 7 braun – an PN532 links RXD
#define NFC_L_RST -1  // kein Pin mehr frei: hängt der Leser, wird er nur neu eingerichtet
// Messung wie auf der Sensorplatine (firmware/sensor-s3zero/include/config.h)
#define GLAETTUNG 0.15f
#define ANZEIGE_HYST_G 1.5f
#define STABIL_BAND_G 1.5f
#define TAG_WEG_MS 1200
#define NFC_NEUSTART_MS 5000
#define NFC_MAX_FEHLER 5
// Web-Oberfläche (firmware/sensor-s3zero/src/oberflaeche.cpp)
#define WEB_PORT 80
#define WLAN_HOSTNAME "filamentwaage"  // Name der Sensorplatine – hier Standard „filamentwaage-2“
#define LAGER_NEU_LADEN_S 60
#define ETIKETT_KOPF_PX 384
#define TROCKNEN_NACH_TAGEN 30
#endif

#define GERAETENAME "filamentlager-waage-35"

#else
// ================================================================ Waveshare ESP32-S3-Touch-LCD-5
// Quelle: docs.waveshare.com/ESP32-S3-Touch-LCD-5 und Schaltplan V1.2.

// ---- Display (RGB565 parallel) ----
#define LCD_W 800
#define LCD_H 480
#define LCD_DE 5
#define LCD_VSYNC 3
#define LCD_HSYNC 46
#define LCD_PCLK 7
#define LCD_R0 1   // R3
#define LCD_R1 2   // R4
#define LCD_R2 42  // R5
#define LCD_R3 41  // R6
#define LCD_R4 40  // R7
#define LCD_G0 39  // G2
#define LCD_G1 0   // G3
#define LCD_G2 45  // G4
#define LCD_G3 48  // G5
#define LCD_G4 47  // G6
#define LCD_G5 21  // G7
#define LCD_B0 14  // B3
#define LCD_B1 38  // B4
#define LCD_B2 18  // B5
#define LCD_B3 17  // B6
#define LCD_B4 10  // B7
// Timing laut Waveshare-Beispiel für das 5"-Panel. Flackert das Bild,
// zuerst LCD_PCLK_HZ auf 14 MHz senken.
#define LCD_PCLK_HZ 16000000
#define LCD_HSYNC_PULSE 4
#define LCD_HSYNC_BACK 8
#define LCD_HSYNC_FRONT 8
#define LCD_VSYNC_PULSE 4
#define LCD_VSYNC_BACK 8
#define LCD_VSYNC_FRONT 8

// ---- I2C: Touch GT911, IO-Erweiterung CH422G, RTC ----
#define I2C_SDA 8
#define I2C_SCL 9
#define TOUCH_INT 4
#define GT911_ADDR 0x5D
// CH422G-Ausgänge
#define EXIO_TP_RST (1 << 1)
#define EXIO_DISP (1 << 2)   // Hintergrundbeleuchtung
#define EXIO_LCD_RST (1 << 3)
#define EXIO_SD_CS (1 << 4)

// ---- RS485 zur Sensorplatine (Klemme A/B) ----
// Laut Waveshare-Doku: GPIO43 = RS485_RXD, GPIO44 = RS485_TXD.
// Kommen keine Daten an, die beiden Werte tauschen.
#define RS485_RX 43
#define RS485_TX 44
#define RS485_DE -1  // Waveshare schaltet die Richtung selbst um
#define RS485_BAUD 115200

#define GERAETENAME "filamentlager-waage"

#endif

// ---- Sensorplatine per WLAN statt RS485 ----
// IP der Sensorplatine im Webflasher eintragen (oder SENSOR_HOST in secrets.h),
// dann verbindet sich das Display über TCP wie das BTT Pad 7. Leer: RS485.
#define SENSOR_PORT 4711

// ---- Verhalten ----
#define LEERSPULE_STANDARD_G 250    // Gewicht einer leeren Spule, wenn die Sorte keinen Wert hat
#define LAGER_NEU_LADEN_MS 60000    // Lager regelmäßig aus Home Assistant holen
#define SENSOR_ABFRAGE_MS 100
#define ZEITZONE "CET-1CEST,M3.5.0,M10.5.0/3"
