// Oberfläche der FilamentLager-Waage, LVGL 8.3.
// 800×480 (Waveshare 5") oder mit UI_KOMPAKT=1 480×320 (WT32-SC01 Plus, 3,5").
// Aufbau: Kopfzeile, Inhalt, Navigation unten.
// Seiten: Wiegen, Lager, Detail einer Sorte, Einstellungen.

#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fonts/fonts.h"
#include "icons/icons.h"
#include "rolle.h"
#include "lvgl.h"

// Stil nur setzen, wenn er sich ändert. LVGL zeichnet ein Objekt sonst bei jedem Aufruf neu – und die
// Kopfzeile, der Ring und die Knöpfe werden 10× pro Sekunde aufgefrischt. Auf dem SC01 Plus (8-Bit-Bus,
// langsamer PSRAM) kostete das spürbar Reaktionszeit.
static inline void stil_farbe(lv_obj_t* o, lv_style_prop_t p, lv_color_t c, lv_style_selector_t sel) {
    lv_style_value_t v;
    if (lv_obj_get_local_style_prop(o, p, &v, sel) == LV_RES_OK && v.color.full == c.full) return;
    v.color = c;
    lv_obj_set_local_style_prop(o, p, v, sel);
}
static inline void stil_zahl(lv_obj_t* o, lv_style_prop_t p, int32_t n, lv_style_selector_t sel) {
    lv_style_value_t v;
    if (lv_obj_get_local_style_prop(o, p, &v, sel) == LV_RES_OK && v.num == n) return;
    v.num = n;
    lv_obj_set_local_style_prop(o, p, v, sel);
}
#define lv_obj_set_style_bg_color(o, c, sel) stil_farbe(o, LV_STYLE_BG_COLOR, c, sel)
#define lv_obj_set_style_text_color(o, c, sel) stil_farbe(o, LV_STYLE_TEXT_COLOR, c, sel)
#define lv_obj_set_style_arc_color(o, c, sel) stil_farbe(o, LV_STYLE_ARC_COLOR, c, sel)
#define lv_obj_set_style_img_recolor(o, c, sel) stil_farbe(o, LV_STYLE_IMG_RECOLOR, c, sel)
#define lv_obj_set_style_border_color(o, c, sel) stil_farbe(o, LV_STYLE_BORDER_COLOR, c, sel)
#define lv_obj_set_style_opa(o, n, sel) stil_zahl(o, LV_STYLE_OPA, n, sel)

namespace {

// ---------------------------------------------------------------- Farben
constexpr uint32_t C_BG = 0x0d0f12, C_PANEL = 0x15181c, C_KACHEL = 0x1d2126, C_LINIE = 0x23272d;
constexpr uint32_t C_RAND = 0x3a4048, C_RAND2 = 0x2a2f36, C_TEXT = 0xf1f3f5, C_SUB = 0xa3aab3;
constexpr uint32_t C_NAV = 0xc9ced4, C_GRUEN = 0x4caf50, C_GRUEN_T = 0x9fe3a2, C_NAV_AKT = 0x1c2a1e;
constexpr uint32_t C_ORANGE = 0xffa726, C_ORANGE_T = 0xffb74d, C_ROT = 0xef5350, C_CHIP = 0x23272d;
constexpr uint32_t C_CHIP_G = 0x1e3024, C_CHIP_B = 0x1a2a3d, C_BLAU_T = 0x90caf9;

#ifndef UI_KOMPAKT
#define UI_KOMPAKT 0
#endif

// Maße: alles ist für 800×480 gezeichnet, S() rechnet es für das 3,5"-Display um.
// Die Schriften schrumpfen weniger als die Flächen, damit der Text lesbar bleibt.
#if UI_KOMPAKT
constexpr int W = 480, H = 320, KOPF_H = 30, NAV_H = 44;
constexpr int S(int v) { return (v * 65 + 50) / 100; }
#define F_K (&fl_12)  // klein
#define F_N (&fl_14)  // normal
#define F_G (&fl_16)  // groß
#define F_T (&fl_20)  // Kachelwerte
#define F_Z (&fl_52)  // Gewicht, „1000 g“ passt neben den Ring
#else
constexpr int W = 800, H = 480, KOPF_H = 46, NAV_H = 62;
constexpr int S(int v) { return v; }
#define F_K (&fl_14)
#define F_N (&fl_16)
#define F_G (&fl_20)
#define F_T (&fl_28)
#define F_Z (&fl_92)
#endif
// Eigener Wert bzw. Text für das 3,5"-Display, wo S() allein nicht passt
constexpr int K(int gross, int klein) { return UI_KOMPAKT ? klein : gross; }
#define KT(gross, klein) (UI_KOMPAKT ? (klein) : (gross))
constexpr int SPULE_DA_G = 20;  // ab diesem Gewicht liegt etwas auf dem Teller
constexpr int MAX_ZEILEN = 60;  // Lagerliste: mehr Zeilen nur über die Suche

enum Seite { WIEGEN = 0, LAGER, DETAIL, EINST };

UiAktionen* g_akt = nullptr;
const Lager* g_lager = nullptr;
WaagenStatus g_st;
Seite g_seite = WIEGEN;

lv_obj_t* g_seiten[4];
lv_obj_t* g_nav[3];

// Kopfzeile
lv_obj_t *h_sub, *h_nfc_l, *h_nfc_r, *h_wlan, *h_ha, *h_zeit;

// Wiegen
lv_obj_t *w_arc, *w_spule, *w_zahl, *w_einheit, *w_proz, *w_brutto, *w_leer, *w_kopf;
lv_obj_t *w_k_titel, *w_k_swatch, *w_k_name, *w_k_sub, *w_k_chips;
lv_obj_t *w_haupt, *w_haupt_lbl, *w_tara, *w_wahl, *w_wahl_lbl, *w_etikett;
std::string g_auswahl_uid;  // per Hand gewählte Sorte für die Spule auf dem Teller
bool g_spule_war_da = false;
enum class Haupt { NICHTS, SPEICHERN, ZUORDNEN, WAEHLEN, VORSCHLAG } g_haupt = Haupt::NICHTS;
int g_vorschlag = -1;  // Sorte, die das AMS für einen neuen Tag vorschlägt
std::string g_aktiver_tag;
int g_aktive_sorte = -1;
int g_netto = 0;

// Lager
lv_obj_t *l_suche, *l_tastatur, *l_liste, *l_reste;
bool g_auswahl_modus = false;  // Lager wird zum Auswählen einer Sorte geöffnet
std::string g_auswahl_tag;     // dieser Tag wird der gewählten Sorte zugeordnet

// Detail
lv_obj_t* d_inhalt;
int g_detail = -1;

// Einstellungen
lv_obj_t *e_kal_wert, *e_info;
int g_kal_g = 1000;
lv_obj_t *e_tab[2], *e_waage, *e_drucker;
int g_einst_tab = 0;  // 0 = Waage, 1 = Drucker

// AMS, Etiketten, Logos
const AmsStand* g_ams = nullptr;
int g_ams_stand = 0, g_logo_stand = 0;  // zählen hoch, damit die Wiegen-Karte neu aufgebaut wird
EtikettEinst g_et;
lv_obj_t* g_et_dlg = nullptr;   // Druckdialog, nullptr = zu
EtikettBild g_et_bild;
EtikettVorschau g_et_vorschau;
int g_et_sorte = -1, g_et_rest = 0;
bool g_et_mit_rest = true;
std::string g_et_drucker;

lv_obj_t* g_toast;
lv_obj_t* g_toast_lbl;
lv_timer_t* g_toast_timer;


// ---------------------------------------------------------------- Hilfen
lv_color_t farbe(uint32_t c) { return lv_color_hex(c); }

lv_obj_t* box(lv_obj_t* p) {
    lv_obj_t* o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* zeile(lv_obj_t* p, int abstand) {
    lv_obj_t* o = box(p);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, abstand, 0);
    return o;
}

lv_obj_t* spalte(lv_obj_t* p, int abstand) {
    lv_obj_t* o = box(p);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, abstand, 0);
    return o;
}

void panel_stil(lv_obj_t* o, uint32_t rand, int radius = 18) {
    lv_obj_set_style_bg_color(o, farbe(C_PANEL), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, farbe(rand), 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_radius(o, radius, 0);
}

lv_obj_t* text(lv_obj_t* p, const lv_font_t* f, uint32_t c, const char* t) {
    lv_obj_t* l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, farbe(c), 0);
    lv_label_set_text(l, t);
    return l;
}

void setze(lv_obj_t* l, const std::string& t) {
    if (strcmp(lv_label_get_text(l), t.c_str()) != 0) lv_label_set_text(l, t.c_str());
}

void ueberschrift(lv_obj_t* l) {
    lv_obj_set_style_text_letter_space(l, 1, 0);
}

