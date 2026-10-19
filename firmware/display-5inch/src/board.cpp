#include "board.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "lvgl.h"

namespace {

Arduino_ESP32RGBPanel* panel = nullptr;
Arduino_RGB_Display* gfx = nullptr;
uint8_t exio = EXIO_SD_CS;  // SD-Karte abgewählt

// ---------------------------------------------------------------- CH422G
// Der CH422G hat keine normale I2C-Adresse: 0x24 setzt den Modus, 0x38 die Ausgänge.
void ch422g_schreiben() {
    Wire.beginTransmission(0x24);
    Wire.write(0x01);  // IO0..7 als Ausgang
    Wire.endTransmission();
    Wire.beginTransmission(0x38);
    Wire.write(exio);
    Wire.endTransmission();
}

void exio_setzen(uint8_t bit, bool an) {
    if (an) exio |= bit;
    else exio &= ~bit;
    ch422g_schreiben();
}

// ---------------------------------------------------------------- GT911
bool gt911_lesen_reg(uint16_t reg, uint8_t* buf, size_t n) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)GT911_ADDR, (int)n) != (int)n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

void gt911_schreiben_reg(uint16_t reg, uint8_t v) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(v);
    Wire.endTransmission();
}

void touch_reset() {
    // INT während des Resets auf LOW legt die Adresse 0x5D fest
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);
    exio_setzen(EXIO_TP_RST, false);
    delay(20);
    exio_setzen(EXIO_TP_RST, true);
    delay(60);
    pinMode(TOUCH_INT, INPUT);
    delay(50);
}

int16_t letztes_x = 0, letztes_y = 0;

void touch_lesen(lv_indev_drv_t*, lv_indev_data_t* data) {
    uint8_t status = 0;
    data->state = LV_INDEV_STATE_REL;
    if (!gt911_lesen_reg(0x814E, &status, 1)) {
        data->point.x = letztes_x;
        data->point.y = letztes_y;
        return;
    }
    if (status & 0x80) {
        uint8_t n = status & 0x0F;
        if (n > 0) {
            uint8_t p[8];
            if (gt911_lesen_reg(0x8150, p, 8)) {
                letztes_x = (int16_t)(p[0] | (p[1] << 8));
                letztes_y = (int16_t)(p[2] | (p[3] << 8));
                data->state = LV_INDEV_STATE_PR;
            }
        }
        gt911_schreiben_reg(0x814E, 0);
    }
    data->point.x = constrain(letztes_x, 0, LCD_W - 1);
    data->point.y = constrain(letztes_y, 0, LCD_H - 1);
}

// ---------------------------------------------------------------- LVGL
void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
    uint32_t w = a->x2 - a->x1 + 1;
    uint32_t h = a->y2 - a->y1 + 1;
    gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t*)&px->full, w, h);
    lv_disp_flush_ready(drv);
}

}  // namespace

void board_licht(bool an) { exio_setzen(EXIO_DISP, an); }

bool board_start() {
    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    // Display und Touch zurücksetzen, Licht aus, bis das Bild steht
    exio = EXIO_SD_CS;
    ch422g_schreiben();
    delay(20);
    exio_setzen(EXIO_LCD_RST, true);
    touch_reset();

    panel = new Arduino_ESP32RGBPanel(
        LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
        LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
        LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
        LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
        0 /* hsync_polarity */, LCD_HSYNC_FRONT, LCD_HSYNC_PULSE, LCD_HSYNC_BACK,
        0 /* vsync_polarity */, LCD_VSYNC_FRONT, LCD_VSYNC_PULSE, LCD_VSYNC_BACK,
        1 /* pclk_active_neg */, LCD_PCLK_HZ);
    gfx = new Arduino_RGB_Display(LCD_W, LCD_H, panel, 0 /* rotation */, true /* auto_flush */);
    if (!gfx->begin()) {
        Serial.println("[board] Display startet nicht");
        return false;
    }
    gfx->fillScreen(0x0841);

    // Zwei Puffer à 60 Zeilen im internen RAM (schneller als PSRAM)
    const size_t n = LCD_W * 60;
    lv_color_t* b1 = (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_color_t* b2 = (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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

    uint8_t id[4] = {0};
    if (gt911_lesen_reg(0x8140, id, 4)) Serial.printf("[board] Touch-Controller GT%c%c%c\n", id[0], id[1], id[2]);
    else Serial.println("[board] Touch-Controller antwortet nicht");

    board_licht(true);
    return true;
}
