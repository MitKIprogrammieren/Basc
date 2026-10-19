// Pins und Einstellungen für das Waveshare ESP32-S3-Touch-LCD-5 (800×480).
// Quelle: docs.waveshare.com/ESP32-S3-Touch-LCD-5 und Schaltplan V1.2.
#pragma once

#define FW_VERSION "1.0.0"

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
#define RS485_BAUD 115200

// ---- Verhalten ----
#define LEERSPULE_STANDARD_G 250    // Gewicht einer leeren Spule, wenn die Sorte keinen Wert hat
#define LAGER_NEU_LADEN_MS 60000    // Lager regelmäßig aus Home Assistant holen
#define SENSOR_ABFRAGE_MS 100
#define ZEITZONE "CET-1CEST,M3.5.0,M10.5.0/3"
