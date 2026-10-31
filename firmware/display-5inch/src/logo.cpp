#include "logo.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

// lodepng ist in LVGL als C übersetzt: nur die C-Schnittstelle einbinden
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include "src/extra/libs/png/lodepng.h"
}

std::string logo_kurz(const std::string& name) {
    std::string n;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = name[i];
        if (c == 0xC3 && i + 1 < name.size()) {  // ä ö ü ß Ä Ö Ü in UTF-8
            unsigned char d = name[++i];
            if (d == 0xA4 || d == 0x84) n += "ae";
            else if (d == 0xB6 || d == 0x96) n += "oe";
            else if (d == 0xBC || d == 0x9C) n += "ue";
            else if (d == 0x9F) n += "ss";
            else n += '-';
            continue;
        }
        if (isalnum(c) && c < 0x80) n += (char)tolower(c);
        else n += '-';
    }
    std::string out;
    for (char c : n)
        if (c != '-' || (!out.empty() && out.back() != '-')) out += c;
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.substr(0, 60);
}

namespace {

struct Rgba {
    std::vector<uint8_t> p;  // RGBA
    int w = 0, h = 0;
};

bool dekodieren(const uint8_t* png, size_t n, Rgba& aus) {
    unsigned char* px = nullptr;
    unsigned w = 0, h = 0;
    if (lodepng_decode32(&px, &w, &h, png, n) || !px || !w || !h) {
        if (px) lv_mem_free(px);
        return false;
    }
    aus.w = (int)w;
    aus.h = (int)h;
    aus.p.assign(px, px + (size_t)w * h * 4);
    lv_mem_free(px);
    return true;
}

// Flächenmittel mit vormultipliziertem Alpha – auch starkes Verkleinern bleibt sauber
Rgba skalieren(const Rgba& q, int w, int h) {
    Rgba z;
    z.w = std::max(1, w);
    z.h = std::max(1, h);
    z.p.assign((size_t)z.w * z.h * 4, 0);
    double fx = (double)q.w / z.w, fy = (double)q.h / z.h;
    for (int y = 0; y < z.h; y++) {
        int y0 = (int)(y * fy), y1 = std::max(y0 + 1, (int)((y + 1) * fy));
        for (int x = 0; x < z.w; x++) {
            int x0 = (int)(x * fx), x1 = std::max(x0 + 1, (int)((x + 1) * fx));
            double r = 0, g = 0, b = 0, a = 0;
            int n = 0;
            for (int yy = y0; yy < y1 && yy < q.h; yy++)
                for (int xx = x0; xx < x1 && xx < q.w; xx++) {
                    const uint8_t* s = &q.p[((size_t)yy * q.w + xx) * 4];
                    double al = s[3] / 255.0;
                    r += s[0] * al;
                    g += s[1] * al;
                    b += s[2] * al;
                    a += al;
                    n++;
                }
            uint8_t* d = &z.p[((size_t)y * z.w + x) * 4];
            if (a > 0) {
                d[0] = (uint8_t)std::min(255.0, r / a);
                d[1] = (uint8_t)std::min(255.0, g / a);
                d[2] = (uint8_t)std::min(255.0, b / a);
            }
            d[3] = n ? (uint8_t)std::min(255.0, a / n * 255.0 + 0.5) : 0;
        }
    }
    return z;
}

// Weißen/hellen Hintergrund vom Rand her entfernen (Flutfüllung), Kante weich auslaufen lassen
void hintergrund_weg(Rgba& b) {
    int w = b.w, h = b.h;
    auto hell = [&](int i) {
        const uint8_t* p = &b.p[(size_t)i * 4];
        int mn = std::min({p[0], p[1], p[2]}), mx = std::max({p[0], p[1], p[2]});
        return p[3] > 0 && mn > 200 && mx - mn < 30;
    };
    std::vector<uint8_t> weg((size_t)w * h, 0);
    std::vector<int> stapel;
    for (int i = 0; i < w; i++) {
        stapel.push_back(i);
        stapel.push_back((h - 1) * w + i);
    }
    for (int j = 0; j < h; j++) {
        stapel.push_back(j * w);
        stapel.push_back(j * w + w - 1);
    }
    while (!stapel.empty()) {
        int i = stapel.back();
        stapel.pop_back();
        if (weg[i] || !(b.p[(size_t)i * 4 + 3] < 30 || hell(i))) continue;
        weg[i] = 1;
        int x = i % w, y = i / w;
        if (x > 0) stapel.push_back(i - 1);
        if (x < w - 1) stapel.push_back(i + 1);
        if (y > 0) stapel.push_back(i - w);
        if (y < h - 1) stapel.push_back(i + w);
    }
    for (int i = 0; i < w * h; i++) {
        uint8_t* p = &b.p[(size_t)i * 4];
        if (weg[i]) {
            p[3] = 0;
            continue;
        }
        int x = i % w, y = i / w;
        bool rand = (x > 0 && weg[i - 1]) || (x < w - 1 && weg[i + 1]) || (y > 0 && weg[i - w]) || (y < h - 1 && weg[i + w]);
        if (rand) {
            double lum = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255.0;
            p[3] = (uint8_t)std::lround(p[3] * std::min(1.0, std::max(0.0, (1 - lum) / 0.25)));
        }
    }
}

// Durchsichtigen Rand abschneiden
void zuschneiden(Rgba& b) {
    int x0 = b.w, y0 = b.h, x1 = -1, y1 = -1;
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++)
            if (b.p[((size_t)y * b.w + x) * 4 + 3] > 20) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    if (x1 < 0 || (x0 == 0 && y0 == 0 && x1 == b.w - 1 && y1 == b.h - 1)) return;
    Rgba z;
    z.w = x1 - x0 + 1;
    z.h = y1 - y0 + 1;
    z.p.resize((size_t)z.w * z.h * 4);
    for (int y = 0; y < z.h; y++)
        memcpy(&z.p[(size_t)y * z.w * 4], &b.p[((size_t)(y + y0) * b.w + x0) * 4], (size_t)z.w * 4);
    b = std::move(z);
}

