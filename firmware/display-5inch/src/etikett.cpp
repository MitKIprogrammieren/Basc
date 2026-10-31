#include "etikett.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts/fonts.h"
#include "logo.h"
#include "src/extra/libs/qrcode/qrcodegen.h"

namespace {

constexpr int PX_MM = 8;  // 203 dpi

lv_obj_t* g_canvas = nullptr;
int g_w = 0, g_h = 0;

// Schriften von groß nach klein – fett wie in etikett.js
const lv_font_t* const SCHRIFTEN[] = {&fl_28, &fl_20, &fl_16, &fl_14, &fl_12};

int breite(const char* t, const lv_font_t* f) {
    lv_point_t s;
    lv_txt_get_size(&s, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return s.x;
}

struct Passend {
    std::string text;
    const lv_font_t* font;
};

// Größte Schrift ab start, in die der Text passt; sonst kleinste mit „…“ (wie passend() in etikett.js)
Passend passend(const std::string& t, int max_b, const lv_font_t* start, const lv_font_t* kleinste = &fl_12) {
    bool an = false;
    for (const lv_font_t* f : SCHRIFTEN) {
        if (f == start) an = true;
        if (!an) continue;
        if (breite(t.c_str(), f) <= max_b) return {t, f};
        if (f == kleinste) break;
    }
    std::string k = t;
    while (k.size() > 1 && breite((k + "…").c_str(), kleinste) > max_b) {
        k.pop_back();
        while (!k.empty() && ((unsigned char)k.back() & 0xC0) == 0x80) k.pop_back();  // UTF-8 nicht zerschneiden
    }
    return {k == t ? k : k + "…", kleinste};
}

// Text mit Grundlinie bei y (wie fillText mit textBaseline alphabetic)
void text(const std::string& t, int x, int grundlinie, const lv_font_t* f, bool weiss = false, bool rechts = false) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = f;
    d.color = weiss ? lv_color_white() : lv_color_black();
    int b = breite(t.c_str(), f);
    int oben = grundlinie - (f->line_height - f->base_line);
    lv_canvas_draw_text(g_canvas, rechts ? x - b : x, oben, b + 2, &d, t.c_str());
}

void rechteck(int x, int y, int w, int h, bool weiss = false) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = weiss ? lv_color_white() : lv_color_black();
    d.bg_opa = LV_OPA_COVER;
    lv_canvas_draw_rect(g_canvas, x, y, w, h, &d);
}

// Höhe der Großbuchstaben, für senkrechtes Mittig-Setzen
int versal(const lv_font_t* f) { return (int)std::lround((f->line_height - f->base_line) * 0.72); }

std::string bereich(int a, int b, const char* e) {
    if (a < 0 && b < 0) return "";
    if (a < 0 || a == b) return std::to_string(b) + e;
    if (b < 0) return std::to_string(a) + e;
    return std::to_string(a) + "–" + std::to_string(b) + e;
}

std::string komma(double v) {
    char b[24];
    snprintf(b, sizeof b, "%.3f", std::round(v * 1000) / 1000);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string material_von(const Filament& f) {
    std::string s = f.material;
    if (!f.besonderheiten.empty()) s += (s.empty() ? "" : " ") + f.besonderheiten[0];
    return s.empty() ? f.summary : s;
}

std::string farbe_von(const Filament& f) {
    std::string s = f.farbe;
    if (!f.fcode.empty()) s += (s.empty() ? "(" : " (") + f.fcode + ")";
    return s;
}

// Werte wie die Standard-Auswahl der Web-Oberfläche: Düse, Bett, Flow, Flow-Info – dazu der Rest
std::vector<std::pair<std::string, std::string>> zeilen(const Filament& f, int rest, size_t max) {
    std::vector<std::pair<std::string, std::string>> z;
    double flow = -1, mvs = -1;
    JsonDocument d;
    if (!deserializeJson(d, f.description)) {
        if (d["flow"].is<double>()) flow = d["flow"].as<double>();
        if (d["mvs"].is<double>()) mvs = d["mvs"].as<double>();
    }
    std::string v = bereich(f.duese_min, f.duese_max, " °C");
    if (!v.empty()) z.push_back({"Düse", v});
    v = bereich(f.bett_min, f.bett_max, " °C");
    if (!v.empty()) z.push_back({"Bett", v});
    if (flow > 0) z.push_back({"Flow", komma(flow)});
    if (mvs > 0 && flow > 0) z.push_back({"Flow-Info", komma(mvs) + " mm³/s · " + komma(flow)});
    else if (mvs > 0) z.push_back({"Max. Vol.", komma(mvs) + " mm³/s"});
    if (rest > 0) {
        // Rest hat Vorrang vor der letzten Zeile
        if (z.size() >= max && max > 0) z.resize(max - 1);
        z.push_back({"Rest", std::to_string(rest) + " g"});
    }
    if (z.size() > max) z.resize(max);
    return z;
}

int qr_zeichnen(const std::string& inhalt, int x, int y, int groesse) {
    if (inhalt.empty() || groesse < 40) return 0;
    static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)], qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    if (!qrcodegen_encodeText(inhalt.c_str(), tmp, qr, qrcodegen_Ecc_LOW, 1, 12, qrcodegen_Mask_AUTO, true)) return 0;
    int n = qrcodegen_getSize(qr), m = groesse / n;
    if (m < 2) return 0;
    int g = n * m, ox = x + groesse - g, oy = y + groesse - g;  // unten rechts ausrichten
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (qrcodegen_getModule(qr, c, r)) rechteck(ox + c * m, oy + r * m, m, m);
    return g;
}

