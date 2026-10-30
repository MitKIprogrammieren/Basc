// Pins der Sensorplatine (Waveshare ESP32-S3-Zero).
// Nur Pins, die auf der Stiftleiste des S3-Zero liegen. GPIO21 ist die RGB-LED.
// Vor dem Löten mit dem Pinout auf docs.waveshare.com/ESP32-S3-Zero vergleichen.
#pragma once

#define FW_VERSION "2.21.0"

// Wägezelle über NAU7802 (I²C, Adresse 0x2A)
#define WAAGE_SDA 1
#define WAAGE_SCL 2

// NFC-Leser rechts (PN532 im HSU-Modus, beide Schalter OFF)
#define NFC_R_RX 4   // an PN532 TXD
#define NFC_R_TX 5   // an PN532 RXD
#define NFC_R_RST 6  // an RSTPDN; -1 = Reset nicht verdrahtet (dann wird nur neu initialisiert)

// NFC-Leser links
#define NFC_L_RX 7
#define NFC_L_TX 8
#define NFC_L_RST 9  // wie oben

// RS485 zum Display (MAX3485-Modul)
#define RS485_RX 44  // an RO
#define RS485_TX 43  // an DI
#define RS485_DE 10  // an DE und /RE; -1 bei Modulen mit automatischer Umschaltung
#define RS485_BAUD 115200

#define LED_PIN 21   // WS2812 auf dem Waveshare S3-Zero
#define LED_PIN2 48  // WS2812 auf dem ESP32-S3 Super Mini (HW-747) – beide werden angesteuert

// Messung
#define GLAETTUNG 0.25f      // Anteil des neuen Werts (EMA), kleiner = ruhiger
#define STABIL_BAND_G 1.5f   // Schwankung, unter der das Gewicht als ruhig gilt
#define TAG_WEG_MS 1200      // so lange ohne Lesung gilt ein Tag als entfernt
#define NFC_NEUSTART_MS 5000 // so oft wird ein fehlender oder hängender NFC-Leser neu gestartet
#define NFC_MAX_FEHLER 5     // so viele Lesefehler hintereinander gelten als „Leser hängt“

// WLAN (für das BTT Pad 7 oder einen anderen Rechner ohne Kabel zur Waage)
// Beim ersten Start öffnet die Platine das WLAN "FilamentWaage-Setup" – damit verbinden und
// das Heim-WLAN auswählen. BOOT-Taste 5 s halten löscht die WLAN-Daten wieder.
#define WLAN_AKTIV 1             // 0 = nur USB/RS485 wie bisher
#define WLAN_SETUP_NAME "FilamentWaage-Setup"
#define WLAN_HOSTNAME "filamentwaage"   // erreichbar als filamentwaage.local
#define WLAN_PORT 4711
// Immer mit gesenkter Sendeleistung (8,5 dBm) funken. Kleine Boards wie der ESP32-S3 Super Mini verbinden sich
// zwar mit voller Leistung, verlieren dann aber beim Senden größerer Daten (Lager, Oberfläche) so viele Pakete,
// dass Seiten hängen bleiben. Boards mit richtiger Antenne (DevKitC-1, externe Antenne): 0
#ifndef WLAN_LEISE
#define WLAN_LEISE 1
#endif
#define WEB_PORT 80                 // Oberfläche: http://filamentwaage.local
#define LAGER_NEU_LADEN_S 60
#define ETIKETT_KOPF_PX 384         // Druckbreite des Phomemo in Punkten (48 mm bei 203 dpi)        // so oft holt die Oberfläche das Lager aus Home Assistant
#define BOOT_TASTE 0

// Trocknungs-Erinnerung: angebrochene PETG/TPU/PA/PC/PVA-Spulen gelten nach so vielen Tagen als fällig
#define TROCKNEN_NACH_TAGEN 30