// Vor dem Verkleinern grob auf höchstens 4× Zielgröße bringen (spart Rechenzeit auf dem ESP32)
void vorab_verkleinern(Rgba& b, int max_w, int max_h) {
    double f = std::min((double)max_w * 4 / b.w, (double)max_h * 4 / b.h);
    if (f < 1) b = skalieren(b, (int)(b.w * f), (int)(b.h * f));
}

void einpassen(Rgba& b, int max_w, int max_h) {
    double f = std::min(std::min(1.0, (double)max_w / b.w), (double)max_h / b.h);
    int w = std::max(1, (int)std::lround(b.w * f)), h = std::max(1, (int)std::lround(b.h * f));
    if (w != b.w || h != b.h) b = skalieren(b, w, h);
}

}  // namespace

namespace {

// Aufbereitetes Logo (Hintergrund weg, dunkle Teile aufgehellt) in die LVGL-Form bringen
void als_lvgl(const Rgba& b, lv_img_dsc_t& dsc, std::vector<uint8_t>& px) {
    px.resize((size_t)b.w * b.h * LV_IMG_PX_SIZE_ALPHA_BYTE);
    for (int i = 0; i < b.w * b.h; i++) {
        const uint8_t* p = &b.p[(size_t)i * 4];
        lv_color_t c = lv_color_make(p[0], p[1], p[2]);
        uint8_t* d = &px[(size_t)i * LV_IMG_PX_SIZE_ALPHA_BYTE];
#if LV_COLOR_DEPTH == 16
        d[0] = c.full & 0xFF;
        d[1] = c.full >> 8;
#else
#error "nur 16 Bit Farbtiefe"
#endif
        d[2] = p[3];
    }
    memset(&dsc, 0, sizeof(dsc));
    dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    dsc.header.w = b.w;
    dsc.header.h = b.h;
    dsc.data_size = px.size();
    dsc.data = px.data();
}

}  // namespace

