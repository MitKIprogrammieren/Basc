#include "rolle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <list>
#include <memory>

namespace {

// ---------------------------------------------------------------- Farbe mischen (vormultipliziert)
struct Px {
    float r = 0, g = 0, b = 0, a = 0;
    void auf(float cr, float cg, float cb, float ca) {  // Quelle über das Bisherige legen (Farben 0..1)
        r = cr * ca + r * (1 - ca);
        g = cg * ca + g * (1 - ca);
        b = cb * ca + b * (1 - ca);
        a = ca + a * (1 - ca);
    }
    void auf(uint32_t rgb, float ca) {
        auf(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, ca);
    }
};

// ---------------------------------------------------------------- Formen (Koordinaten im SVG, vor der Drehung)
inline float ell(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    return dx * dx + dy * dy;
}
inline bool in_ell(float x, float y, float cx, float cy, float rx, float ry) { return ell(x, y, cx, cy, rx, ry) <= 1; }

// Ungefährer Abstand zum Ellipsenrand (genau genug für Linien unter 1,5 Einheiten)
inline float ab_ell(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = x - cx, dy = y - cy;
    float f = std::sqrt(dx * dx / (rx * rx) + dy * dy / (ry * ry));
    if (f < 1e-6f) return std::min(rx, ry);
    float gx = dx / (rx * rx) / f, gy = dy / (ry * ry) / f;
    float g = std::sqrt(gx * gx + gy * gy);
    return std::fabs(f - 1) / std::max(g, 1e-6f);
}
inline bool auf_ell(float x, float y, float cx, float cy, float rx, float ry, float w) {
    return ab_ell(x, y, cx, cy, rx, ry) <= w / 2;
}

inline float ab_strecke(float x, float y, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = x - ax, wy = y - ay;
    float t = std::max(0.f, std::min(1.f, (wx * vx + wy * vy) / (vx * vx + vy * vy)));
    float dx = wx - t * vx, dy = wy - t * vy;
    return std::sqrt(dx * dx + dy * dy);
}

// Band: M18 6.4 H27 A8.3 15.6 0 0 1 27 37.6 H18 Z  (Rechteck + rechte Ellipsenhälfte)
inline bool in_band(float x, float y) {
    if (x >= 18 && x <= 27 && y >= 6.4f && y <= 37.6f) return true;
    return x >= 27 && in_ell(x, y, 27, 22, 8.3f, 15.6f);
}
// rechte Hälfte einer Ellipse als Linie (Bogen mit sweep 1 von oben nach unten)
inline bool auf_halbbogen(float x, float y, float cx, float w) {
    return x >= cx - w / 2 && auf_ell(x, y, cx, 22, 8.3f, 15.6f, w);
}
// Umriss des Bands ohne linke Kante
inline bool auf_bandrand(float x, float y, float w) {
    if (ab_strecke(x, y, 18, 6.4f, 27, 6.4f) <= w / 2) return true;
    if (ab_strecke(x, y, 18, 37.6f, 27, 37.6f) <= w / 2) return true;
    return x >= 27 && auf_ell(x, y, 27, 22, 8.3f, 15.6f, w);
}

// Glanzlicht: M12.2 11.6 c1.3-2.9 3.1-4.7 4.9-5.1 (kubische Kurve), runde Enden
float ab_glanz(float x, float y) {
    static float pkt[17][2];
    static bool fertig = false;
    if (!fertig) {
        const float p[4][2] = {{12.2f, 11.6f}, {13.5f, 8.7f}, {15.3f, 6.9f}, {17.1f, 6.5f}};
        for (int i = 0; i <= 16; i++) {
            float t = i / 16.f, u = 1 - t;
            for (int k = 0; k < 2; k++)
                pkt[i][k] = u * u * u * p[0][k] + 3 * u * u * t * p[1][k] + 3 * u * t * t * p[2][k] + t * t * t * p[3][k];
        }
        fertig = true;
    }
    float m = 1e9f;
    for (int i = 0; i < 16; i++) m = std::min(m, ab_strecke(x, y, pkt[i][0], pkt[i][1], pkt[i + 1][0], pkt[i + 1][1]));
    return m;
}

// ---------------------------------------------------------------- Verläufe (wie die linearGradients in rollenMuster())
struct Stopp {
    float pos;
    float c;  // 1 = weiß, 0 = schwarz
    float a;
};

// liefert Farbe (weiß/schwarz-Anteil) und Deckkraft an Stelle t
void verlauf(const Stopp* s, int n, float t, float& c, float& a) {
    if (t <= s[0].pos) { c = s[0].c; a = s[0].a; return; }
    for (int i = 1; i < n; i++)
        if (t <= s[i].pos) {
            float f = (t - s[i - 1].pos) / (s[i].pos - s[i - 1].pos);
            // SVG mischt Farbe und Deckkraft getrennt (nicht vormultipliziert)
            c = s[i - 1].c + (s[i].c - s[i - 1].c) * f;
            a = s[i - 1].a + (s[i].a - s[i - 1].a) * f;
            return;
        }
    c = s[n - 1].c;
    a = s[n - 1].a;
}

const Stopp SH[] = {{0, 1, .22f}, {.35f, 1, 0}, {.7f, 0, .18f}, {1, 0, .38f}};
const Stopp SH_M[] = {{0, 0, .02f}, {.6f, 0, .12f}, {1, 0, .3f}};
const Stopp SILK[] = {{.22f, 1, 0}, {.42f, 1, .62f}, {.5f, 1, .18f}, {.6f, 1, .45f}, {.78f, 1, 0}};
const Stopp METAL[] = {{0, 1, .45f}, {.3f, 0, .12f}, {.5f, 1, .35f}, {.75f, 0, .2f}, {1, 1, .25f}};

void verlauf_auf(Px& p, const Stopp* s, int n, float t) {
    float c, a;
    verlauf(s, n, t, c, a);
    if (a > 0) p.auf(c, c, c, a);
}

// ---------------------------------------------------------------- Muster (userSpaceOnUse)
inline float modp(float v, float m) {
    float r = std::fmod(v, m);
    return r < 0 ? r + m : r;
}

// Kohlefaser / Glasfaser: kurze Striche in einem gedrehten Raster
void faser(Px& p, float x, float y, float winkel_grad, bool glas) {
    float w = -winkel_grad * 3.14159265f / 180;  // patternTransform rotate(...) rückgängig machen
    float u = x * std::cos(w) - y * std::sin(w), v = x * std::sin(w) + y * std::cos(w);
    u = modp(u, 2.8f);
    v = modp(v, 2.8f);
    auto strich = [&](float ax, float bx, float sy, float bw) {
        return ab_strecke(u, v, ax, sy, bx, sy) <= bw / 2;
    };
    if (strich(.1f, 1.2f, .5f, glas ? .26f : .3f) || strich(1.3f, 2.1f, 1.75f, glas ? .26f : .3f)) {
        if (glas) p.auf(0xffffff, .3f);
        else p.auf(0x000000, .26f);
    } else if (strich(.6f, glas ? 1.1f : 1.2f, 1.15f, .25f)) {
        if (glas) p.auf(0x000000, .08f);
        else p.auf(0xffffff, .1f);
    }
}

void glitzer(Px& p, float x, float y) {
    float u = modp(x, 3.2f), v = modp(y, 3.2f);
    auto kreis = [&](float cx, float cy, float r) { return (u - cx) * (u - cx) + (v - cy) * (v - cy) <= r * r; };
    if (kreis(.7f, .8f, .32f)) p.auf(0xffffff, .95f);
    else if (kreis(2.3f, 2.2f, .22f)) p.auf(0xffffff, .75f);
    else if (kreis(2.6f, .5f, .16f)) p.auf(0xffe9a8, .85f);
}

uint32_t karo(float x, float y) {
    float u = modp(x, 3), v = modp(y, 3);
    return ((u < 1.5f) == (v < 1.5f)) ? 0x868c94 : 0xc9cdd3;
}

struct Art {
    bool trans = false, matt = false, seide = false, cf = false, gf = false, glitzer = false, metal = false;
};

Art art_von(const std::vector<std::string>& bes) {
    Art a;
    for (std::string s : bes) {
        for (auto& c : s) c = (char)tolower((unsigned char)c);
        if (s == "transparent" || s == "klar" || s == "translucent") a.trans = true;
        else if (s == "matt" || s == "matte") a.matt = true;
        else if (s == "seide" || s == "silk") a.seide = true;
        else if (s == "cf") a.cf = true;
        else if (s == "gf") a.gf = true;
        else if (s == "glitzer" || s == "sparkle" || s == "galaxy") a.glitzer = true;
        else if (s == "metallic" || s == "metall") a.metal = true;
    }
    return a;
}

// Überzüge (Seide, Metallic, CF, GF, Glitzer) über einer Fläche mit Begrenzungsrahmen bx0..bx1 / by0..by1
void ueberzuege(Px& p, const Art& a, float x, float y, float bx0, float bx1) {
    const float by0 = 6.4f, by1 = 37.6f;
    float tx = (x - bx0) / (bx1 - bx0), ty = (y - by0) / (by1 - by0);
    if (a.seide) verlauf_auf(p, SILK, 5, (tx + ty) / 2);
    if (a.metal) verlauf_auf(p, METAL, 5, ty);
    if (a.cf) faser(p, x, y, 32, false);
    if (a.gf) faser(p, x, y, -28, true);
    if (a.glitzer) glitzer(p, x, y);
}

// Eine Abtastung an der Stelle (x, y) in SVG-Koordinaten (nach Rückdrehung)
Px abtasten(float x, float y, uint32_t c, const Art& a) {
    Px p;
    const float op = a.trans ? .58f : 1;
    // hintere Scheibe
    if (in_ell(x, y, 27, 22, 9.4f, 17.4f)) p.auf(0x2b2f35, 1);
    if (auf_ell(x, y, 27, 22, 9.4f, 17.4f, 1)) p.auf(0x7d8591, 1);
    // Filamentband
    if (in_band(x, y)) {
        if (a.trans) p.auf(karo(x, y), 1);
        p.auf(c, op);
        verlauf_auf(p, a.matt ? SH_M : SH, a.matt ? 3 : 4, (y - 6.4f) / 31.2f);
        ueberzuege(p, a, x, y, 18, 35.3f);
    }
    if (auf_halbbogen(x, y, 21.5f, .55f) || auf_halbbogen(x, y, 25, .55f)) p.auf(0x000000, .1f);
    if (auf_bandrand(x, y, .7f)) p.auf(0x000000, .45f);
    // vordere Scheibe
    if (in_ell(x, y, 18, 22, 9.4f, 17.4f)) p.auf(0x33373e, 1);
    if (auf_ell(x, y, 18, 22, 9.4f, 17.4f, 1)) p.auf(0xa3abb6, 1);
    if (in_ell(x, y, 18, 22, 8.3f, 15.6f)) {
        if (a.trans) p.auf(karo(x, y), 1);
        p.auf(c, op);
        if (a.matt) p.auf(0x000000, .06f);
        ueberzuege(p, a, x, y, 9.7f, 26.3f);
    }
    if (auf_ell(x, y, 18, 22, 6.9f, 12.9f, .6f)) p.auf(0x000000, .14f);
    if (auf_ell(x, y, 18, 22, 8.3f, 15.6f, .7f)) p.auf(0x000000, .35f);
    // Nabe
    if (in_ell(x, y, 18, 22, 5.2f, 9.6f)) p.auf(0x4b5058, 1);
    if (auf_ell(x, y, 18, 22, 5.2f, 9.6f, .8f)) p.auf(0x000000, .45f);
    if (in_ell(x, y, 18, 22, 2.6f, 4.8f)) p.auf(0x1d2024, 1);
    if (auf_ell(x, y, 18, 22, 2.6f, 4.8f, .7f)) p.auf(0xa3abb6, 1);
    if (in_ell(x, y, 18, 22, 1, 1.9f)) p.auf(0x0b0c0e, 1);
    // Glanzlicht
    if (!a.matt) {
        bool stark = a.seide || a.metal;
        if (ab_glanz(x, y) <= (stark ? 1.3f : .9f) / 2) p.auf(0xffffff, stark ? .85f : .5f);
    }
    return p;
}

// ---------------------------------------------------------------- Zwischenspeicher
struct Eintrag {
    std::string schluessel;
    lv_img_dsc_t bild{};
    std::vector<uint8_t> px;
};
std::list<std::unique_ptr<Eintrag>> g_cache;  // vorne = zuletzt benutzt

}  // namespace