void kopf(const Filament& f, const std::vector<uint8_t>* png, int x, int y, int b, int h) {
    std::vector<uint8_t> s;
    int w = 0, hh = 0;
    if (png && logo_druck(*png, b, h, s, w, hh)) {
        int oy = y + (h - hh) / 2;
        for (int j = 0; j < hh; j++)
            for (int i = 0; i < w; i++)
                if (s[(size_t)j * w + i]) lv_canvas_set_px_color(g_canvas, x + i, oy + j, lv_color_black());
        return;
    }
    std::string name = f.hersteller.empty() ? "Filament" : f.hersteller;
    Passend p = passend(name, b, &fl_28);
    text(p.text, x, y + (int)std::lround(h * 0.86), p.font);
}

}  // namespace

void etikett_vorschau(const EtikettBild& e, int hoehe, EtikettVorschau& aus) {
    int h = std::max(1, std::min(hoehe, e.hoehe)), w = std::max(1, e.breite * h / e.hoehe);
    aus.px.assign((size_t)w * h, lv_color_white());
    const int bpr = e.breite / 8;
    for (int y = 0; y < h; y++) {
        int y0 = y * e.hoehe / h, y1 = std::max(y0 + 1, (y + 1) * e.hoehe / h);
        for (int x = 0; x < w; x++) {
            int x0 = x * e.breite / w, x1 = std::max(x0 + 1, (x + 1) * e.breite / w), n = 0, s = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++, n++)
                    if (e.raster[(size_t)yy * bpr + (xx >> 3)] & (0x80 >> (xx & 7))) s++;
            uint8_t v = (uint8_t)(255 - 255 * s / std::max(1, n));
            aus.px[(size_t)y * w + x] = lv_color_make(v, v, v);
        }
    }
    memset(&aus.bild, 0, sizeof(aus.bild));
    aus.bild.header.cf = LV_IMG_CF_TRUE_COLOR;
    aus.bild.header.w = w;
    aus.bild.header.h = h;
    aus.bild.data_size = aus.px.size() * sizeof(lv_color_t);
    aus.bild.data = (const uint8_t*)aus.px.data();
}

std::string etikett_qr(const Filament& f, const std::string& basis_, const std::string& lager_url) {
    std::string code;
    JsonDocument d;
    if (!deserializeJson(d, f.description) && d["sl_id"].is<const char*>()) code = d["sl_id"].as<const char*>();
    if (code.empty() && !f.codes.empty()) code = f.codes[0];
    std::string basis = basis_;
    while (!basis.empty() && basis.back() == ' ') basis.pop_back();
    if (basis.empty() || code.empty()) return lager_url;
    if (basis.rfind("http://", 0) != 0 && basis.rfind("https://", 0) != 0) basis = "https://" + basis;
    // nur Adresse ohne Pfad → /app/ wie in SchichtLohn
    size_t ab = basis.find("://") + 3, pfad = basis.find('/', ab);
    if (pfad == std::string::npos) basis += "/app/";
    std::string enc;
    for (unsigned char c : code) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') enc += (char)c;
        else {
            char h[4];
            snprintf(h, sizeof h, "%%%02X", c);
            enc += h;
        }
    }
    return basis + (basis.find('?') != std::string::npos ? "&" : "?") + "rolle=" + enc;
}

std::string etikett_titel(const Filament& f) {
    std::string t = f.kurzname();
    if (!f.hersteller.empty()) t += " (" + f.hersteller + ")";
    return t;
}