bool logo_laden(const uint8_t* png, size_t laenge, uint32_t text_rgb, Logo& aus) {
    Rgba b;
    if (!dekodieren(png, laenge, b)) return false;
    vorab_verkleinern(b, LOGO_H_GROSS * 8 * 2, LOGO_H_GROSS * 2);
    hintergrund_weg(b);
    zuschneiden(b);
    // Schwarze/dunkelgraue Logos wären auf dem dunklen Hintergrund unsichtbar:
    // nur unbunte dunkle Pixel in Schriftfarbe aufhellen, bunte Teile bleiben wie sie sind.
    int dunkel = 0, sichtbar = 0;
    for (size_t i = 0; i < b.p.size(); i += 4)
        if (b.p[i + 3] > 128) {
            sichtbar++;
            if (0.299 * b.p[i] + 0.587 * b.p[i + 1] + 0.114 * b.p[i + 2] < 90) dunkel++;
        }
    if (sichtbar && dunkel * 2 > sichtbar) {
        uint8_t tr = (text_rgb >> 16) & 0xFF, tg = (text_rgb >> 8) & 0xFF, tb = text_rgb & 0xFF;
        for (size_t i = 0; i < b.p.size(); i += 4) {
            uint8_t* p = &b.p[i];
            int mn = std::min({p[0], p[1], p[2]}), mx = std::max({p[0], p[1], p[2]});
            if (mx - mn < 40 && 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] < 140) {
                p[0] = tr;
                p[1] = tg;
                p[2] = tb;
            }
        }
    }
    const int hoehen[2] = {LOGO_H_KLEIN, LOGO_H_GROSS};
    for (int i = 0; i < 2; i++) {
        Rgba k = b;
        einpassen(k, hoehen[i] * 8, hoehen[i]);
        als_lvgl(k, aus.bild[i], aus.px[i]);
    }
    aus.png.assign(png, png + laenge);
    return true;
}

bool logo_druck(const std::vector<uint8_t>& png, int max_w, int max_h, std::vector<uint8_t>& schwarz, int& w, int& h) {
    Rgba b;
    if (png.empty() || !dekodieren(png.data(), png.size(), b)) return false;
    zuschneiden(b);  // durchsichtigen Rand weg, solange Alpha noch stimmt
    // Wie fuerDruck(): helles Logo auf durchsichtigem Grund → Deckung zählt; sonst Dunkelheit/Buntheit
    long summe = 0;
    int deckend = 0, durch = 0;
    for (size_t i = 0; i < b.p.size(); i += 4) {
        if (b.p[i + 3] > 128) {
            summe += std::min({b.p[i], b.p[i + 1], b.p[i + 2]});
            deckend++;
        } else {
            durch++;
        }
    }
    bool hell_auf_durchsichtig = deckend > 0 && durch * 5 > deckend && summe / deckend > 200;
    // Tinte als Graustufe in einem Bild, das dann wie jedes andere verkleinert wird
    for (size_t i = 0; i < b.p.size(); i += 4) {
        uint8_t* p = &b.p[i];
        double a = p[3] / 255.0;
        int hell = std::min({p[0], p[1], p[2]}), bunt = std::max({p[0], p[1], p[2]}) - hell;
        double tinte = hell_auf_durchsichtig ? a : a * std::max((255 - hell) / 255.0, std::min(1.0, bunt / 120.0));
        uint8_t v = (uint8_t)std::lround(255 * (1 - tinte));
        p[0] = p[1] = p[2] = v;
        p[3] = 255;
    }
    vorab_verkleinern(b, max_w, max_h);
    einpassen(b, max_w, max_h);
    w = b.w;
    h = b.h;
    schwarz.assign((size_t)w * h, 0);
    for (int i = 0; i < w * h; i++) schwarz[i] = b.p[(size_t)i * 4] < 128 ? 1 : 0;
    return true;
}