void rolle_rgba(uint32_t rgb, const std::vector<std::string>& besonderheiten, int n, std::vector<uint8_t>& rgba) {
    const Art a = art_von(besonderheiten);
    const int SS = 4;  // 4×4 Abtastungen je Bildpunkt
    const float ws = std::cos(16 * 3.14159265f / 180), wc = std::sin(16 * 3.14159265f / 180);
    std::vector<float> fl((size_t)n * n * 4, 0);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            Px summe;
            for (int sj = 0; sj < SS; sj++)
                for (int si = 0; si < SS; si++) {
                    float vx = (i + (si + .5f) / SS) * 44.f / n, vy = (j + (sj + .5f) / SS) * 44.f / n;
                    // Gruppe ist um −16° gedreht → Abtastpunkt um +16° zurückdrehen
                    float dx = vx - 22, dy = vy - 22;
                    float x = 22 + dx * ws - dy * wc, y = 22 + dx * wc + dy * ws;
                    Px p = abtasten(x, y, rgb, a);
                    summe.r += p.r;
                    summe.g += p.g;
                    summe.b += p.b;
                    summe.a += p.a;
                }
            float* d = &fl[((size_t)j * n + i) * 4];
            d[0] = summe.r / (SS * SS);
            d[1] = summe.g / (SS * SS);
            d[2] = summe.b / (SS * SS);
            d[3] = summe.a / (SS * SS);
        }
    // Schatten wie filter: drop-shadow(0 1px 1.5px rgba(0,0,0,.35)): Deckung 1 px nach unten, leicht weich
    std::vector<float> sch((size_t)n * n, 0);
    const float k[3] = {.25f, .5f, .25f};
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int dj = -1; dj <= 1; dj++)
                for (int di = -1; di <= 1; di++) {
                    int x = i + di, y = j - 1 + dj;
                    if (x < 0 || y < 0 || x >= n || y >= n) continue;
                    s += fl[((size_t)y * n + x) * 4 + 3] * k[di + 1] * k[dj + 1];
                }
            sch[(size_t)j * n + i] = s * .35f;
        }
    rgba.assign((size_t)n * n * 4, 0);
    for (size_t q = 0; q < (size_t)n * n; q++) {
        float r = fl[q * 4], g = fl[q * 4 + 1], b = fl[q * 4 + 2], al = fl[q * 4 + 3];
        float sa = sch[q] * (1 - al);  // Schatten nur dort, wo die Rolle nicht deckt
        float A = al + sa;
        uint8_t* d = &rgba[q * 4];
        if (A > 0) {
            d[0] = (uint8_t)std::lround(std::min(1.f, r / A) * 255);
            d[1] = (uint8_t)std::lround(std::min(1.f, g / A) * 255);
            d[2] = (uint8_t)std::lround(std::min(1.f, b / A) * 255);
        }
        d[3] = (uint8_t)std::lround(std::min(1.f, A) * 255);
    }
}

