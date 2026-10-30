// Hardware des WT32-SC01 Plus: ST7796 (8-Bit-Parallelbus), Touch FT6336U, Hintergrundlicht.
// Wird nur für env:sc01plus gebaut (statt board.cpp), Schnittstelle wie board.h.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "board.h"
#include "config.h"
#include "lvgl.h"

namespace {

Arduino_DataBus* bus = nullptr;
Arduino_GFX* gfx = nullptr;
constexpr int BL_KANAL = 0;

// ---------------------------------------------------------------- FT6336U
// Register 0x02: Anzahl Berührungen, 0x03..0x06: erster Punkt (X/Y je 12 Bit).
// Der Controller liefert Koordinaten im Hochformat (320×480), LVGL arbeitet quer.
bool ft_lesen(uint8_t reg, uint8_t* buf, size_t n) {
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)FT6336_ADDR, (int)n) != (int)n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

int16_t letztes_x = 0, letztes_y = 0;
uint32_t letzte_ausgabe = 0;

void touch_lesen(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->state = LV_INDEV_STATE_REL;
    uint8_t p[5];
    if (ft_lesen(0x02, p, 5) && (p[0] & 0x0F) > 0 && (p[0] & 0x0F) < 3) {
        int16_t rx = ((p[1] & 0x0F) << 8) | p[2];  // 0..319 (kurze Seite)
        int16_t ry = ((p[3] & 0x0F) << 8) | p[4];  // 0..479 (lange Seite)
        int16_t x, y;
#if LCD_DREHUNG == 3
        x = LCD_W - 1 - ry;
        y = rx;
#else
        x = ry;
        y = LCD_H - 1 - rx;
#endif
        if (TOUCH_SPIEGEL_X) x = LCD_W - 1 - x;
        if (TOUCH_SPIEGEL_Y) y = LCD_H - 1 - y;
        letztes_x = constrain(x, 0, LCD_W - 1);
        letztes_y = constrain(y, 0, LCD_H - 1);
        data->state = LV_INDEV_STATE_PR;
        // Hilfe beim Einstellen von TOUCH_SPIEGEL_*: höchstens 4× pro Sekunde ausgeben
        if (TOUCH_PROTOKOLL && millis() - letzte_ausgabe > 250) {
            letzte_ausgabe = millis();
            Serial.printf("[touch] roh %d/%d → %d/%d\n", rx, ry, letztes_x, letztes_y);
        }
    }
    data->point.x = letztes_x;
    data->point.y = letztes_y;
}

// ---------------------------------------------------------------- LVGL
void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
    uint32_t w = a->x2 - a->x1 + 1;
    uint32_t h = a->y2 - a->y1 + 1;
    gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t*)&px->full, w, h);
    lv_disp_flush_ready(drv);
}

}  // namespace

void board_licht(bool an) { ledcWrite(BL_KANAL, an ? 255 : 0); }

bool board_start() {
    // Licht aus, bis das erste Bild steht
    ledcSetup(BL_KANAL, 5000, 8);
    ledcAttachPin(LCD_BL, BL_KANAL);
    ledcWrite(BL_KANAL, 0);

    bus = new Arduino_ESP32LCD8(LCD_DC, GFX_NOT_DEFINED /* CS */, LCD_WR, GFX_NOT_DEFINED /* RD */,
                                LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
    gfx = new Arduino_ST7796(bus, LCD_RST, LCD_DREHUNG, LCD_IPS);
    if (!gfx->begin()) {
        Serial.println("[board] Display startet nicht");
        return false;
    }
    gfx->fillScreen(0x0841);

    // Der gemeinsame Reset (GPIO4) hat auch den Touch-Controller neu gestartet
    delay(200);
    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    pinMode(TOUCH_INT, INPUT_PULLUP);
    uint8_t id = 0;
    if (ft_lesen(0xA8, &id, 1)) Serial.printf("[board] Touch-Controller FT, Hersteller-ID 0x%02X\n", id);
    else Serial.println("[board] Touch-Controller antwortet nicht");

    // Zwei Puffer à 40 Zeilen im internen RAM (2 MB PSRAM bleiben für LVGL und WLAN)
    const size_t n = LCD_W * 40;
    lv_color_t* b1 = (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    lv_color_t* b2 = (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!b1) {
        Serial.println("[board] kein RAM für den Bildpuffer");
        return false;
    }
    static lv_disp_draw_buf_t buf;
    lv_disp_draw_buf_init(&buf, b1, b2, n);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = LCD_W;
    drv.ver_res = LCD_H;
    drv.flush_cb = flush;
    drv.draw_buf = &buf;
    lv_disp_drv_register(&drv);

    static lv_indev_drv_t touch;
    lv_indev_drv_init(&touch);
    touch.type = LV_INDEV_TYPE_POINTER;
    touch.read_cb = touch_lesen;
    lv_indev_drv_register(&touch);

    board_licht(true);
    return true;
}