bool etikett_zeichnen(const Filament& f, const EtikettOpt& o, EtikettBild& aus) {
    const int W = (o.breit ? 50 : 40) * PX_MM, H = 30 * PX_MM;
    aus.breite = W;
    aus.hoehe = H;
    aus.px.assign((size_t)W * H, lv_color_white());
    if (!g_canvas) {
        g_canvas = lv_canvas_create(lv_layer_sys());
        lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    g_w = W;
    g_h = H;
    lv_canvas_set_buffer(g_canvas, aus.px.data(), W, H, LV_IMG_CF_TRUE_COLOR);

    // Maße wie quer() in etikett.js (ref = Höhe)
    const int ref = H, m = (int)std::lround(ref * 0.055), innen = W - 2 * m;
    int y = m;
    const int kh = (int)std::lround(ref * 0.16);
    kopf(f, o.logo_png, m, y, innen, kh);
    y += kh + (int)std::lround(ref * 0.02);

    // schwarzer Balken mit weißem Material, Hex-Wert rechts daneben
    const int bh = (int)std::lround(ref * 0.17);
    char hex[8];
    snprintf(hex, sizeof hex, "#%06X", (unsigned)(f.rgb & 0xFFFFFF));
    const lv_font_t* hf = &fl_20;
    int hex_b = breite(hex, hf) + (int)std::lround(ref * 0.03);
    rechteck(m, y, innen - hex_b, bh);
    Passend mp = passend(material_von(f), innen - hex_b - (int)std::lround(bh * 0.3), &fl_28, &fl_14);
    text(mp.text, m + (int)std::lround(bh * 0.15), y + (bh + versal(mp.font)) / 2, mp.font, true);
    text(hex, W - m, y + (bh + versal(hf)) / 2, hf, false, true);
    y += bh + (int)std::lround(ref * 0.02);

    // Farbe
    Passend fp = passend(farbe_von(f), innen, &fl_28, &fl_14);
    y += versal(fp.font) + (int)std::lround(ref * 0.03);
    text(fp.text, m, y, fp.font);
    y += (int)std::lround(ref * 0.04);

    // Werte links unten, QR-Code rechts unten
    const int unten = H - m;
    int q = o.qr.empty() ? 0 : std::min(unten - y, (int)std::lround(W * 0.3));
    int zh = std::max((int)std::lround(ref * 0.072), std::min((int)std::lround(ref * 0.095), (unten - y) / 4));
    size_t max = (size_t)std::max(0, std::min(4, (unten - y) / zh));
    auto liste = zeilen(f, o.rest_g, max);
    int qg = q ? qr_zeichnen(o.qr, W - m - q, unten - q, q) : 0;
    int wb = innen - (qg ? qg + (int)std::lround(ref * 0.03) : 0);
    const lv_font_t* nf = &fl_14;
    int spalte = 0;
    for (auto& z : liste) spalte = std::max(spalte, breite(z.first.c_str(), nf));
    spalte += (int)std::lround(ref * 0.04);
    // alle Werte gleich groß, damit die Spalte ruhig wirkt
    const lv_font_t* wf = &fl_20;
    for (auto& z : liste) {
        Passend p = passend(z.second, wb - spalte, &fl_20, &fl_12);
        if (p.font->line_height < wf->line_height) wf = p.font;
    }
    int y0 = unten - (int)liste.size() * zh;
    for (size_t i = 0; i < liste.size(); i++) {
        int zy = y0 + (int)(i + 1) * zh - (int)std::lround(zh * 0.22);
        text(liste[i].first, m, zy, nf);
        Passend p = passend(liste[i].second, wb - spalte, wf, &fl_12);
        text(p.text, m + spalte, zy, p.font);
    }

    // Schwarz-Weiß wie schwarzWeiss() in etikett.js, dazu das 1-Bit-Raster
    const int bpr = W / 8;
    aus.raster.assign((size_t)bpr * H, 0);
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            lv_color_t& c = aus.px[(size_t)j * W + i];
            uint32_t r = c.ch.red * 255 / 31, g = c.ch.green * 255 / 63, b = c.ch.blue * 255 / 31;
            bool schwarz = 0.299 * r + 0.587 * g + 0.114 * b < 140;
            c = schwarz ? lv_color_black() : lv_color_white();
            if (schwarz) aus.raster[(size_t)j * bpr + (i >> 3)] |= 0x80 >> (i & 7);
        }
    memset(&aus.bild, 0, sizeof(aus.bild));
    aus.bild.header.cf = LV_IMG_CF_TRUE_COLOR;
    aus.bild.header.w = W;
    aus.bild.header.h = H;
    aus.bild.data_size = aus.px.size() * sizeof(lv_color_t);
    aus.bild.data = (const uint8_t*)aus.px.data();
    return true;
}