const lv_img_dsc_t* rolle_bild(uint32_t rgb, const std::vector<std::string>& bes, int n, bool leer) {
    std::string k = std::to_string(rgb) + "|" + std::to_string(n) + "|" + (leer ? "L" : "");
    for (const auto& b : bes) k += "|" + b;
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it)
        if ((*it)->schluessel == k) {
            if (it != g_cache.begin()) g_cache.splice(g_cache.begin(), g_cache, it);
            return &g_cache.front()->bild;
        }
    std::vector<uint8_t> rgba;
    rolle_rgba(rgb, bes, n, rgba);
    std::unique_ptr<Eintrag> e(new Eintrag);
    e->schluessel = k;
    e->px.resize((size_t)n * n * LV_IMG_PX_SIZE_ALPHA_BYTE);
    for (int q = 0; q < n * n; q++) {
        lv_color_t c = lv_color_make(rgba[q * 4], rgba[q * 4 + 1], rgba[q * 4 + 2]);
        uint8_t* d = &e->px[(size_t)q * LV_IMG_PX_SIZE_ALPHA_BYTE];
        d[0] = c.full & 0xFF;
        d[1] = c.full >> 8;
        d[2] = leer ? (uint8_t)(rgba[q * 4 + 3] * 55 / 100) : rgba[q * 4 + 3];  // .rgross.leer { opacity: .55 }
    }
    e->bild.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    e->bild.header.w = n;
    e->bild.header.h = n;
    e->bild.data_size = e->px.size();
    e->bild.data = e->px.data();
    g_cache.push_front(std::move(e));
    // Älteste verwerfen – aber nie ein Bild, das LVGL gerade zeigt: die Oberfläche baut ihre Listen bei jedem
    // Neuaufbau komplett neu, ältere Einträge sind dann nicht mehr in Gebrauch.
    while (g_cache.size() > ROLLE_CACHE) g_cache.pop_back();
    return &g_cache.front()->bild;
}