// Label auf eine Zeile mit „…“ begrenzen
void einzeilig(lv_obj_t* l, int breite) {
    const lv_font_t* f = lv_obj_get_style_text_font(l, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_size(l, breite, lv_font_get_line_height(f));
}

lv_obj_t* chip(lv_obj_t* p, const std::string& t, uint32_t bg, uint32_t fg, uint32_t rand = 0) {
    lv_obj_t* c = box(p);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, S(8), 0);
    lv_obj_set_style_pad_ver(c, S(3), 0);
    lv_obj_set_style_radius(c, S(7), 0);
    if (rand) {
        lv_obj_set_style_border_color(c, farbe(rand), 0);
        lv_obj_set_style_border_width(c, 1, 0);
    } else {
        lv_obj_set_style_bg_color(c, farbe(bg), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    }
    text(c, &fl_12, fg, t.c_str());
    return c;
}

lv_obj_t* farbfeld(lv_obj_t* p, int groesse, int radius, uint32_t rgb) {
    lv_obj_t* f = box(p);
    lv_obj_set_size(f, groesse, groesse);
    lv_obj_set_style_radius(f, radius, 0);
    lv_obj_set_style_bg_color(f, farbe(rgb), 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(f, farbe(0x4a5058), 0);
    lv_obj_set_style_border_width(f, 1, 0);
    return f;
}

int text_breite(const char* t, const lv_font_t* f) {
    lv_point_t p;
    lv_txt_get_size(&p, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

const Logo* logo_von(const std::string& hersteller) {
    return g_akt && !hersteller.empty() ? g_akt->logo(hersteller) : nullptr;
}

// Herstellerlogo (gross = Karte/Detail, sonst Listen) höchstens max_b breit; ohne Logo der Name als Text.
// breite_aus: tatsächliche Breite, damit daneben noch Text passt.
lv_obj_t* marke(lv_obj_t* p, const std::string& hersteller, LogoArt art, const lv_font_t* f, uint32_t farbe_, int max_b,
                int* breite_aus = nullptr) {
    const Logo* l = logo_von(hersteller);
    const lv_img_dsc_t* b = l ? &l->bild[art] : nullptr;
    if (b && b->header.w && b->header.w <= max_b) {
        lv_obj_t* img = lv_img_create(p);
        lv_img_set_src(img, b);
        if (breite_aus) *breite_aus = b->header.w;
        return img;
    }
    lv_obj_t* t = text(p, f, farbe_, hersteller.c_str());
    int w = std::min(max_b, text_breite(hersteller.c_str(), f) + 2);
    einzeilig(t, w);
    if (breite_aus) *breite_aus = w;
    return t;
}

enum class Art { PRIMAER, RAHMEN, FLACH };

lv_obj_t* knopf(lv_obj_t* p, const char* t, Art art, lv_event_cb_t cb, void* daten = nullptr,
                lv_obj_t** lbl_aus = nullptr) {
    lv_obj_t* b = box(p);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, S(14), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    uint32_t fg = C_TEXT;
    if (art == Art::PRIMAER) {
        lv_obj_set_style_bg_color(b, farbe(C_GRUEN), 0);
        lv_obj_set_style_bg_color(b, farbe(0x3d8b40), LV_STATE_PRESSED);
        fg = C_BG;
    } else if (art == Art::RAHMEN) {
        lv_obj_set_style_bg_color(b, farbe(C_BG), 0);
        lv_obj_set_style_bg_color(b, farbe(C_LINIE), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, farbe(C_RAND), 0);
        lv_obj_set_style_border_width(b, 2, 0);
    } else {
        lv_obj_set_style_bg_color(b, farbe(C_PANEL), 0);
        lv_obj_set_style_bg_color(b, farbe(C_LINIE), LV_STATE_PRESSED);
        fg = C_NAV;
    }
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, art == Art::PRIMAER ? F_G : F_N, 0);
    lv_obj_set_style_text_color(l, farbe(fg), 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, daten);
    if (lbl_aus) *lbl_aus = l;
    return b;
}

void knopf_aktiv(lv_obj_t* b, bool an) {
    if (an) lv_obj_clear_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
    lv_obj_set_style_opa(b, an ? LV_OPA_COVER : LV_OPA_40, 0);  // .knopf:disabled { opacity: .4 }
}


std::string gramm(int g) { return std::to_string(g) + " g"; }

std::string komma(float v, int stellen) {
    char b[24];
    snprintf(b, sizeof b, "%.*f", stellen, v);
    for (char* p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

std::string spulen_text(int n) { return std::to_string(n) + (n == 1 ? " Spule" : " Spulen"); }

std::string rest_text(const Filament& f) {
    if (f.spulen == 0) return "aufgebraucht";
    if (f.reste.empty()) return f.refills ? std::to_string(f.refills) + " Refill" : "voll";
    if (f.reste.size() == 1) return "Rest " + gramm(f.reste[0]);
    return std::to_string(f.reste.size()) + " Reste";
}

int leergewicht(int sorte) {
    if (sorte >= 0 && g_lager->sorten[sorte].leer_g > 0) return g_lager->sorten[sorte].leer_g;
    return g_st.leer_standard_g;
}

void zeige_seite(Seite s);

// ---------------------------------------------------------------- Symbole und Rollen (wie die Web-Oberfläche)
// Symbol aus src/icons (SVG der Web-Oberfläche), in Farbe c
lv_obj_t* ikon(lv_obj_t* p, const char* name, int px, uint32_t c) {
    lv_obj_t* i = lv_img_create(p);
    const lv_img_dsc_t* b = ikon_bild(name, px);
    if (b) lv_img_set_src(i, b);
    lv_obj_set_style_img_recolor(i, farbe(c), 0);
    lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
    return i;
}

void ikon_farbe(lv_obj_t* i, uint32_t c) { lv_obj_set_style_img_recolor(i, farbe(c), 0); }

// Filamentrolle wie rolle() in app.js: ein Platz der Größe platz, das Bild (bild px) ragt darüber hinaus
lv_obj_t* rolle_platz(lv_obj_t* p, int platz, int bild, uint32_t rgb, const std::vector<std::string>& bes,
                      bool leer = false) {
    lv_obj_t* b = box(p);
    lv_obj_set_size(b, platz, platz);
    lv_obj_add_flag(b, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* i = lv_img_create(b);
    lv_img_set_src(i, rolle_bild(rgb, bes, bild, leer));
    lv_obj_center(i);
    return b;
}

void rolle_tauschen(lv_obj_t* platz, int bild, uint32_t rgb, const std::vector<std::string>& bes, bool leer = false) {
    lv_obj_t* i = lv_obj_get_child(platz, 0);
    if (i) lv_img_set_src(i, rolle_bild(rgb, bes, bild, leer));
}

// Schriften wie style.css (Pixelgrößen der Web-Oberfläche bei 800×480)
#if UI_KOMPAKT
#define T12 (&fl_12)
#define T13 (&fl_12)
#define T14 (&fl_12)
#define T15 (&fl_13)
#define T17 (&fl_15)
#define T30 (&fl_20)
#else
#define T12 (&fl_12)
#define T13 (&fl_13)
#define T14 (&fl_14)
#define T15 (&fl_15)
#define T17 (&fl_17)
#define T30 (&fl_30)
#endif

// ---------------------------------------------------------------- Kopfzeile
lv_obj_t* punkt(lv_obj_t* p) {
    lv_obj_t* d = box(p);
    lv_obj_set_size(d, 8, 8);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, farbe(C_RAND), 0);
    return d;
}

lv_obj_t *h_waage, *h_drucker_z, *h_drucker, *h_wlan_i, *h_wlan_t;

void baue_kopf(lv_obj_t* scr) {
    lv_obj_t* k = box(scr);
    lv_obj_set_size(k, W, KOPF_H);
    lv_obj_set_style_border_side(k, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(k, 1, 0);
    lv_obj_set_style_border_color(k, farbe(C_LINIE), 0);

    lv_obj_t* links = zeile(k, S(10));
    lv_obj_align(links, LV_ALIGN_LEFT_MID, K(12, 8), 0);
    // Logo: Filamentrolle in der Akzentfarbe (logoSetzen() in app.js)
    rolle_platz(links, K(30, 22), K(30, 22), C_GRUEN, {});
    text(links, T17, C_TEXT, "FilamentLager");
    h_sub = text(links, T13, C_SUB, "Waage");
    if (UI_KOMPAKT) lv_obj_add_flag(h_sub, LV_OBJ_FLAG_HIDDEN);  // .app.eng .kopf #kopf-sub { display: none }

    lv_obj_t* rechts = zeile(k, K(12, 7));
    lv_obj_align(rechts, LV_ALIGN_RIGHT_MID, -K(18, 8), 0);
    auto status = [&](const char* name, lv_obj_t** dot) {
        lv_obj_t* z = zeile(rechts, K(6, 4));
        *dot = punkt(z);
        text(z, T12, C_SUB, name);
        return z;
    };
    status(KT("NFC L", "L"), &h_nfc_l);
    status(KT("NFC R", "R"), &h_nfc_r);
    status("Waage", &h_waage);
    h_drucker_z = status(KT("Drucker", "Druck."), &h_drucker);
    lv_obj_add_flag(h_drucker_z, LV_OBJ_FLAG_HIDDEN);
    h_wlan = zeile(rechts, K(6, 4));
    h_wlan_i = ikon(h_wlan, "wlan", K(16, 13), C_SUB);
    h_wlan_t = text(h_wlan, T12, C_SUB, "WLAN");
    if (UI_KOMPAKT) lv_obj_add_flag(h_wlan_t, LV_OBJ_FLAG_HIDDEN);
    status(KT("Home Assistant", "HA"), &h_ha);
    h_zeit = text(rechts, KT(&fl_14, &fl_13), C_TEXT, "--:--");
}

// ---------------------------------------------------------------- Navigation
lv_obj_t* g_nav_ikon[3];

void nav_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    g_auswahl_modus = false;
    zeige_seite(i == 0 ? WIEGEN : i == 1 ? LAGER : EINST);
}

void baue_nav(lv_obj_t* scr) {
    lv_obj_t* n = box(scr);
    lv_obj_set_size(n, W, NAV_H);
    lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_left(n, S(16), 0);
    lv_obj_set_style_pad_right(n, S(16), 0);
    lv_obj_set_style_pad_bottom(n, S(10), 0);
    lv_obj_set_flex_flow(n, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(n, S(8), 0);
    const char* namen[3] = {"Wiegen", "Lager", "Einstellungen"};
    const char* ikone[3] = {"waage", "lager", "einst"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = box(n);
        g_nav[i] = b;
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_height(b, NAV_H - S(10));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, S(14), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, farbe(C_PANEL), 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, K(8, 5), 0);
        g_nav_ikon[i] = ikon(b, ikone[i], K(20, 14), C_NAV);
        text(b, KT(&fl_15, &fl_13), C_NAV, namen[i]);  // .nav-knopf { font-size: 14px; font-weight: 600 }
        lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

void nav_markieren(int aktiv) {
    for (int i = 0; i < 3; i++) {
        bool an = i == aktiv;
        lv_obj_set_style_bg_color(g_nav[i], farbe(an ? C_NAV_AKT : C_PANEL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g_nav[i], 1), farbe(an ? C_GRUEN_T : C_NAV), 0);
        ikon_farbe(g_nav_ikon[i], an ? C_GRUEN_T : C_NAV);
    }
}

lv_obj_t* seite(lv_obj_t* scr) {
    lv_obj_t* s = box(scr);
    lv_obj_set_size(s, W, H - KOPF_H - NAV_H);
    lv_obj_set_pos(s, 0, KOPF_H);
    lv_obj_set_style_pad_hor(s, S(16), 0);
    lv_obj_set_style_pad_ver(s, S(14), 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
    return s;
}

// ---------------------------------------------------------------- Wiegen
void et_oeffnen(int sorte, int rest);
std::string g_karte_schluessel;  // verhindert unnötiges Neuaufbauen der Karte
std::string g_spule_schluessel;  // Rolle im Ring nur neu zeichnen, wenn sich die Sorte ändert

constexpr int RING = K(168, 96), RING_W = K(14, 8), SPULE = K(128, 74);
constexpr int KS_PLATZ = K(34, 24), KS_BILD = K(50, 35);  // .rolle.r34

void haupt_cb(lv_event_t*) {
    switch (g_haupt) {
        case Haupt::VORSCHLAG:
            if (g_vorschlag >= 0 && g_akt) {
                const Filament& f = g_lager->sorten[g_vorschlag];
                g_auswahl_uid = f.uid;
                g_akt->tag_zuordnen(f.uid, g_aktiver_tag);
                g_karte_schluessel.clear();
            }
            break;
        case Haupt::SPEICHERN:
            if (g_aktive_sorte >= 0 && g_akt)
                g_akt->rest_speichern(g_lager->sorten[g_aktive_sorte].uid, g_netto, g_aktiver_tag);
            break;
        case Haupt::ZUORDNEN:
        case Haupt::WAEHLEN:
            g_auswahl_modus = true;
            g_auswahl_tag = g_aktiver_tag;
            zeige_seite(LAGER);
            break;
        default: break;
    }
}

void tara_cb(lv_event_t*) {
    if (g_akt) g_akt->tara();
}

void andere_cb(lv_event_t*) {
    g_auswahl_modus = true;
    g_auswahl_tag = g_aktiver_tag;
    zeige_seite(LAGER);
}

void etikett_wiegen_cb(lv_event_t*) {
    if (g_aktive_sorte < 0) return;
    bool da = g_st.brutto_g >= SPULE_DA_G;
    et_oeffnen(g_aktive_sorte, da && g_st.stabil ? g_netto : 0);
}

lv_obj_t* w_etikett_i;

void baue_wiegen(lv_obj_t* s) {
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s, S(14), 0);

    // .wiegen-karte: Ring mit Rolle, daneben Gewicht
    lv_obj_t* karte = box(s);
    panel_stil(karte, C_GRUEN);
    lv_obj_set_height(karte, lv_pct(100));
    lv_obj_set_flex_grow(karte, 1);
    lv_obj_set_style_pad_hor(karte, K(22, 10), 0);
    lv_obj_set_style_pad_ver(karte, K(18, 10), 0);
    lv_obj_set_flex_flow(karte, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(karte, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(karte, K(22, 10), 0);

    lv_obj_t* ring = box(karte);
    lv_obj_set_size(ring, RING, RING);
    w_arc = lv_arc_create(ring);
    lv_obj_remove_style(w_arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(w_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(w_arc, RING, RING);
    lv_arc_set_rotation(w_arc, 270);
    lv_arc_set_bg_angles(w_arc, 0, 360);
    lv_arc_set_range(w_arc, 0, 100);
    lv_arc_set_value(w_arc, 0);
    lv_obj_set_style_arc_width(w_arc, RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(w_arc, RING_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(w_arc, farbe(C_RAND2), LV_PART_MAIN);
    lv_obj_set_style_arc_color(w_arc, farbe(C_GRUEN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(w_arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(w_arc, false, LV_PART_MAIN);
    lv_obj_set_style_pad_all(w_arc, 0, 0);
    lv_obj_center(w_arc);
    w_spule = rolle_platz(ring, SPULE, SPULE, C_RAND, {"matt"}, true);
    lv_obj_center(w_spule);

    lv_obj_t* sp = spalte(karte, S(6));
    w_kopf = text(sp, T12, C_SUB, "FILAMENT ÜBRIG");
    ueberschrift(w_kopf);
    lv_obj_t* zahl = zeile(sp, S(8));
    lv_obj_set_flex_align(zahl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    w_zahl = text(zahl, F_Z, C_TEXT, "0");
    w_einheit = text(zahl, T30, C_SUB, "g");
    lv_obj_set_style_pad_bottom(w_einheit, K(12, 6), 0);
    w_proz = text(sp, T15, C_SUB, "Spule auflegen");
    lv_obj_t* chips = zeile(sp, S(8));
#if UI_KOMPAKT
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_COLUMN);  // nebeneinander zu breit
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(chips, 4, 0);
#endif
    lv_obj_set_style_pad_top(chips, S(8), 0);
    w_brutto = chip(chips, "Brutto 0 g", C_CHIP, C_TEXT);
    w_leer = chip(chips, "Leerspule 250 g", C_CHIP, C_TEXT);

    // .rechts270: Karte der erkannten Sorte und Knöpfe
    lv_obj_t* seite_r = box(s);
    lv_obj_set_size(seite_r, K(270, 172), lv_pct(100));
    lv_obj_set_flex_flow(seite_r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(seite_r, S(10), 0);

    lv_obj_t* k = box(seite_r);
    panel_stil(k, C_GRUEN);
    lv_obj_set_width(k, lv_pct(100));
    lv_obj_set_flex_grow(k, 1);
    lv_obj_set_style_pad_hor(k, K(16, 9), 0);
    lv_obj_set_style_pad_ver(k, K(14, 8), 0);
    lv_obj_set_flex_flow(k, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(k, K(8, 5), 0);
    w_k_titel = text(k, KT(&fl_12, &fl_12), C_SUB, "");
    if (!UI_KOMPAKT) ueberschrift(w_k_titel);
    einzeilig(w_k_titel, K(234, 150));
    lv_obj_t* kz = zeile(k, K(12, 7));
    w_k_swatch = box(kz);  // Platz für Rolle oder Farbfeld
    lv_obj_set_size(w_k_swatch, KS_PLATZ, KS_PLATZ);
    lv_obj_add_flag(w_k_swatch, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_t* kt = spalte(kz, S(2));
    w_k_name = text(kt, T17, C_TEXT, "");
    einzeilig(w_k_name, K(188, 112));
    w_k_sub = text(kt, T13, C_SUB, "");
    einzeilig(w_k_sub, K(188, 112));
    w_k_chips = box(k);
    lv_obj_set_size(w_k_chips, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(w_k_chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(w_k_chips, S(6), 0);
    lv_obj_set_style_pad_row(w_k_chips, S(6), 0);

    w_haupt = knopf(seite_r, "Rest speichern", Art::PRIMAER, haupt_cb, nullptr, &w_haupt_lbl);
    lv_obj_set_size(w_haupt, lv_pct(100), K(58, 38));
    lv_obj_set_style_text_font(w_haupt_lbl, T17, 0);
    lv_obj_t* r2 = box(seite_r);
    lv_obj_set_size(r2, lv_pct(100), K(50, 36));
    lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r2, S(10), 0);
    w_tara = knopf(r2, "Tara", Art::RAHMEN, tara_cb);
    w_wahl = knopf(r2, "Sorte wählen", Art::RAHMEN, andere_cb, nullptr, &w_wahl_lbl);
    for (lv_obj_t* b : {w_tara, w_wahl}) {
        lv_obj_set_height(b, lv_pct(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 4, 0);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        lv_obj_set_style_text_font(l, T15, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
    // „Sorte wählen“ bricht wie im Browser um, wenn der Platz nicht reicht
    if (UI_KOMPAKT) {
        lv_obj_set_style_text_font(w_wahl_lbl, &fl_12, 0);
        lv_obj_set_style_pad_hor(w_wahl, 1, 0);
    }
    if (UI_KOMPAKT) {
        lv_label_set_long_mode(w_wahl_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(w_wahl_lbl, lv_pct(100));
        lv_obj_set_style_text_line_space(w_wahl_lbl, -2, 0);
    }
    // Etikett drucken: quadratischer Knopf mit Druckersymbol
    w_etikett = box(r2);
    lv_obj_add_flag(w_etikett, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(w_etikett, K(58, 40), lv_pct(100));
    lv_obj_set_style_radius(w_etikett, S(14), 0);
    lv_obj_set_style_border_width(w_etikett, 2, 0);
    lv_obj_set_style_border_color(w_etikett, farbe(C_RAND), 0);
    lv_obj_set_style_bg_color(w_etikett, farbe(C_LINIE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(w_etikett, LV_OPA_COVER, LV_STATE_PRESSED);
    w_etikett_i = ikon(w_etikett, "drucker", K(22, 16), C_TEXT);
    lv_obj_center(w_etikett_i);
    lv_obj_add_event_cb(w_etikett, etikett_wiegen_cb, LV_EVENT_CLICKED, nullptr);
}

std::string ams_text(const AmsTreffer& t, bool mit_rest) {
    std::string s = t.drucker->name + " · " + ams_slot_kurz(*t.ams, *t.slot);
    if (mit_rest && t.slot->rest >= 0) s += " · " + std::to_string(t.slot->rest) + " %";
    return s;
}

void karte_chips(const std::vector<std::pair<std::string, int>>& chips) {
    // 0 = grau, 1 = grün (.g), 2 = orange Rahmen (.o), 3 = blau (.b / .ams)
    lv_obj_clean(w_k_chips);
    for (auto& c : chips) {
        switch (c.second) {
            case 1: chip(w_k_chips, c.first, C_CHIP_G, C_GRUEN_T); break;
            case 2: chip(w_k_chips, c.first, 0, C_ORANGE_T, 0xa16e22); break;
            case 3: chip(w_k_chips, c.first, C_CHIP_B, C_BLAU_T); break;
            default: chip(w_k_chips, c.first, C_CHIP, C_TEXT); break;
        }
    }
}

// Platz links in der Karte: Rolle der Sorte oder ein leeres Farbfeld (.swatch.s34)
void karte_swatch(const Filament* f, uint32_t leer_farbe) {
    lv_obj_clean(w_k_swatch);
    if (f) {
        lv_obj_set_style_bg_opa(w_k_swatch, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(w_k_swatch, 0, 0);
        lv_obj_t* i = lv_img_create(w_k_swatch);
        lv_img_set_src(i, rolle_bild(f->rgb, f->besonderheiten, KS_BILD));
        lv_obj_center(i);
    } else {
        lv_obj_set_style_bg_opa(w_k_swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(w_k_swatch, farbe(leer_farbe), 0);
        lv_obj_set_style_radius(w_k_swatch, S(9), 0);
        lv_obj_set_style_border_width(w_k_swatch, 1, 0);
        lv_obj_set_style_border_color(w_k_swatch, farbe(0x4a5058), 0);
    }
}

std::string gross_text(std::string s) {  // .ueber: text-transform uppercase (UTF-8 für ä ö ü)
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = s[i + 1];
            if (d == 0xA4 || d == 0xB6 || d == 0xBC) s[i + 1] = (char)(d - 0x20);
            i++;
        } else if (c < 0x80) {
            s[i] = (char)toupper(c);
        }
    }
    return s;
}

void wiegen_aktualisieren() {
    const WaagenStatus& st = g_st;
    std::string tag = !st.tag_r.empty() ? st.tag_r : st.tag_l;
    const char* leser = !st.tag_r.empty() ? "rechts" : "links";
    bool da = st.brutto_g >= SPULE_DA_G;
    if (g_spule_war_da && !da) g_auswahl_uid.clear();  // Spule abgenommen
    g_spule_war_da = da;

    int sorte = g_lager ? g_lager->finde_nfc(tag) : -1;
    if (sorte < 0 && !g_auswahl_uid.empty() && g_lager) sorte = g_lager->finde_uid(g_auswahl_uid);
    g_aktive_sorte = sorte;
    g_aktiver_tag = tag;

    int leer = leergewicht(sorte);
    int brutto = (int)(st.brutto_g + 0.5f);
    int netto = std::max(0, brutto - leer);
    g_netto = netto;
    int soll = sorte >= 0 ? g_lager->sorten[sorte].gewicht : 1000;
    int proz = da ? std::min(100, netto * 100 / std::max(1, soll)) : 0;

    lv_arc_set_value(w_arc, proz);
    lv_obj_set_style_arc_color(w_arc, farbe(proz < 15 && da ? C_ROT : C_GRUEN), LV_PART_INDICATOR);
    // Rolle im Ring in der Farbe der Sorte, sonst grau und blass (.rgross.leer)
    std::string sk = sorte >= 0 ? g_lager->sorten[sorte].uid + "|" + std::to_string(g_lager->sorten[sorte].rgb) : "";
    if (sk != g_spule_schluessel) {
        g_spule_schluessel = sk;
        if (sorte >= 0) rolle_tauschen(w_spule, SPULE, g_lager->sorten[sorte].rgb, g_lager->sorten[sorte].besonderheiten);
        else rolle_tauschen(w_spule, SPULE, C_RAND, {"matt"}, true);
    }
    setze(w_zahl, std::to_string(da ? netto : 0));
    if (!st.sensor_ok) setze(w_proz, "Sensor antwortet nicht");
    else if (!da) setze(w_proz, "Spule auflegen");
    else setze(w_proz, std::to_string(proz) + " % von " + gramm(soll) + (st.stabil ? "" : " · misst …"));
    setze(lv_obj_get_child(w_brutto, 0), "Brutto " + gramm(brutto));
    setze(lv_obj_get_child(w_leer, 0), "Leerspule " + gramm(leer));

    std::string schluessel = std::to_string(sorte) + "|" + tag + "|" + (da ? "1" : "0") + "|" + std::to_string(g_ams_stand);
    if (sorte >= 0) schluessel += "|" + g_lager->sorten[sorte].description;
    if (sorte >= 0) lv_obj_clear_state(w_etikett, LV_STATE_DISABLED);
    else lv_obj_add_state(w_etikett, LV_STATE_DISABLED);
    lv_obj_set_style_opa(w_etikett, sorte >= 0 ? LV_OPA_COVER : LV_OPA_40, 0);
    if (schluessel == g_karte_schluessel) return;
    g_karte_schluessel = schluessel;
    AmsTreffer ams = g_ams ? ams_slot_fuer_tag(*g_ams, tag) : AmsTreffer{};
    g_vorschlag = -1;

    if (sorte >= 0) {
        const Filament& f = g_lager->sorten[sorte];
        setze(w_k_titel, gross_text(tag.empty() ? "Gewählte Sorte" : std::string(KT("Erkannt · NFC ", "NFC ")) + leser));
        karte_swatch(&f, 0);
        setze(w_k_name, f.kurzname());
        setze(w_k_sub, f.hersteller);
        std::vector<std::pair<std::string, int>> c;
        for (size_t i = 0; i < f.besonderheiten.size() && i < 2; i++) c.push_back({f.besonderheiten[i], 0});
        c.push_back({spulen_text(f.spulen) + " im Lager", 1});
        if (!f.reste.empty()) c.push_back({"Rest bisher " + gramm(f.reste.back()), 2});
        if (ams) c.push_back({ams_text(ams, true), 3});
        karte_chips(c);
        g_haupt = da ? Haupt::SPEICHERN : Haupt::NICHTS;
        setze(w_haupt_lbl, LV_SYMBOL_OK "  Rest speichern");
        setze(w_wahl_lbl, "Andere Sorte");
    } else if (!tag.empty()) {
        setze(w_k_titel, gross_text(std::string(KT("Neuer Tag · NFC ", "Neu · NFC ")) + leser));
        karte_swatch(nullptr, C_RAND);
        setze(w_k_name, "Neue Spule");
        setze(w_k_sub, "UID " + tag);
        g_vorschlag = ams && g_lager ? ams_vorschlag(*g_lager, *ams.slot) : -1;
        if (ams)
            karte_chips({{ams_text(ams, false), 3},
                         {g_vorschlag >= 0 ? "Vorschlag: " + g_lager->sorten[g_vorschlag].kurzname() : ams.slot->name, 1}});
        else
            karte_chips({{"Keiner Sorte zugeordnet", 2}});
        if (g_vorschlag >= 0) {
            g_haupt = Haupt::VORSCHLAG;
            setze(w_haupt_lbl, "Als " + g_lager->sorten[g_vorschlag].kurzname() + " speichern");
        } else {
            g_haupt = Haupt::ZUORDNEN;
            setze(w_haupt_lbl, "Sorte zuordnen");
        }
        setze(w_wahl_lbl, "Sorte wählen");
    } else {
        setze(w_k_titel, gross_text("Keine Spule erkannt"));
        karte_swatch(nullptr, C_RAND2);
        setze(w_k_name, da ? "Ohne NFC-Tag" : "Waage ist leer");
        setze(w_k_sub, da ? "Sorte aus dem Lager wählen" : "Spule mit Tag auflegen");
        karte_chips({});
        g_haupt = da ? Haupt::WAEHLEN : Haupt::NICHTS;
        setze(w_haupt_lbl, da ? "Sorte wählen" : LV_SYMBOL_OK "  Rest speichern");
        setze(w_wahl_lbl, "Sorte wählen");
    }
    if (!UI_KOMPAKT) lv_obj_set_style_min_width(w_wahl, text_breite(lv_label_get_text(w_wahl_lbl), T15) + 24, 0);
    lv_label_set_long_mode(w_haupt_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(w_haupt_lbl, LV_SIZE_CONTENT);
    if (lv_obj_get_width(w_haupt) > 0 && text_breite(lv_label_get_text(w_haupt_lbl), T17) > lv_obj_get_width(w_haupt) - 24)
        lv_obj_set_width(w_haupt_lbl, lv_obj_get_width(w_haupt) - 24);
    knopf_aktiv(w_haupt, g_haupt != Haupt::NICHTS);
}

// ---------------------------------------------------------------- Lager (wie in SchichtLohn: „Nach …“, Filter, Gruppen)
void lager_liste_bauen();

struct LagerOpt {
    std::string gruppe = "hersteller";            // material, hersteller, farbe, besonderheit, bestand
    std::string fl_material, fl_hersteller, fl_farbe, fl_ort;
    std::vector<std::string> auf;                 // aufgeklappte Gruppen – alle anderen sind zu
    bool filter_auf = false;
} g_lo;
std::string g_dd_offen;  // "gruppe", "f-material", … oder leer
lv_obj_t *l_suchbox, *l_suche_n, *l_leiste, *l_filterzeile, *l_dd_ebene;

const char* const FAM[] = {"Schwarz", "Grau", "Weiß", "Braun", "Beige", "Rot", "Orange", "Gelb", "Grün", "Türkis", "Blau", "Lila", "Pink"};
const uint32_t FAM_HEX[] = {0x111111, 0x8a8a8a, 0xf2f2f2, 0x6d4c41, 0xd8c3a0, 0xd32f2f, 0xf57c00, 0xfdd835, 0x43a047, 0x26a69a, 0x1e63c8, 0x7b3fb0, 0xec5f9a};
int fam_index(const std::string& n) {
    for (int i = 0; i < 13; i++)
        if (n == FAM[i]) return i;
    return 99;
}

// Farbfamilie wie familie() in app.js
std::string familie(uint32_t rgb) {
    float r = ((rgb >> 16) & 0xFF) / 255.f, g = ((rgb >> 8) & 0xFF) / 255.f, b = (rgb & 0xFF) / 255.f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), l = (mx + mn) / 2, d = mx - mn;
    float s = d == 0 ? 0 : d / (1 - std::fabs(2 * l - 1));
    float hue = 0;
    if (d > 0) hue = (mx == r ? std::fmod((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4) * 60;
    hue = std::fmod(hue + 360, 360.f);
    if (l < 0.13f) return "Schwarz";
    if (l > 0.9f && s < 0.5f) return "Weiß";
    if (s < 0.15f) return l > 0.82f ? "Weiß" : "Grau";
    if (hue >= 10 && hue < 45 && (l < 0.4f || (l < 0.5f && s < 0.6f))) return "Braun";
    if (hue >= 20 && hue < 55 && l >= 0.55f && (s < 0.5f || l > 0.75f)) return "Beige";
    if (hue < 15 || hue >= 345) return "Rot";
    if (hue < 40) return "Orange";
    if (hue < 70) return "Gelb";
    if (hue < 165) return "Grün";
    if (hue < 195) return "Türkis";
    if (hue < 255) return "Blau";
    if (hue < 300) return "Lila";
    return "Pink";
}

std::string material_name(const Filament& f) {
    std::string m = f.material;
    while (!m.empty() && m.back() == ' ') m.pop_back();
    return m.empty() ? "OHNE MATERIAL" : gross_text(m);
}

std::string gruppe_von(const Filament& f, const std::string& art) {
    if (art == "hersteller") return f.hersteller.empty() ? "Ohne Marke" : f.hersteller;
    if (art == "farbe") return familie(f.rgb);
    if (art == "besonderheit") return f.besonderheiten.empty() ? "Ohne Besonderheit" : f.besonderheiten[0];
    return material_name(f);
}

// Sortierschlüssel ohne Groß/Klein und Umlaute (localeCompare mit sensitivity: base)
std::string normal(const std::string& t) {
    std::string o;
    for (size_t i = 0; i < t.size(); i++) {
        unsigned char c = t[i];
        if (c == 0xC3 && i + 1 < t.size()) {
            unsigned char d = t[++i];
            if (d == 0xA4 || d == 0x84) o += "ae";
            else if (d == 0xB6 || d == 0x96) o += "oe";
            else if (d == 0xBC || d == 0x9C) o += "ue";
            else if (d == 0x9F) o += "ss";
            else o += 'e';
        } else {
            o += (char)tolower(c);
        }
    }
    return o;
}

bool gruppe_vor(const std::string& art, const std::string& a, const std::string& b) {
    if (art == "farbe") return fam_index(a) < fam_index(b);
    bool oa = a.rfind("Ohne ", 0) == 0, ob = b.rfind("Ohne ", 0) == 0;
    if (oa != ob) return ob;
    return normal(a) < normal(b);
}

std::string kg_text(int g) {
    if (g < 1000) return std::to_string(g) + " g";
    int z = (g + 50) / 100;  // Zehntel kg
    std::string s = std::to_string(z / 10);
    if (z % 10) s += "," + std::to_string(z % 10);
    return s + " kg";
}

bool passt_filter(const Filament& f) {
    if (!g_lo.fl_material.empty() && material_name(f) != g_lo.fl_material) return false;
    if (!g_lo.fl_hersteller.empty() && (f.hersteller.empty() ? "Ohne Marke" : f.hersteller) != g_lo.fl_hersteller) return false;
    if (!g_lo.fl_farbe.empty() && familie(f.rgb) != g_lo.fl_farbe) return false;
    if (g_lo.fl_ort == "angebrochen" && f.reste.empty()) return false;
    if (g_lo.fl_ort == "voll" && (!f.reste.empty() || !f.spulen)) return false;
    if (g_lo.fl_ort == "leer" && f.spulen > 0) return false;
    return true;
}

bool gruppe_offen(const std::string& g) { return std::find(g_lo.auf.begin(), g_lo.auf.end(), g) != g_lo.auf.end(); }

void zeile_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_lager || i < 0 || i >= (int)g_lager->sorten.size()) return;
    const Filament& f = g_lager->sorten[i];
    if (g_auswahl_modus) {
        g_auswahl_modus = false;
        g_auswahl_uid = f.uid;
        if (!g_auswahl_tag.empty() && !f.hat_nfc(g_auswahl_tag) && g_akt) g_akt->tag_zuordnen(f.uid, g_auswahl_tag);
        g_karte_schluessel.clear();
        zeige_seite(WIEGEN);
    } else {
        g_detail = i;
        zeige_seite(DETAIL);
    }
}

std::vector<std::string> g_gruppen_namen;  // für die Klicks auf Gruppenköpfe

void gruppe_cb(lv_event_t* e) {
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (i >= g_gruppen_namen.size()) return;
    const std::string g = g_gruppen_namen[i];
    auto it = std::find(g_lo.auf.begin(), g_lo.auf.end(), g);
    if (it != g_lo.auf.end()) g_lo.auf.erase(it);
    else g_lo.auf.push_back(g);
    lv_coord_t y = lv_obj_get_scroll_y(l_liste);
    lager_liste_bauen();
    lv_obj_scroll_to_y(l_liste, y, LV_ANIM_OFF);
}

void suche_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(l_tastatur, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(l_tastatur);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        lager_liste_bauen();
    }
}

void tastatur_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(l_tastatur, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(l_suche, LV_STATE_FOCUSED);
    }
}

// --- Auswahlknöpfe (.dd) und ihre Listen (.dd-panel)
constexpr int DD_H = K(40, 28);

lv_obj_t* dd_knopf(lv_obj_t* p, const std::string& t, const char* ikon_l, const char* ikon_r, bool an, bool auf,
                   lv_event_cb_t cb, void* daten, int zahl = 0) {
    lv_obj_t* b = box(p);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(b, DD_H);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, K(12, 9), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, farbe(an || auf ? C_GRUEN : C_RAND), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, farbe(C_PANEL), 0);
    lv_obj_set_style_bg_color(b, farbe(C_LINIE), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(b, t.empty() ? 0 : K(12, 7), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, K(7, 4), 0);
    uint32_t fg = an ? C_GRUEN_T : C_TEXT;
    if (ikon_l) ikon(b, ikon_l, K(16, 13), fg);
    if (!t.empty()) {
        lv_obj_t* l = text(b, KT(&fl_15, &fl_13), fg, t.c_str());
        lv_obj_set_style_text_font(l, KT(&fl_15, &fl_13), 0);
    }
    if (zahl) {
        lv_obj_t* z = box(b);
        lv_obj_set_size(z, LV_SIZE_CONTENT, K(20, 16));
        lv_obj_set_style_min_width(z, K(20, 16), 0);
        lv_obj_set_style_radius(z, K(10, 8), 0);
        lv_obj_set_style_bg_opa(z, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(z, farbe(C_GRUEN), 0);
        lv_obj_set_style_pad_hor(z, K(6, 4), 0);
        lv_obj_t* zl = text(z, T12, C_BG, std::to_string(zahl).c_str());
        lv_obj_center(zl);
    }
    if (ikon_r) ikon(b, ikon_r, K(16, 13), fg);
    if (t.empty()) lv_obj_set_width(b, DD_H);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, daten);
    return b;
}

void leiste_bauen();

struct DdWahl {
    std::string art, wert;  // art: "gruppe" oder Filterschlüssel
};
std::vector<DdWahl> g_dd_wahl;

void dd_schliessen() {
    if (l_dd_ebene) lv_obj_del(l_dd_ebene);
    l_dd_ebene = nullptr;
    g_dd_offen.clear();
}

void dd_ebene_cb(lv_event_t*) {
    dd_schliessen();
    lager_liste_bauen();
}

void dd_wahl_cb(lv_event_t* e) {
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (i >= g_dd_wahl.size()) return;
    DdWahl w = g_dd_wahl[i];
    if (w.art == "gruppe") {
        g_lo.gruppe = w.wert;
        g_lo.auf.clear();
    } else if (w.art == "material") g_lo.fl_material = w.wert;
    else if (w.art == "hersteller") g_lo.fl_hersteller = w.wert;
    else if (w.art == "farbe") g_lo.fl_farbe = w.wert;
    else if (w.art == "ort") g_lo.fl_ort = w.wert;
    dd_schliessen();
    lager_liste_bauen();
}

// Liste unter dem Knopf anker öffnen: Einträge (Wert, Text, Anzahl/Zusatz, Farbpunkt)
struct DdEintrag {
    std::string wert, text, klein;
    uint32_t punkt = 0;
    bool trenner_danach = false;
};

void dd_liste(lv_obj_t* anker, const std::string& art, const std::vector<DdEintrag>& eintraege, const std::string& aktuell,
              bool rechts) {
    dd_schliessen();
    g_dd_offen = art == "gruppe" ? "gruppe" : "f-" + art;
    // Fläche über allem: ein Tipp daneben schließt die Liste
    l_dd_ebene = box(lv_layer_top());
    lv_obj_set_size(l_dd_ebene, W, H);
    lv_obj_add_flag(l_dd_ebene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(l_dd_ebene, dd_ebene_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* p = box(l_dd_ebene);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);  // Tipps in die Liste schließen sie nicht
    lv_obj_set_style_bg_color(p, farbe(C_KACHEL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_border_color(p, farbe(C_RAND), 0);
    lv_obj_set_style_radius(p, K(14, 10), 0);
    lv_obj_set_style_pad_all(p, K(6, 4), 0);
    lv_obj_set_style_shadow_width(p, 30, 0);
    lv_obj_set_style_shadow_color(p, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(p, LV_OPA_50, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_area_t a;
    lv_obj_get_coords(anker, &a);
    const int breite = std::max(K(220, 160), (int)lv_area_get_width(&a));
    const int max_h = std::min(K(300, 200), H - a.y2 - 6 - 6);
    lv_obj_set_size(p, breite, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(p, max_h, 0);
    int x = rechts ? a.x2 - breite + 1 : a.x1;
    x = std::max(4, std::min(x, W - breite - 4));
    lv_obj_set_pos(p, x, a.y2 + K(6, 4));
    leiste_bauen();  // Knopf der offenen Liste grün umranden (.dd.auf)
    for (const auto& e : eintraege) {
        bool an = e.wert == aktuell;
        lv_obj_t* o = box(p);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(o, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(o, K(10, 7), 0);
        lv_obj_set_style_radius(o, K(9, 7), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, farbe(an ? C_NAV_AKT : C_KACHEL), 0);
        lv_obj_set_style_bg_color(o, farbe(C_LINIE), LV_STATE_PRESSED);
        lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(o, K(8, 6), 0);
        if (e.punkt) {
            lv_obj_t* d = box(o);
            lv_obj_set_size(d, K(12, 10), K(12, 10));
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d, farbe(e.punkt), 0);
            lv_obj_set_style_border_width(d, 1, 0);
            lv_obj_set_style_border_color(d, farbe(0x55595f), 0);
        }
        lv_obj_t* t = text(o, T14, an ? C_GRUEN_T : C_TEXT, e.text.c_str());
        lv_obj_set_flex_grow(t, 1);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        if (!e.klein.empty()) text(o, T12, an ? C_GRUEN_T : C_SUB, e.klein.c_str());
        g_dd_wahl.push_back({art, e.wert});
        lv_obj_add_event_cb(o, dd_wahl_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(g_dd_wahl.size() - 1));
        if (e.trenner_danach) {
            lv_obj_t* tr = box(p);
            lv_obj_set_size(tr, lv_pct(100), 1);
            lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(tr, farbe(C_LINIE), 0);
        }
    }
}

const char* const GRUPPEN[][2] = {{"material", "Material"}, {"hersteller", "Marke"}, {"farbe", "Farbe"},
                                  {"besonderheit", "Besonderheit"}, {"bestand", "Bestand"}};
const char* gruppe_name() {
    for (auto& g : GRUPPEN)
        if (g_lo.gruppe == g[0]) return g[1];
    return "Marke";
}
const char* const FILTER_K[] = {"material", "hersteller", "farbe", "ort"};
const char* const FILTER_TITEL[] = {"Material", "Marke", "Farbe", "Bestand"};
const char* const FILTER_ALLE[] = {"Alle Materialien", "Alle Marken", "Alle Farben", "Überall"};
const char* const ORTE[][2] = {{"angebrochen", "Angebrochen"}, {"voll", "Nur volle"}, {"leer", "Aufgebraucht"}};

std::string& filter_wert(const std::string& k) {
    if (k == "material") return g_lo.fl_material;
    if (k == "hersteller") return g_lo.fl_hersteller;
    if (k == "farbe") return g_lo.fl_farbe;
    return g_lo.fl_ort;
}

void gruppe_dd_cb(lv_event_t* e) {
    if (g_dd_offen == "gruppe") {
        dd_schliessen();
        return leiste_bauen();
    }
    g_dd_wahl.clear();
    std::vector<DdEintrag> v;
    for (auto& g : GRUPPEN) v.push_back({g[0], std::string("Nach ") + g[1], !strcmp(g[0], "bestand") ? "WENIG ZUERST" : ""});
    dd_liste(lv_event_get_current_target(e), "gruppe", v, g_lo.gruppe, false);
}

void filter_dd_cb(lv_event_t* e) {
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    std::string art = FILTER_K[k];
    if (g_dd_offen == "f-" + art) {
        dd_schliessen();
        return leiste_bauen();
    }
    g_dd_wahl.clear();
    std::vector<DdEintrag> v;
    v.push_back({"", FILTER_ALLE[k], std::to_string(g_lager ? g_lager->sorten.size() : 0)});
    v.back().trenner_danach = true;
    if (g_lager) {
        if (art == "ort") {
            for (auto& o : ORTE) {
                int n = 0;
                for (const Filament& f : g_lager->sorten) {
                    if (!strcmp(o[0], "angebrochen")) n += !f.reste.empty();
                    else if (!strcmp(o[0], "leer")) n += !f.spulen;
                    else n += f.spulen && f.reste.empty();
                }
                v.push_back({o[0], o[1], std::to_string(n)});
            }
        } else {
            std::vector<std::pair<std::string, int>> z;
            for (const Filament& f : g_lager->sorten) {
                std::string w = art == "material" ? material_name(f) : art == "hersteller" ? (f.hersteller.empty() ? "Ohne Marke" : f.hersteller) : familie(f.rgb);
                auto it = std::find_if(z.begin(), z.end(), [&](const std::pair<std::string, int>& x) { return x.first == w; });
                if (it == z.end()) z.push_back({w, 1});
                else it->second++;
            }
            std::sort(z.begin(), z.end(), [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                return gruppe_vor(art, a.first, b.first);
            });
            for (auto& x : z) {
                DdEintrag d{x.first, x.first, std::to_string(x.second)};
                if (art == "farbe") d.punkt = FAM_HEX[std::min(12, fam_index(x.first))];
                v.push_back(d);
            }
        }
    }
    dd_liste(lv_event_get_current_target(e), art, v, filter_wert(art), k == 3);
}

void filter_auf_cb(lv_event_t*) {
    g_lo.filter_auf = !g_lo.filter_auf;
    dd_schliessen();
    lager_liste_bauen();
}

void filter_weg_cb(lv_event_t*) {
    g_lo.fl_material.clear();
    g_lo.fl_hersteller.clear();
    g_lo.fl_farbe.clear();
    g_lo.fl_ort.clear();
    dd_schliessen();
    lager_liste_bauen();
}

void alle_zu_cb(lv_event_t*) {
    if (!g_lo.auf.empty()) g_lo.auf.clear();
    else if (g_lager)
        for (const Filament& f : g_lager->sorten) {
            std::string g = gruppe_von(f, g_lo.gruppe);
            if (!gruppe_offen(g)) g_lo.auf.push_back(g);
        }
    lager_liste_bauen();
}

void leiste_bauen() {
    lv_obj_clean(l_leiste);
    int n_f = !g_lo.fl_material.empty() + !g_lo.fl_hersteller.empty() + !g_lo.fl_farbe.empty() + !g_lo.fl_ort.empty();
    dd_knopf(l_leiste, std::string(KT("Nach ", "")) + gruppe_name(), nullptr, g_dd_offen == "gruppe" ? "hoch" : "runter", false,
             g_dd_offen == "gruppe", gruppe_dd_cb, nullptr);
    dd_knopf(l_leiste, "Filter", "filter", nullptr, g_lo.filter_auf || n_f, false, filter_auf_cb, nullptr, n_f);
    if (g_lo.gruppe != "bestand") dd_knopf(l_leiste, "", g_lo.auf.empty() ? "auf" : "zu", nullptr, false, false, alle_zu_cb, nullptr);

    lv_obj_clean(l_filterzeile);
    if (!g_lo.filter_auf) {
        lv_obj_add_flag(l_filterzeile, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(l_filterzeile, LV_OBJ_FLAG_HIDDEN);
    for (int k = 0; k < 4; k++) {
        const std::string& w = filter_wert(FILTER_K[k]);
        std::string t = w.empty() ? FILTER_TITEL[k] : w;
        if (k == 3 && !w.empty())
            for (auto& o : ORTE)
                if (w == o[0]) t = o[1];
        bool auf = g_dd_offen == std::string("f-") + FILTER_K[k];
        lv_obj_t* b = dd_knopf(l_filterzeile, t, nullptr, auf ? "hoch" : "runter", !w.empty(), auf, filter_dd_cb, (void*)(intptr_t)k);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_width(b, 0);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(b, K(10, 6), 0);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        lv_obj_set_style_text_font(l, T14, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(l, 1);
        lv_obj_set_width(l, 0);
        lv_obj_set_height(l, lv_font_get_line_height(T14));
    }
    if (n_f) dd_knopf(l_filterzeile, "", "x", nullptr, false, false, filter_weg_cb, nullptr);
}

void baue_lager(lv_obj_t* s) {
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s, S(14), 0);

    lv_obj_t* links = box(s);
    lv_obj_set_height(links, lv_pct(100));
    lv_obj_set_flex_grow(links, 1);
    lv_obj_set_width(links, 0);
    lv_obj_set_flex_flow(links, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(links, S(10), 0);

    lv_obj_t* oben = box(links);
    lv_obj_set_size(oben, lv_pct(100), DD_H);
    lv_obj_set_flex_flow(oben, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(oben, S(8), 0);
    // .suche: Lupe, Eingabe, Trefferzahl
    l_suchbox = box(oben);
    lv_obj_set_height(l_suchbox, DD_H);
    lv_obj_set_flex_grow(l_suchbox, 1);
    lv_obj_set_width(l_suchbox, 0);
    lv_obj_set_style_min_width(l_suchbox, K(92, 64), 0);
    lv_obj_set_style_bg_opa(l_suchbox, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(l_suchbox, farbe(C_PANEL), 0);
    lv_obj_set_style_border_width(l_suchbox, 1, 0);
    lv_obj_set_style_border_color(l_suchbox, farbe(C_RAND2), 0);
    lv_obj_set_style_radius(l_suchbox, K(12, 9), 0);
    lv_obj_set_style_pad_hor(l_suchbox, K(12, 6), 0);
    lv_obj_set_flex_flow(l_suchbox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l_suchbox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(l_suchbox, K(8, 4), 0);
    ikon(l_suchbox, "lupe", K(18, 13), C_SUB);
    l_suche = lv_textarea_create(l_suchbox);
    lv_obj_remove_style_all(l_suche);
    lv_textarea_set_one_line(l_suche, true);
    lv_textarea_set_placeholder_text(l_suche, "Suchen …");
    lv_obj_set_flex_grow(l_suche, 1);
    lv_obj_set_width(l_suche, 0);
    lv_obj_set_height(l_suche, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(l_suche, T14, 0);
    lv_obj_set_style_text_color(l_suche, farbe(C_TEXT), 0);
    lv_obj_set_style_text_color(l_suche, farbe(C_SUB), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_side(l_suche, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_width(l_suche, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_color(l_suche, farbe(C_GRUEN), LV_PART_CURSOR);
    lv_obj_set_style_anim_time(l_suche, 400, LV_PART_CURSOR);
    lv_obj_add_event_cb(l_suche, suche_cb, LV_EVENT_ALL, nullptr);
    l_suche_n = text(l_suchbox, T12, C_SUB, "");
    l_leiste = box(oben);
    lv_obj_set_size(l_leiste, LV_SIZE_CONTENT, DD_H);
    lv_obj_set_flex_flow(l_leiste, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(l_leiste, K(8, 5), 0);

    l_filterzeile = box(links);
    lv_obj_set_size(l_filterzeile, lv_pct(100), DD_H);
    lv_obj_set_flex_flow(l_filterzeile, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(l_filterzeile, K(8, 5), 0);
    lv_obj_add_flag(l_filterzeile, LV_OBJ_FLAG_HIDDEN);

    l_liste = box(links);
    panel_stil(l_liste, C_GRUEN);
    lv_obj_set_width(l_liste, lv_pct(100));
    lv_obj_set_flex_grow(l_liste, 1);
    lv_obj_set_style_pad_hor(l_liste, K(14, 8), 0);
    lv_obj_set_style_pad_ver(l_liste, K(4, 2), 0);
    lv_obj_set_flex_flow(l_liste, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(l_liste, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(l_liste, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l_liste, LV_SCROLLBAR_MODE_ACTIVE);

    // .reste: angebrochene Rollen
    l_reste = box(s);
    panel_stil(l_reste, C_ORANGE);
    lv_obj_set_size(l_reste, K(232, 150), lv_pct(100));
    lv_obj_set_style_pad_hor(l_reste, K(14, 8), 0);
    lv_obj_set_style_pad_ver(l_reste, K(12, 8), 0);
    lv_obj_set_flex_flow(l_reste, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l_reste, K(8, 5), 0);
    lv_obj_add_flag(l_reste, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(l_reste, LV_DIR_VER);

    l_tastatur = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(l_tastatur, W, S(220));
    lv_obj_align(l_tastatur, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(l_tastatur, l_suche);
    lv_obj_add_event_cb(l_tastatur, tastatur_cb, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(l_tastatur, LV_OBJ_FLAG_HIDDEN);
}

constexpr int Z_PLATZ = K(30, 22), Z_BILD = K(46, 34);  // .rolle.r30
constexpr int R_PLATZ = K(22, 16), R_BILD = K(32, 24);  // .rolle.r22

void lager_zeile(lv_obj_t* liste, const Filament& f, int index, bool vorschlag) {
    lv_obj_t* z = box(liste);
    lv_obj_set_size(z, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(z, K(9, 5), 0);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(z, farbe(C_LINIE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(z, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(z, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(z, 1, 0);
    lv_obj_set_style_border_color(z, farbe(C_LINIE), 0);
    lv_obj_set_flex_flow(z, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(z, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(z, K(12, 7), 0);
    lv_obj_add_event_cb(z, zeile_cb, LV_EVENT_CLICKED, (void*)(intptr_t)index);

    rolle_platz(z, Z_PLATZ, Z_BILD, f.rgb, f.besonderheiten);
    lv_obj_t* mitte = spalte(z, S(2));
    lv_obj_set_flex_grow(mitte, 1);
    lv_obj_set_width(mitte, 0);
    lv_obj_t* n = text(mitte, T15, C_TEXT, f.kurzname().c_str());
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_set_size(n, lv_pct(100), lv_font_get_line_height(T15));
    // Unterzeile: Herstellerlogo (oder Name) · Besonderheiten · Lagerort
    lv_obj_t* u = zeile(mitte, 0);
    lv_obj_set_width(u, lv_pct(100));
    std::string rest;
    for (size_t i = 0; i < f.besonderheiten.size() && i < 2; i++) rest += " · " + f.besonderheiten[i];
    if (!f.ort.empty()) rest += " · " + f.ort;
    int mb = 0;
    if (!f.hersteller.empty()) {
        lv_obj_t* m = marke(u, f.hersteller, LOGO_LISTE, T13, C_SUB, LOGO_W[LOGO_LISTE], &mb);
        lv_obj_set_style_pad_right(m, 0, 0);
    } else if (!rest.empty()) {
        rest = rest.substr(4);  // führendes „ · “ weg
    }
    if (!rest.empty()) {
        lv_obj_t* r = text(u, T13, C_SUB, rest.c_str());
        lv_label_set_long_mode(r, LV_LABEL_LONG_DOT);
        lv_obj_set_height(r, lv_font_get_line_height(T13));
        lv_obj_set_flex_grow(r, 1);
        lv_obj_set_width(r, 0);
    }
    if (vorschlag) chip(z, "Vorschlag aus AMS", C_CHIP_B, C_BLAU_T);
    else if (!f.nfc.empty()) chip(z, "NFC", C_CHIP_B, C_BLAU_T);
    lv_obj_t* r = spalte(z, 0);
    lv_obj_set_width(r, K(86, 62));
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t* a = text(r, KT(&fl_15, &fl_13), f.spulen ? C_TEXT : C_ROT, spulen_text(f.spulen).c_str());
    lv_obj_set_style_text_font(a, KT(&fl_15, &fl_13), 0);
    text(r, T13, C_SUB, rest_text(f).c_str());
}

void gruppen_kopf(lv_obj_t* liste, const std::string& name, const std::string& art, int anzahl, int gramm_, bool zu) {
    lv_obj_t* k = box(liste);
    lv_obj_set_size(k, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(k, K(9, 6), 0);
    lv_obj_set_style_pad_hor(k, K(6, 4), 0);
    lv_obj_set_style_radius(k, K(10, 7), 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, farbe(0x191c21), 0);  // color-mix(panel 70 %, linie)
    lv_obj_set_style_bg_color(k, farbe(C_LINIE), LV_STATE_PRESSED);
    lv_obj_set_style_border_side(k, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(k, 1, 0);
    lv_obj_set_style_border_color(k, farbe(C_LINIE), 0);
    lv_obj_set_style_opa(k, zu ? 217 : LV_OPA_COVER, 0);  // .g-kopf.zu { opacity: .85 }
    lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(k, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(k, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(k, K(10, 6), 0);
    ikon(k, zu ? "runter" : "hoch", K(16, 13), C_SUB);
    if (art == "material") {
        lv_obj_t* m = box(k);
        lv_obj_set_size(m, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(m, K(12, 8), 0);
        lv_obj_set_style_pad_ver(m, 2, 0);
        lv_obj_set_style_radius(m, K(8, 6), 0);
        lv_obj_set_style_border_width(m, 1, 0);
        lv_obj_set_style_border_color(m, farbe(C_GRUEN), 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(m, farbe(0x1f2c22), 0);  // gruen 16 % über panel
        lv_obj_t* l = text(m, T13, C_GRUEN_T, name.c_str());
        lv_obj_set_style_text_letter_space(l, 1, 0);
    } else if (art == "hersteller" && name.rfind("Ohne ", 0) != 0) {
        marke(k, name, LOGO_GRUPPE, T14, C_TEXT, LOGO_W[LOGO_GRUPPE]);
    } else {
        lv_obj_t* z = zeile(k, K(8, 5));
        if (art == "farbe") {
            lv_obj_t* d = box(z);
            lv_obj_set_size(d, K(12, 10), K(12, 10));
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d, farbe(FAM_HEX[std::min(12, fam_index(name))]), 0);
            lv_obj_set_style_border_width(d, 1, 0);
            lv_obj_set_style_border_color(d, farbe(0x55595f), 0);
        }
        text(z, KT(&fl_14, &fl_13), C_TEXT, name.c_str());
    }
    lv_obj_t* fl = box(k);
    lv_obj_set_flex_grow(fl, 1);
    lv_obj_set_height(fl, 1);
    text(k, T13, C_SUB, (std::to_string(anzahl) + (anzahl == 1 ? " Sorte" : " Sorten")).c_str());
    lv_obj_t* g = text(k, KT(&fl_15, &fl_13), C_TEXT, kg_text(gramm_).c_str());
    lv_obj_set_style_min_width(g, K(64, 44), 0);
    lv_obj_set_style_text_align(g, LV_TEXT_ALIGN_RIGHT, 0);
    g_gruppen_namen.push_back(name);
    lv_obj_add_event_cb(k, gruppe_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(g_gruppen_namen.size() - 1));
}

void lager_liste_bauen() {
    if (!g_lager) return;
    leiste_bauen();
    lv_obj_clean(l_liste);
    g_gruppen_namen.clear();
    std::string q = normal(lv_textarea_get_text(l_suche));
    while (!q.empty() && q.back() == ' ') q.pop_back();
    int vorschlag = g_auswahl_modus && !g_auswahl_tag.empty() ? g_vorschlag : -1;
    std::vector<int> treffer;
    for (size_t i = 0; i < g_lager->sorten.size(); i++) {
        const Filament& f = g_lager->sorten[i];
        if ((int)i == vorschlag || !passt_filter(f)) continue;
        if (!q.empty()) {
            std::string alles = f.summary + " " + f.hersteller + " " + f.farbe + " " + f.material + " " + f.fcode + " " + f.ort;
            for (auto& b : f.besonderheiten) alles += " " + b;
            if (normal(alles).find(q) == std::string::npos) continue;
        }
        treffer.push_back((int)i);
    }
    // sortieren: nach Name (bzw. Bestand), Zahlen natürlich wie localeCompare(numeric)
    auto name_von = [&](int i) { return normal(g_lager->sorten[i].kurzname() + " " + g_lager->sorten[i].hersteller); };
    if (g_lo.gruppe == "bestand" && q.empty())
        std::stable_sort(treffer.begin(), treffer.end(), [&](int a, int b) {
            const Filament &x = g_lager->sorten[a], &y = g_lager->sorten[b];
            if ((x.spulen == 0) != (y.spulen == 0)) return x.spulen != 0;
            if (x.bestand_g() != y.bestand_g()) return x.bestand_g() < y.bestand_g();
            return name_von(a) < name_von(b);
        });
    else
        std::stable_sort(treffer.begin(), treffer.end(), [&](int a, int b) { return name_von(a) < name_von(b); });

    int n = 0, weitere = 0;
    if (vorschlag >= 0) {
        lager_zeile(l_liste, g_lager->sorten[vorschlag], vorschlag, true);
        n++;
    }
    bool gruppiert = q.empty() && vorschlag < 0 && g_lo.gruppe != "bestand";
    if (gruppiert) {
        std::vector<std::pair<std::string, std::vector<int>>> gruppen;
        for (int i : treffer) {
            std::string k = gruppe_von(g_lager->sorten[i], g_lo.gruppe);
            auto it = std::find_if(gruppen.begin(), gruppen.end(), [&](const std::pair<std::string, std::vector<int>>& g) { return g.first == k; });
            if (it == gruppen.end()) gruppen.push_back({k, {i}});
            else it->second.push_back(i);
        }
        std::sort(gruppen.begin(), gruppen.end(), [&](const std::pair<std::string, std::vector<int>>& a, const std::pair<std::string, std::vector<int>>& b) {
            return gruppe_vor(g_lo.gruppe, a.first, b.first);
        });
        for (auto& g : gruppen) {
            int summe = 0;
            for (int i : g.second) summe += g_lager->sorten[i].bestand_g();
            bool zu = !gruppe_offen(g.first);
            gruppen_kopf(l_liste, g.first, g_lo.gruppe, (int)g.second.size(), summe, zu);
            if (zu) continue;
            for (int i : g.second) {
                if (n >= MAX_ZEILEN) { weitere++; continue; }
                lager_zeile(l_liste, g_lager->sorten[i], i, false);
                n++;
            }
        }
    } else {
        for (int i : treffer) {
            if (n >= MAX_ZEILEN) { weitere++; continue; }
            lager_zeile(l_liste, g_lager->sorten[i], i, false);
            n++;
        }
    }
    setze(l_suche_n, q.empty() ? "" : std::to_string(treffer.size()) + " Treffer");
    if (treffer.empty() && vorschlag < 0) {
        lv_obj_t* l = text(l_liste, T15, C_SUB, g_lager->sorten.empty() ? "Lager wird geladen …" : "Keine passende Sorte");
        lv_obj_set_style_pad_ver(l, K(16, 10), 0);
    }
    if (weitere) {
        lv_obj_t* l = text(l_liste, T13, C_SUB, ("… und " + std::to_string(weitere) + " weitere – Suche verwenden").c_str());
        lv_obj_set_style_pad_ver(l, K(16, 10), 0);
    }
}

void reste_bauen() {
    lv_obj_clean(l_reste);
    lv_obj_t* t = text(l_reste, T12, C_ORANGE_T, "ANGEBROCHENE ROLLEN");
    ueberschrift(t);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, lv_pct(100));
    if (!g_lager) return;
    struct R { int sorte; int g; };
    std::vector<R> alle;
    for (size_t i = 0; i < g_lager->sorten.size(); i++)
        for (int g : g_lager->sorten[i].reste) alle.push_back({(int)i, g});
    std::stable_sort(alle.begin(), alle.end(), [](const R& a, const R& b) { return a.g > b.g; });
    if (alle.empty()) text(l_reste, T13, C_SUB, "Keine angebrochenen Rollen");
    for (size_t k = 0; k < alle.size() && k < 7; k++) {
        const Filament& f = g_lager->sorten[alle[k].sorte];
        lv_obj_t* z = zeile(l_reste, K(10, 6));
        lv_obj_set_width(z, lv_pct(100));
        lv_obj_set_style_pad_ver(z, K(4, 2), 0);
        rolle_platz(z, R_PLATZ, R_BILD, f.rgb, f.besonderheiten);
        lv_obj_t* m = spalte(z, K(4, 3));
        lv_obj_set_flex_grow(m, 1);
        lv_obj_set_width(m, 0);
        lv_obj_t* n = text(m, KT(&fl_13, &fl_12), C_TEXT, f.kurzname().c_str());
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_size(n, lv_pct(100), lv_font_get_line_height(KT(&fl_13, &fl_12)));
        // auf dem 3,5"-Display Balken und Gewicht in der zweiten Zeile, damit der Name Platz hat
        lv_obj_t* bz = UI_KOMPAKT ? zeile(m, 6) : m;
        if (UI_KOMPAKT) lv_obj_set_width(bz, lv_pct(100));
        lv_obj_t* bar = lv_bar_create(bz);
        lv_obj_set_size(bar, lv_pct(100), 4);
        if (UI_KOMPAKT) {
            lv_obj_set_flex_grow(bar, 1);
            lv_obj_set_width(bar, 0);
        }
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, std::max(2, alle[k].g * 100 / std::max(1, f.gewicht)), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, farbe(C_RAND2), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, farbe(alle[k].g < 250 ? C_ROT : C_ORANGE), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
        lv_obj_t* g = text(UI_KOMPAKT ? bz : z, KT(&fl_13, &fl_12), C_TEXT, gramm(alle[k].g).c_str());
        lv_obj_set_style_min_width(g, K(50, 34), 0);
        lv_obj_set_style_text_align(g, LV_TEXT_ALIGN_RIGHT, 0);
    }
}

// ---------------------------------------------------------------- Detail
void spulen_cb(lv_event_t* e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_detail >= 0 && g_akt) g_akt->spulen_aendern(g_lager->sorten[g_detail].uid, delta);
}

void wiegen_cb(lv_event_t*) {
    if (g_detail >= 0) g_auswahl_uid = g_lager->sorten[g_detail].uid;
    g_karte_schluessel.clear();
    zeige_seite(WIEGEN);
}

void zurueck_cb(lv_event_t*) { zeige_seite(LAGER); }

void etikett_detail_cb(lv_event_t*) {
    if (g_detail >= 0) et_oeffnen(g_detail, 0);
}

lv_obj_t* kachel(lv_obj_t* p, const std::string& wert, const char* name, const lv_font_t* f, bool klein_ = false) {
    lv_obj_t* k = box(p);
    lv_obj_set_style_radius(k, klein_ ? S(12) : S(16), 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, farbe(klein_ ? C_KACHEL : C_PANEL), 0);
    if (!klein_) {
        lv_obj_set_style_border_color(k, farbe(C_RAND2), 0);
        lv_obj_set_style_border_width(k, 2, 0);
    }
    lv_obj_set_style_pad_hor(k, klein_ ? S(12) : S(14), 0);
    lv_obj_set_style_pad_ver(k, klein_ ? S(10) : S(12), 0);
    lv_obj_set_flex_flow(k, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(k, S(4), 0);
    lv_obj_set_flex_grow(k, 1);
    lv_obj_set_height(k, LV_SIZE_CONTENT);
    if (klein_) {
        text(k, f, C_TEXT, wert.c_str());
        text(k, F_K, C_SUB, name);
    } else {
        lv_obj_t* t = text(k, F_K, C_SUB, name);
        ueberschrift(t);
        text(k, f, C_TEXT, wert.c_str());
    }
    return k;
}

void info_zeile(lv_obj_t* p, const char* name, const std::string& wert, bool letzte) {
    lv_obj_t* z = box(p);
    lv_obj_set_size(z, lv_pct(100), S(40));
    if (!letzte) {
        lv_obj_set_style_border_side(z, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(z, 1, 0);
        lv_obj_set_style_border_color(z, farbe(C_LINIE), 0);
    }
    lv_obj_t* a = text(z, F_K, C_SUB, name);
    lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* b = text(z, F_N, C_TEXT, wert.empty() ? "–" : wert.c_str());
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
}

std::string bereich(int a, int b, const char* einheit) {
    if (a < 0 && b < 0) return "–";
    if (a < 0 || a == b) return std::to_string(b) + einheit;
    if (b < 0) return std::to_string(a) + einheit;
    return std::to_string(a) + "–" + std::to_string(b) + einheit;
}

void detail_bauen() {
    lv_obj_clean(d_inhalt);
    if (!g_lager || g_detail < 0 || g_detail >= (int)g_lager->sorten.size()) return;
    const Filament& f = g_lager->sorten[g_detail];
    lv_obj_set_flex_flow(d_inhalt, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(d_inhalt, S(14), 0);

    lv_obj_t* l = box(d_inhalt);
    panel_stil(l, C_GRUEN);
    lv_obj_set_size(l, K(300, 222), lv_pct(100));
    lv_obj_set_style_pad_all(l, S(16), 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, S(12), 0);

    lv_obj_t* kopf = zeile(l, S(10));
    knopf(kopf, LV_SYMBOL_LEFT, Art::RAHMEN, zurueck_cb);
    lv_obj_set_size(lv_obj_get_child(kopf, 0), S(44), S(44));
    lv_obj_t* tn = spalte(kopf, 0);
    lv_obj_t* name = text(tn, F_G, C_TEXT, f.kurzname().c_str());
    einzeilig(name, S(210));
    if (!f.hersteller.empty()) marke(tn, f.hersteller, LOGO_GROSS, F_K, C_SUB, K(210, 150));

    lv_obj_t* o = zeile(l, S(14));
    farbfeld(o, S(64), S(14), f.rgb);
    lv_obj_t* oc = spalte(o, S(6));
    lv_obj_t* chips = zeile(oc, S(6));
    if (!f.material.empty()) chip(chips, f.material, C_CHIP, C_TEXT);
    for (size_t i = 0; i < f.besonderheiten.size() && i < 2; i++) chip(chips, f.besonderheiten[i], C_CHIP_B, C_BLAU_T);
    text(oc, F_K, C_SUB, (komma(f.durchmesser, 2) + " mm · " + gramm(f.gewicht)).c_str());

    lv_obj_t* kz = zeile(l, S(10));
    lv_obj_set_width(kz, lv_pct(100));
    kachel(kz, std::to_string(f.spulen), "Spulen", F_T, true);
    kachel(kz, std::to_string(f.reste.size()), "angebrochen", F_T, true);

    lv_obj_t* kn = box(l);
    lv_obj_set_size(kn, lv_pct(100), S(52));
    lv_obj_set_flex_flow(kn, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(kn, S(8), 0);
    lv_obj_t* minus = knopf(kn, LV_SYMBOL_MINUS, Art::RAHMEN, spulen_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(minus, K(52, 34), S(52));
    lv_obj_t* w = knopf(kn, "Wiegen", Art::PRIMAER, wiegen_cb);
    lv_obj_t* et = knopf(kn, "Etikett", Art::RAHMEN, etikett_detail_cb);
    for (lv_obj_t* b : {w, et}) {
        lv_obj_set_height(b, S(52));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 2, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), KT(F_N, F_K), 0);
    }
    lv_obj_t* plus = knopf(kn, LV_SYMBOL_PLUS, Art::RAHMEN, spulen_cb, (void*)(intptr_t)1);
    lv_obj_set_size(plus, K(52, 34), S(52));

    lv_obj_t* r = box(d_inhalt);
    lv_obj_set_height(r, lv_pct(100));
    lv_obj_set_flex_grow(r, 1);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(r, S(10), 0);
    lv_obj_t* t = zeile(r, S(10));
    lv_obj_set_width(t, lv_pct(100));
    kachel(t, bereich(f.duese_min, f.duese_max, " °C"), "DÜSE", KT(F_T, F_G));
    std::string trocknen = f.trocknen_c > 0 ? std::to_string(f.trocknen_c) + " °C" : "–";
    if (f.trocknen_c > 0 && f.trocknen_h > 0) trocknen += " · " + std::to_string(f.trocknen_h) + " h";
    kachel(t, trocknen, "TROCKNEN", KT(F_T, F_G));

    lv_obj_t* liste = box(r);
    lv_obj_set_width(liste, lv_pct(100));
    lv_obj_set_flex_grow(liste, 1);
    lv_obj_set_style_bg_color(liste, farbe(C_PANEL), 0);
    lv_obj_set_style_bg_opa(liste, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(liste, farbe(C_RAND2), 0);
    lv_obj_set_style_border_width(liste, 2, 0);
    lv_obj_set_style_radius(liste, S(16), 0);
    lv_obj_set_style_pad_hor(liste, S(14), 0);
    lv_obj_set_style_pad_ver(liste, S(4), 0);
    lv_obj_set_flex_flow(liste, LV_FLEX_FLOW_COLUMN);
    std::string reste;
    for (int g : f.reste) reste += (reste.empty() ? "" : ", ") + gramm(g);
    info_zeile(liste, "Reste", reste, false);
    info_zeile(liste, "Bambu-ID", f.bambu_id, false);
    info_zeile(liste, "Farbcode", f.fcode, false);
    info_zeile(liste, "EAN", f.codes.empty() ? "" : f.codes[0], false);
    info_zeile(liste, "NFC-Tags", f.nfc.empty() ? "" : std::to_string(f.nfc.size()) + " zugeordnet", true);
}

// ---------------------------------------------------------------- Einstellungen
void kal_cb(lv_event_t* e) {
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    g_kal_g = std::max(100, std::min(5000, g_kal_g + d));
    setze(e_kal_wert, gramm(g_kal_g));
}

void kalibrieren_cb(lv_event_t*) {
    if (g_akt) g_akt->kalibrieren(g_kal_g);
}

void laden_cb(lv_event_t*) {
    if (g_akt) g_akt->neu_laden();
}

void drucker_bauen();
void einst_tab_markieren();

void einst_tab_cb(lv_event_t* e) {
    g_einst_tab = (int)(intptr_t)lv_event_get_user_data(e);
    einst_tab_markieren();
    if (g_einst_tab == 1) {
        drucker_bauen();
        if (g_akt) g_akt->drucker_laden();
    }
}

void baue_einst(lv_obj_t* seite_) {
    lv_obj_set_flex_flow(seite_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(seite_, S(10), 0);
    lv_obj_set_style_pad_top(seite_, S(10), 0);
    lv_obj_t* tabs = box(seite_);
    lv_obj_set_size(tabs, lv_pct(100), K(40, 28));
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, S(8), 0);
    const char* namen[2] = {"Waage", "Drucker & AMS"};
    for (int i = 0; i < 2; i++) {
        e_tab[i] = knopf(tabs, namen[i], Art::FLACH, einst_tab_cb, (void*)(intptr_t)i);
        lv_obj_set_size(e_tab[i], K(200, 130), lv_pct(100));
        lv_obj_set_style_text_font(lv_obj_get_child(e_tab[i], 0), F_K, 0);
    }
    e_drucker = box(seite_);
    lv_obj_set_width(e_drucker, lv_pct(100));
    lv_obj_set_flex_grow(e_drucker, 1);
    lv_obj_set_flex_flow(e_drucker, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(e_drucker, S(14), 0);
    e_waage = box(seite_);
    lv_obj_set_width(e_waage, lv_pct(100));
    lv_obj_set_flex_grow(e_waage, 1);
    lv_obj_t* s = e_waage;
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s, S(14), 0);

    lv_obj_t* a = box(s);
    panel_stil(a, C_GRUEN);
    lv_obj_set_height(a, lv_pct(100));
    lv_obj_set_flex_grow(a, 1);
    lv_obj_set_style_pad_all(a, K(18, 10), 0);
    lv_obj_set_flex_flow(a, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(a, K(12, 6), 0);
    lv_obj_t* t = text(a, F_K, C_SUB, "WAAGE");
    ueberschrift(t);
#if UI_KOMPAKT
    // kein Platz für die Erklärung – die Knöpfe sprechen für sich
#else
    text(a, F_K, C_SUB, "Teller leer machen und Tara drücken. Dann das\nKalibriergewicht auflegen und Kalibrieren drücken.");
#endif
    lv_obj_t* tara = knopf(a, "Tara", Art::RAHMEN, tara_cb);
    lv_obj_set_size(tara, lv_pct(100), K(50, 30));
    lv_obj_t* z = box(a);
    lv_obj_set_size(z, lv_pct(100), K(52, 34));
    lv_obj_set_flex_flow(z, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(z, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(z, S(8), 0);
    lv_obj_t* m = knopf(z, LV_SYMBOL_MINUS, Art::RAHMEN, kal_cb, (void*)(intptr_t)-100);
    lv_obj_set_size(m, K(58, 40), K(52, 34));
    e_kal_wert = text(z, F_T, C_TEXT, "1000 g");
    lv_obj_set_flex_grow(e_kal_wert, 1);
    lv_obj_set_style_text_align(e_kal_wert, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* p = knopf(z, LV_SYMBOL_PLUS, Art::RAHMEN, kal_cb, (void*)(intptr_t)100);
    lv_obj_set_size(p, K(58, 40), K(52, 34));
    lv_obj_t* k = knopf(a, "Kalibrieren", Art::PRIMAER, kalibrieren_cb);
    lv_obj_set_size(k, lv_pct(100), K(54, 34));

    lv_obj_t* b = box(s);
    panel_stil(b, C_RAND2, S(18));
    lv_obj_set_size(b, K(300, 212), lv_pct(100));
    lv_obj_set_style_pad_all(b, S(18), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, S(12), 0);
    lv_obj_t* t2 = text(b, F_K, C_SUB, "VERBINDUNG");
    ueberschrift(t2);
    e_info = text(b, KT(F_N, F_K), C_TEXT, "");
    lv_obj_set_style_text_line_space(e_info, S(6), 0);
    lv_obj_t* lade = knopf(b, LV_SYMBOL_REFRESH "  Lager neu laden", Art::RAHMEN, laden_cb);
    lv_obj_set_size(lade, lv_pct(100), S(50));
}

void einst_aktualisieren() {
    std::string s = std::string("WLAN: ") + (g_st.wlan_ok ? g_st.ip : "getrennt") + "\n";
    s += std::string("Home Assistant: ") + (g_st.ha_ok ? "verbunden" : "keine Verbindung") + "\n";
    s += std::string("Sensor: ") + (g_st.sensor_ok ? "verbunden" : "antwortet nicht") + "\n";
    s += "Sorten im Lager: " + std::to_string(g_lager ? g_lager->sorten.size() : 0) + "\n";
    s += "Leerspule Standard: " + gramm(g_st.leer_standard_g) + "\n";
    s += "Version: " + g_st.version;
    setze(e_info, s);
}

// ---------------------------------------------------------------- Drucker & AMS
void einst_tab_markieren() {
    for (int i = 0; i < 2; i++) {
        bool an = i == g_einst_tab;
        lv_obj_set_style_bg_color(e_tab[i], farbe(an ? C_NAV_AKT : C_PANEL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(e_tab[i], 0), farbe(an ? C_GRUEN_T : C_NAV), 0);
    }
    if (g_einst_tab == 0) {
        lv_obj_clear_flag(e_waage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(e_drucker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(e_waage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(e_drucker, LV_OBJ_FLAG_HIDDEN);
    }
}

void drucker_neu_cb(lv_event_t*) {
    if (g_akt) g_akt->drucker_laden();
    ui_meldung("Frage Drucker ab …");
}

// Ein AMS-Fach: Farbkreis, Name, Füllstand
void ams_fach(lv_obj_t* p, const AmsEinheit& a, const AmsSlot& sl, int b) {
    lv_obj_t* f = spalte(p, 2);
    lv_obj_set_width(f, b);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* kreis = box(f);
    int d = K(30, 20);
    lv_obj_set_size(kreis, d, d);
    lv_obj_set_style_radius(kreis, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(kreis, sl.aktiv ? 3 : 1, 0);
    lv_obj_set_style_border_color(kreis, farbe(sl.aktiv ? C_GRUEN : 0x4a5058), 0);
    if (!sl.leer) {
        lv_obj_set_style_bg_color(kreis, farbe(sl.rgb), 0);
        lv_obj_set_style_bg_opa(kreis, LV_OPA_COVER, 0);
    }
    std::string nr = ams_slot_kurz(a, sl);
    lv_obj_t* l = text(kreis, F_K, (sl.rgb & 0xFF) + ((sl.rgb >> 8) & 0xFF) + ((sl.rgb >> 16) & 0xFF) > 450 && !sl.leer ? C_BG : C_TEXT,
                       sl.leer ? "–" : "");
    lv_obj_center(l);
    lv_obj_t* n = text(f, F_K, sl.leer ? C_SUB : C_TEXT, sl.leer ? "leer" : (sl.name.empty() ? nr.c_str() : sl.name.c_str()));
    einzeilig(n, b);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    std::string r = sl.leer ? nr : nr + (sl.rest >= 0 ? " · " + std::to_string(sl.rest) + " %" : "");
    lv_obj_t* rl = text(f, F_K, C_SUB, r.c_str());
    einzeilig(rl, b);
    lv_obj_set_style_text_align(rl, LV_TEXT_ALIGN_CENTER, 0);
}

void drucker_bauen() {
    lv_obj_clean(e_drucker);
    // links: AMS-Belegung je Drucker
    lv_obj_t* a = box(e_drucker);
    panel_stil(a, C_GRUEN);
    lv_obj_set_height(a, lv_pct(100));
    lv_obj_set_flex_grow(a, 1);
    lv_obj_set_style_pad_all(a, K(16, 8), 0);
    lv_obj_set_flex_flow(a, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(a, K(10, 5), 0);
    lv_obj_add_flag(a, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(a, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(a, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_t* t = text(a, F_K, C_SUB, "AMS-BELEGUNG");
    ueberschrift(t);
    const int innen = W - 2 * S(16) - K(300, 196) - S(14) - 2 * K(16, 8) - 4;
    if (!g_ams || !g_ams->geladen) text(a, F_K, C_SUB, "Wird aus Home Assistant geladen …");
    else if (g_ams->drucker.empty()) text(a, F_K, C_SUB, "Keine Bambu-Lab-Drucker in Home Assistant gefunden");
    std::vector<const AmsDrucker*> liste;
    if (g_ams)
        for (const auto& d : g_ams->drucker) liste.push_back(&d);
    std::stable_sort(liste.begin(), liste.end(), [](const AmsDrucker* x, const AmsDrucker* y) { return x->online && !y->online; });
    {
        for (const AmsDrucker* dp : liste) {
            const AmsDrucker& d = *dp;
            lv_obj_t* k = zeile(a, S(8));
            lv_obj_t* p = box(k);
            lv_obj_set_size(p, 8, 8);
            lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(p, farbe(d.online ? C_GRUEN : C_ROT), 0);
            text(k, F_N, C_TEXT, d.name.c_str());
            if (!d.online) text(k, F_K, C_SUB, "offline");
            for (const auto& e : d.ams) {
                std::string kopf = e.name + (e.feuchte >= 0 ? " · " + std::to_string(e.feuchte) + (e.feuchte <= 5 ? "" : " %") : "");
                text(a, F_K, C_SUB, kopf.c_str());
                lv_obj_t* r = zeile(a, S(6));
                int n = std::max<int>(4, (int)e.slots.size());
                int b = (innen - (n - 1) * S(6)) / n;
                for (const auto& sl : e.slots) ams_fach(r, e, sl, b);
            }
        }
    }
    // rechts: Etikettendrucker in Home Assistant
    lv_obj_t* b = box(e_drucker);
    panel_stil(b, C_RAND2, 18);
    lv_obj_set_size(b, K(300, 196), lv_pct(100));
    lv_obj_set_style_pad_all(b, K(16, 8), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, K(10, 6), 0);
    lv_obj_t* t2 = text(b, F_K, C_SUB, "ETIKETTENDRUCKER");
    ueberschrift(t2);
    if (!g_et.fehler.empty()) einzeilig(text(b, F_K, C_ROT, g_et.fehler.c_str()), K(268, 178));
    else if (!g_et.geladen) text(b, F_K, C_SUB, "Wird abgefragt …");
    else if (g_et.drucker.empty()) text(b, F_K, C_SUB, "Keine Drucker eingerichtet");
    for (const auto& d : g_et.drucker) {
        lv_obj_t* z = zeile(b, S(8));
        lv_obj_t* p = box(z);
        lv_obj_set_size(p, 8, 8);
        lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(p, farbe(d.erreichbar ? C_GRUEN : C_ROT), 0);
        lv_obj_t* n = text(z, F_N, C_TEXT, d.name.c_str());
        einzeilig(n, K(250, 160));
    }
    lv_obj_t* fl = box(b);
    lv_obj_set_flex_grow(fl, 1);
    lv_obj_t* k = knopf(b, LV_SYMBOL_REFRESH "  Neu abfragen", Art::RAHMEN, drucker_neu_cb);
    lv_obj_set_size(k, lv_pct(100), K(50, 34));
    if (UI_KOMPAKT) lv_obj_set_style_text_font(lv_obj_get_child(k, 0), F_K, 0);
}

// ---------------------------------------------------------------- Etikett drucken
void et_bauen();

void et_schliessen() {
    if (g_et_dlg) lv_obj_del(g_et_dlg);
    g_et_dlg = nullptr;
    g_et_sorte = -1;
}

void et_zu_cb(lv_event_t*) { et_schliessen(); }

void et_drucker_cb(lv_event_t* e) {
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (i < g_et.drucker.size()) g_et_drucker = g_et.drucker[i].id;
    et_bauen();
}

void et_format_cb(lv_event_t* e) {
    g_et.breit = (intptr_t)lv_event_get_user_data(e) != 0;
    et_bauen();
}

void et_rest_cb(lv_event_t*) {
    g_et_mit_rest = !g_et_mit_rest;
    et_bauen();
}

void et_druck_cb(lv_event_t*) {
    if (g_et_sorte < 0 || !g_akt || g_et_drucker.empty()) return;
    g_akt->etikett_drucken(etikett_titel(g_lager->sorten[g_et_sorte]), g_et_bild, g_et_drucker);
    et_schliessen();
    ui_meldung("Etikett geht an den Drucker …");
}

// Auswahlknopf (gewählt = grün umrandet)
lv_obj_t* wahl(lv_obj_t* p, const std::string& t, bool an, lv_event_cb_t cb, void* daten) {
    lv_obj_t* b = knopf(p, t.c_str(), Art::RAHMEN, cb, daten);
    lv_obj_set_height(b, K(44, 30));
    lv_obj_set_style_border_color(b, farbe(an ? C_GRUEN : C_RAND2), 0);
    lv_obj_set_style_bg_color(b, farbe(an ? C_NAV_AKT : C_BG), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), F_K, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), farbe(an ? C_GRUEN_T : C_TEXT), 0);
    return b;
}

void et_bauen() {
    if (g_et_sorte < 0 || !g_lager || g_et_sorte >= (int)g_lager->sorten.size()) return;
    const Filament& f = g_lager->sorten[g_et_sorte];
    // Drucker vorwählen: zuletzt gewählter, sonst der erste erreichbare
    bool gibt = false;
    for (auto& d : g_et.drucker) gibt |= d.id == g_et_drucker;
    if (!gibt) {
        g_et_drucker.clear();
        for (auto& d : g_et.drucker)
            if (d.erreichbar) { g_et_drucker = d.id; break; }
        if (g_et_drucker.empty() && !g_et.drucker.empty()) g_et_drucker = g_et.drucker[0].id;
    }
    // Etikett zeichnen
    EtikettOpt o;
    o.breit = g_et.breit;
    o.rest_g = g_et_mit_rest ? g_et_rest : 0;
    o.qr = etikett_qr(f, g_et.qr_basis, g_et.lager_url);
    const Logo* l = logo_von(f.hersteller);
    if (l) o.logo_png = &l->png;
    etikett_zeichnen(f, o, g_et_bild);

    if (g_et_dlg) lv_obj_del(g_et_dlg);
    g_et_dlg = box(lv_layer_top());
    lv_obj_set_size(g_et_dlg, W, H);
    lv_obj_add_flag(g_et_dlg, LV_OBJ_FLAG_CLICKABLE);  // fängt Tipps neben dem Dialog ab
    lv_obj_set_style_bg_color(g_et_dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_et_dlg, LV_OPA_60, 0);
    lv_obj_t* p = box(g_et_dlg);
    panel_stil(p, C_GRUEN);
    lv_obj_set_size(p, K(720, 468), K(420, 304));
    lv_obj_center(p);
    lv_obj_set_style_pad_all(p, K(18, 8), 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, K(12, 6), 0);
    lv_obj_t* kopf = zeile(p, S(10));
    text(kopf, F_G, C_TEXT, "Etikett drucken");
    einzeilig(text(kopf, F_K, C_SUB, f.kurzname().c_str()), K(420, 260));

    lv_obj_t* mitte = box(p);
    lv_obj_set_width(mitte, lv_pct(100));
    lv_obj_set_flex_grow(mitte, 1);
    lv_obj_set_flex_flow(mitte, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(mitte, K(16, 8), 0);
    // Vorschau auf weißem Grund (fertig verkleinert)
    const int vh = K(240, 166);
    etikett_vorschau(g_et_bild, vh, g_et_vorschau);
    lv_obj_t* papier = box(mitte);
    lv_obj_set_size(papier, g_et_vorschau.bild.header.w + 8, g_et_vorschau.bild.header.h + 8);
    lv_obj_set_style_bg_color(papier, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(papier, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(papier, 6, 0);
    lv_obj_t* img = lv_img_create(papier);
    lv_img_set_src(img, &g_et_vorschau.bild);
    lv_obj_set_pos(img, 4, 4);

    lv_obj_t* r = box(mitte);
    lv_obj_set_height(r, lv_pct(100));
    lv_obj_set_flex_grow(r, 1);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(r, K(8, 4), 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(r, LV_DIR_VER);
    text(r, F_K, C_SUB, "DRUCKER");
    if (g_et.drucker.empty())
        einzeilig(text(r, F_K, C_SUB, !g_et.fehler.empty() ? g_et.fehler.c_str() : g_et.geladen ? "Keine Drucker in Home Assistant" : "Drucker werden abgefragt …"), K(300, 180));
    for (size_t i = 0; i < g_et.drucker.size(); i++) {
        const auto& d = g_et.drucker[i];
        lv_obj_t* b = wahl(r, d.name + (d.erreichbar ? "" : " (aus)"), d.id == g_et_drucker, et_drucker_cb, (void*)(intptr_t)i);
        lv_obj_set_width(b, lv_pct(100));
    }
    text(r, F_K, C_SUB, "FORMAT");
    lv_obj_t* fz = box(r);
    lv_obj_set_size(fz, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(fz, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(fz, S(6), 0);
    for (int i = 0; i < 2; i++) lv_obj_set_flex_grow(wahl(fz, i ? "50×30" : "40×30", g_et.breit == (i == 1), et_format_cb, (void*)(intptr_t)i), 1);
    if (g_et_rest > 0) {
        lv_obj_t* b = wahl(r, (g_et_mit_rest ? LV_SYMBOL_OK " Rest " : "Rest ") + gramm(g_et_rest), g_et_mit_rest, et_rest_cb, nullptr);
        lv_obj_set_width(b, lv_pct(100));
    }

    lv_obj_t* unten = box(p);
    lv_obj_set_size(unten, lv_pct(100), K(52, 36));
    lv_obj_set_flex_flow(unten, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(unten, S(10), 0);
    lv_obj_t* zu = knopf(unten, "Abbrechen", Art::RAHMEN, et_zu_cb);
    lv_obj_set_height(zu, lv_pct(100));
    lv_obj_set_flex_grow(zu, 1);
    lv_obj_t* dr = knopf(unten, LV_SYMBOL_OK "  Drucken", Art::PRIMAER, et_druck_cb);
    lv_obj_set_height(dr, lv_pct(100));
    lv_obj_set_flex_grow(dr, 2);
    knopf_aktiv(dr, !g_et_drucker.empty());
    if (UI_KOMPAKT) {
        lv_obj_set_style_text_font(lv_obj_get_child(zu, 0), F_K, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(dr, 0), F_N, 0);
    }
}

void et_oeffnen(int sorte, int rest) {
    g_et_sorte = sorte;
    g_et_rest = rest;
    g_et_mit_rest = rest > 0;
    lv_obj_add_flag(l_tastatur, LV_OBJ_FLAG_HIDDEN);
    et_bauen();
    if (g_akt && !g_et.geladen) g_akt->drucker_laden();
}

// ---------------------------------------------------------------- Seiten
void zeige_seite(Seite s) {
    g_seite = s;
    dd_schliessen();
    for (int i = 0; i < 4; i++) {
        if (i == s) lv_obj_clear_flag(g_seiten[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_seiten[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(l_tastatur, LV_OBJ_FLAG_HIDDEN);
    nav_markieren(s == WIEGEN ? 0 : (s == LAGER || s == DETAIL) ? 1 : 2);
    if (s == WIEGEN) {
        setze(h_sub, "Waage");
        g_karte_schluessel.clear();
        wiegen_aktualisieren();
    } else if (s == LAGER) {
        setze(h_sub, g_auswahl_modus ? "Sorte für die Spule wählen"
                                     : std::to_string(g_lager ? g_lager->sorten.size() : 0) + " Sorten");
        lager_liste_bauen();
        reste_bauen();
    } else if (s == DETAIL) {
        setze(h_sub, "Sorte im Detail");
        detail_bauen();
    } else {
        setze(h_sub, "Einstellungen");
        einst_tab_markieren();
        einst_aktualisieren();
        if (g_einst_tab == 1) drucker_bauen();
    }
}

void toast_aus(lv_timer_t*) { lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN); }

}  // namespace

// ---------------------------------------------------------------- öffentlich
void ui_start(UiAktionen* aktionen, const Lager* lager) {
    g_akt = aktionen;
    g_lager = lager;
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, farbe(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, farbe(C_TEXT), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    baue_kopf(scr);
    for (int i = 0; i < 4; i++) g_seiten[i] = seite(scr);
    baue_wiegen(g_seiten[WIEGEN]);
    baue_lager(g_seiten[LAGER]);
    d_inhalt = g_seiten[DETAIL];
    baue_einst(g_seiten[EINST]);
    baue_nav(scr);

    g_toast = box(lv_layer_top());  // auch über dem Druckdialog
    lv_obj_set_size(g_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(g_toast, farbe(C_PANEL), 0);
    lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_toast, 2, 0);
    lv_obj_set_style_radius(g_toast, S(14), 0);
    lv_obj_set_style_pad_hor(g_toast, S(18), 0);
    lv_obj_set_style_pad_ver(g_toast, S(12), 0);
    lv_obj_set_style_shadow_width(g_toast, S(24), 0);
    lv_obj_set_style_shadow_color(g_toast, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(g_toast, LV_OPA_60, 0);
    g_toast_lbl = text(g_toast, F_N, C_TEXT, "");
    lv_obj_align(g_toast, LV_ALIGN_BOTTOM_MID, 0, -NAV_H - S(8));
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
    g_toast_timer = lv_timer_create(toast_aus, 2800, nullptr);
    lv_timer_pause(g_toast_timer);

    zeige_seite(WIEGEN);
}

void ui_status(const WaagenStatus& s) {
    g_st = s;
    uint32_t sensor_aus = s.sensor_ok ? C_RAND : C_ROT;
    lv_obj_set_style_bg_color(h_nfc_l, farbe(!s.tag_l.empty() ? C_GRUEN : sensor_aus), 0);
    lv_obj_set_style_bg_color(h_nfc_r, farbe(!s.tag_r.empty() ? C_GRUEN : sensor_aus), 0);
    // Waage: grün = misst und ist kalibriert, rot = Sensor antwortet nicht, grau = nicht kalibriert
    lv_obj_set_style_bg_color(h_waage, farbe(!s.sensor_ok ? C_ROT : s.kalibriert ? C_GRUEN : C_RAND), 0);
    // Drucker: nur zeigen, wenn Etikettendrucker bekannt sind
    if (s.drucker < 0) lv_obj_add_flag(h_drucker_z, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_clear_flag(h_drucker_z, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(h_drucker, farbe(s.drucker == 2 ? C_ROT : s.drucker == 1 ? C_GRUEN : C_RAND), 0);
    }
    lv_obj_set_style_text_color(h_wlan_t, farbe(s.wlan_ok ? C_SUB : C_ROT), 0);
    ikon_farbe(h_wlan_i, s.wlan_ok ? C_GRUEN : C_ROT);
    lv_obj_set_style_bg_color(h_ha, farbe(s.ha_ok ? C_GRUEN : C_ROT), 0);
    setze(h_zeit, s.uhrzeit.empty() ? "--:--" : s.uhrzeit);
    if (g_seite == WIEGEN) wiegen_aktualisieren();
    else if (g_seite == EINST) einst_aktualisieren();
}

void ui_lager_neu() {
    g_karte_schluessel.clear();
    if (g_seite == LAGER) {
        // Liste nur neu aufbauen, wenn die Tastatur nicht gerade benutzt wird
        lager_liste_bauen();
        reste_bauen();
        setze(h_sub, g_auswahl_modus ? "Sorte für die Spule wählen" : std::to_string(g_lager->sorten.size()) + " Sorten");
    } else if (g_seite == DETAIL) {
        detail_bauen();
    } else if (g_seite == WIEGEN) {
        wiegen_aktualisieren();
    }
}

void ui_ams(const AmsStand* ams) {
    g_ams = ams;
    g_ams_stand++;
    if (g_seite == WIEGEN) wiegen_aktualisieren();
    else if (g_seite == EINST && g_einst_tab == 1) drucker_bauen();
}

void ui_etikett(const EtikettEinst& e) {
    bool breit = g_et.breit;
    g_et = e;
    g_et.breit = breit;  // Wahl im Dialog bleibt
    if (g_et_dlg) et_bauen();
    if (g_seite == EINST && g_einst_tab == 1) drucker_bauen();
}

void ui_logos_neu() {
    g_logo_stand++;
    if (g_seite == LAGER) {
        lv_coord_t y = lv_obj_get_scroll_y(l_liste);
        lager_liste_bauen();
        lv_obj_scroll_to_y(l_liste, y, LV_ANIM_OFF);
    } else if (g_seite == DETAIL) {
        detail_bauen();
    } else if (g_seite == WIEGEN) {
        wiegen_aktualisieren();
    }
}

void ui_meldung(const std::string& t, bool fehler) {
    lv_label_set_text(g_toast_lbl, t.c_str());
    lv_obj_set_style_border_color(g_toast, farbe(fehler ? C_ROT : C_GRUEN), 0);
    lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_toast);
    lv_timer_reset(g_toast_timer);
    lv_timer_resume(g_toast_timer);
}

void ui_zeige(const std::string& name) {
    if (name == "lager") zeige_seite(LAGER);
    else if (name == "auswahl") { g_auswahl_modus = true; zeige_seite(LAGER); }
    else if (name == "einst") { g_einst_tab = 0; zeige_seite(EINST); }
    else if (name == "drucker") { g_einst_tab = 1; zeige_seite(EINST); }
    else if (name == "lager_offen" || name == "lager_filter") {
        g_lo.auf.clear();
        if (name == "lager_offen") g_lo.auf.push_back("Bambu Lab");
        g_lo.filter_auf = name == "lager_filter";
        if (name == "lager_filter") g_lo.fl_material = "PLA";
        zeige_seite(LAGER);
    }
    else if (name.rfind("etikett:", 0) == 0) et_oeffnen(atoi(name.c_str() + 8), 562);
    else if (name.rfind("detail:", 0) == 0) { g_detail = atoi(name.c_str() + 7); zeige_seite(DETAIL); }
    else zeige_seite(WIEGEN);
}
