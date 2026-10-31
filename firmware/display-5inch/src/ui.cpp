// Oberfläche der FilamentLager-Waage, LVGL 8.3.
// 800×480 (Waveshare 5") oder mit UI_KOMPAKT=1 480×320 (WT32-SC01 Plus, 3,5").
// Aufbau: Kopfzeile, Inhalt, Navigation unten.
// Seiten: Wiegen, Lager, Detail einer Sorte, Einstellungen.

#include "ui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fonts/fonts.h"
#include "lvgl.h"

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
#define F_Z (&fl_96)
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
lv_obj_t* l_filter[5];
int g_filter = 0;
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

const char* FILTER[5] = {"Alle", "PLA", "PETG", "ABS·ASA", "Andere"};

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
    text(c, F_K, fg, t.c_str());
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
lv_obj_t* marke(lv_obj_t* p, const std::string& hersteller, bool gross, const lv_font_t* f, uint32_t farbe_, int max_b,
                int* breite_aus = nullptr) {
    const Logo* l = logo_von(hersteller);
    const lv_img_dsc_t* b = l ? &l->bild[gross ? 1 : 0] : nullptr;
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
    lv_obj_set_style_bg_color(b, farbe(C_LINIE), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, farbe(0x6b727b), LV_STATE_DISABLED);
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
    lv_obj_t* l = lv_obj_get_child(b, 0);
    if (l) lv_obj_set_style_text_opa(l, an ? LV_OPA_COVER : LV_OPA_40, 0);
}

std::string klein(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
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

bool passt_filter(const Filament& f, int filter) {
    std::string m = klein(f.material);
    bool pla = m.rfind("pla", 0) == 0 || m.rfind("apla", 0) == 0;
    bool petg = m.find("pet") != std::string::npos && !pla;
    bool abs = m.rfind("abs", 0) == 0 || m.rfind("asa", 0) == 0 || m.rfind("pcabs", 0) == 0;
    switch (filter) {
        case 1: return pla;
        case 2: return petg;
        case 3: return abs;
        case 4: return !pla && !petg && !abs;
        default: return true;
    }
}

int leergewicht(int sorte) {
    if (sorte >= 0 && g_lager->sorten[sorte].leer_g > 0) return g_lager->sorten[sorte].leer_g;
    return g_st.leer_standard_g;
}

void zeige_seite(Seite s);

// ---------------------------------------------------------------- Kopfzeile
lv_obj_t* punkt(lv_obj_t* p) {
    lv_obj_t* d = box(p);
    lv_obj_set_size(d, S(8), S(8));
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, farbe(C_RAND), 0);
    return d;
}

void baue_kopf(lv_obj_t* scr) {
    lv_obj_t* k = box(scr);
    lv_obj_set_size(k, W, KOPF_H);
    lv_obj_set_style_border_side(k, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(k, 1, 0);
    lv_obj_set_style_border_color(k, farbe(C_LINIE), 0);

    lv_obj_t* links = zeile(k, S(10));
    lv_obj_align(links, LV_ALIGN_LEFT_MID, S(18), 0);
    lv_obj_t* logo = box(links);
    lv_obj_set_size(logo, S(20), S(20));
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(logo, 2, 0);
    lv_obj_set_style_border_color(logo, farbe(C_GRUEN), 0);
    lv_obj_t* loch = box(logo);
    lv_obj_set_size(loch, S(6), S(6));
    lv_obj_set_style_radius(loch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(loch, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(loch, farbe(C_GRUEN), 0);
    lv_obj_center(loch);
    text(links, F_G, C_TEXT, "FilamentLager");
    h_sub = text(links, F_K, C_SUB, "Waage");

    lv_obj_t* rechts = zeile(k, S(14));
    lv_obj_align(rechts, LV_ALIGN_RIGHT_MID, -S(18), 0);
    auto status = [&](const char* name, lv_obj_t** dot) {
        lv_obj_t* z = zeile(rechts, S(6));
        *dot = punkt(z);
        text(z, F_K, C_SUB, name);
    };
#if UI_KOMPAKT
    status("L", &h_nfc_l);
    status("R", &h_nfc_r);
    h_wlan = text(rechts, F_K, C_SUB, LV_SYMBOL_WIFI);
    status("HA", &h_ha);
#else
    status("NFC L", &h_nfc_l);
    status("NFC R", &h_nfc_r);
    h_wlan = text(rechts, F_K, C_SUB, LV_SYMBOL_WIFI " WLAN");
    status("Home Assistant", &h_ha);
#endif
    h_zeit = text(rechts, F_N, C_TEXT, "--:--");
}

// ---------------------------------------------------------------- Navigation
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
#if UI_KOMPAKT
    const char* namen[3] = {LV_SYMBOL_HOME " Wiegen", LV_SYMBOL_LIST " Lager", LV_SYMBOL_SETTINGS " Einstellungen"};
#else
    const char* namen[3] = {LV_SYMBOL_HOME "  Wiegen", LV_SYMBOL_LIST "  Lager", LV_SYMBOL_SETTINGS "  Einstellungen"};
#endif
    for (int i = 0; i < 3; i++) {
        g_nav[i] = knopf(n, namen[i], Art::FLACH, nav_cb, (void*)(intptr_t)i);
        lv_obj_set_height(g_nav[i], NAV_H - S(10));
        lv_obj_set_flex_grow(g_nav[i], 1);
    }
}

void nav_markieren(int aktiv) {
    for (int i = 0; i < 3; i++) {
        bool an = i == aktiv;
        lv_obj_set_style_bg_color(g_nav[i], farbe(an ? C_NAV_AKT : C_PANEL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g_nav[i], 0), farbe(an ? C_GRUEN_T : C_NAV), 0);
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

void baue_wiegen(lv_obj_t* s) {
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s, S(14), 0);

    lv_obj_t* karte = box(s);
    panel_stil(karte, C_GRUEN);
    lv_obj_set_height(karte, lv_pct(100));
    lv_obj_set_flex_grow(karte, 1);
    lv_obj_set_style_pad_hor(karte, K(22, 10), 0);
    lv_obj_set_flex_flow(karte, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(karte, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(karte, K(22, 10), 0);

    lv_obj_t* ring = box(karte);
    lv_obj_set_size(ring, K(168, 76), K(168, 76));
    w_arc = lv_arc_create(ring);
    lv_obj_remove_style(w_arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(w_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(w_arc, K(168, 76), K(168, 76));
    lv_arc_set_rotation(w_arc, 270);
    lv_arc_set_bg_angles(w_arc, 0, 360);
    lv_arc_set_range(w_arc, 0, 100);
    lv_arc_set_value(w_arc, 0);
    lv_obj_set_style_arc_width(w_arc, K(14, 8), LV_PART_MAIN);
    lv_obj_set_style_arc_width(w_arc, K(14, 8), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(w_arc, farbe(C_RAND2), LV_PART_MAIN);
    lv_obj_set_style_arc_color(w_arc, farbe(C_GRUEN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(w_arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(w_arc, 0, 0);
    lv_obj_center(w_arc);
    w_spule = box(ring);
    lv_obj_set_size(w_spule, K(92, 40), K(92, 40));
    lv_obj_set_style_radius(w_spule, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(w_spule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(w_spule, farbe(C_RAND), 0);
    lv_obj_set_style_border_width(w_spule, 3, 0);
    lv_obj_set_style_border_color(w_spule, farbe(C_RAND2), 0);
    lv_obj_center(w_spule);
    lv_obj_t* nabe = box(w_spule);
    lv_obj_set_size(nabe, K(26, 12), K(26, 12));
    lv_obj_set_style_radius(nabe, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(nabe, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(nabe, farbe(C_PANEL), 0);
    lv_obj_center(nabe);

    lv_obj_t* sp = spalte(karte, S(6));
    w_kopf = text(sp, F_K, C_SUB, "FILAMENT ÜBRIG");
    ueberschrift(w_kopf);
    lv_obj_t* zahl = zeile(sp, S(8));
    lv_obj_set_flex_align(zahl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    w_zahl = text(zahl, F_Z, C_TEXT, "0");
    w_einheit = text(zahl, F_T, C_SUB, "g");
    lv_obj_set_style_pad_bottom(w_einheit, S(14), 0);
    w_proz = text(sp, F_N, C_SUB, "");
    lv_obj_t* chips = zeile(sp, S(8));
#if UI_KOMPAKT
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_COLUMN);  // nebeneinander zu breit
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(chips, 4, 0);
#endif
    lv_obj_set_style_pad_top(chips, S(8), 0);
    w_brutto = chip(chips, "Brutto 0 g", C_CHIP, C_TEXT);
    w_leer = chip(chips, "Leerspule 250 g", C_CHIP, C_TEXT);

    lv_obj_t* seite_r = box(s);
    lv_obj_set_size(seite_r, K(270, 190), lv_pct(100));
    lv_obj_set_flex_flow(seite_r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(seite_r, S(10), 0);

    lv_obj_t* k = box(seite_r);
    panel_stil(k, C_GRUEN);
    lv_obj_set_width(k, lv_pct(100));
    lv_obj_set_flex_grow(k, 1);
    lv_obj_set_style_pad_hor(k, S(16), 0);
    lv_obj_set_style_pad_ver(k, S(14), 0);
    lv_obj_set_flex_flow(k, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(k, S(8), 0);
    w_k_titel = text(k, F_K, C_SUB, "");
    ueberschrift(w_k_titel);
    einzeilig(w_k_titel, S(234));
    lv_obj_t* kz = zeile(k, S(12));
    w_k_swatch = farbfeld(kz, S(34), S(9), C_RAND);
    lv_obj_t* kt = spalte(kz, S(2));
    w_k_name = text(kt, F_G, C_TEXT, "");
    einzeilig(w_k_name, S(188));
    w_k_sub = box(kt);  // Herstellerlogo oder Text
    lv_obj_set_size(w_k_sub, S(188), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(w_k_sub, lv_font_get_line_height(F_K), 0);
    w_k_chips = box(k);
    lv_obj_set_size(w_k_chips, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(w_k_chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(w_k_chips, S(6), 0);
    lv_obj_set_style_pad_row(w_k_chips, S(6), 0);

    w_haupt = knopf(seite_r, LV_SYMBOL_OK "  Rest speichern", Art::PRIMAER, haupt_cb, nullptr, &w_haupt_lbl);
    lv_obj_set_size(w_haupt, lv_pct(100), S(58));
    lv_obj_t* r2 = box(seite_r);
    lv_obj_set_size(r2, lv_pct(100), S(50));
    lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r2, S(10), 0);
    lv_obj_set_style_pad_column(r2, S(8), 0);
    w_tara = knopf(r2, "Tara", Art::RAHMEN, tara_cb);
    w_wahl = knopf(r2, "Andere", Art::RAHMEN, andere_cb, nullptr, &w_wahl_lbl);
    w_etikett = knopf(r2, "Etikett", Art::RAHMEN, etikett_wiegen_cb);
    for (lv_obj_t* b : {w_tara, w_wahl, w_etikett}) {
        lv_obj_set_height(b, S(50));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 2, 0);
        if (UI_KOMPAKT) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), F_K, 0);
    }
}

// Zweite Zeile der Karte: Herstellerlogo/-name oder ein Text
void karte_sub_text(const std::string& t) {
    lv_obj_clean(w_k_sub);
    lv_obj_t* l = text(w_k_sub, F_K, C_SUB, t.c_str());
    einzeilig(l, S(188));
}

void karte_sub_marke(const std::string& hersteller) {
    lv_obj_clean(w_k_sub);
    marke(w_k_sub, hersteller, true, F_K, C_SUB, S(188));
}

std::string ams_text(const AmsTreffer& t, bool mit_rest) {
    std::string s = t.drucker->name + " · " + ams_slot_kurz(*t.ams, *t.slot);
    if (mit_rest && t.slot->rest >= 0) s += " · " + std::to_string(t.slot->rest) + " %";
    return s;
}

void karte_chips(const std::vector<std::pair<std::string, int>>& chips) {
    // 0 = grau, 1 = grün, 2 = orange (Rahmen), 3 = blau
    lv_obj_clean(w_k_chips);
    for (auto& c : chips) {
        switch (c.second) {
            case 1: chip(w_k_chips, c.first, C_CHIP_G, C_GRUEN_T); break;
            case 2: chip(w_k_chips, c.first, 0, C_ORANGE_T, 0x9a6a2a); break;
            case 3: chip(w_k_chips, c.first, C_CHIP_B, C_BLAU_T); break;
            default: chip(w_k_chips, c.first, C_CHIP, C_TEXT); break;
        }
    }
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
    lv_obj_set_style_bg_color(w_spule, farbe(sorte >= 0 ? g_lager->sorten[sorte].rgb : C_RAND), 0);
    setze(w_zahl, std::to_string(da ? netto : 0));
    if (!st.sensor_ok) setze(w_proz, "Sensor antwortet nicht");
    else if (!da) setze(w_proz, "Spule auflegen");
    else setze(w_proz, std::to_string(proz) + " % von " + gramm(soll) + (st.stabil ? "" : " · misst …"));
    setze(lv_obj_get_child(w_brutto, 0), "Brutto " + gramm(brutto));
    setze(lv_obj_get_child(w_leer, 0), "Leerspule " + gramm(leer));

    std::string schluessel = std::to_string(sorte) + "|" + tag + "|" + (da ? "1" : "0") + "|" +
                             std::to_string(g_ams_stand) + "|" + std::to_string(g_logo_stand);
    if (sorte >= 0) {
        const Filament& f = g_lager->sorten[sorte];
        schluessel += "|" + f.description;
    }
    knopf_aktiv(w_etikett, sorte >= 0);
    if (schluessel == g_karte_schluessel) return;
    g_karte_schluessel = schluessel;
    AmsTreffer ams = g_ams ? ams_slot_fuer_tag(*g_ams, tag) : AmsTreffer{};
    g_vorschlag = -1;

    if (sorte >= 0) {
        const Filament& f = g_lager->sorten[sorte];
        setze(w_k_titel, tag.empty() ? "GEWÄHLTE SORTE" : std::string(KT("ERKANNT · NFC ", "NFC ")) + leser);
        lv_obj_set_style_bg_color(w_k_swatch, farbe(f.rgb), 0);
        setze(w_k_name, f.kurzname());
        karte_sub_marke(f.hersteller);
        std::vector<std::pair<std::string, int>> c;
        if (ams) c.push_back({ams_text(ams, true), 3});
        for (size_t i = 0; i < f.besonderheiten.size() && i < (UI_KOMPAKT ? 1u : 2u); i++) c.push_back({f.besonderheiten[i], 0});
        c.push_back({spulen_text(f.spulen) + KT(" im Lager", ""), 1});
        if (!f.reste.empty()) c.push_back({KT("Rest bisher ", "Rest ") + gramm(f.reste.back()), 2});
        karte_chips(c);
        g_haupt = da ? Haupt::SPEICHERN : Haupt::NICHTS;
        setze(w_haupt_lbl, LV_SYMBOL_OK "  Rest speichern");
        setze(w_wahl_lbl, "Andere");
    } else if (!tag.empty()) {
        setze(w_k_titel, std::string(KT("NEUER TAG · NFC ", "NEU · NFC ")) + leser);
        g_vorschlag = ams && g_lager ? ams_vorschlag(*g_lager, *ams.slot) : -1;
        if (g_vorschlag >= 0) {
            // Das AMS kennt die Spule: Bambu-ID und Farbe passen zu einer Sorte im Lager
            const Filament& v = g_lager->sorten[g_vorschlag];
            lv_obj_set_style_bg_color(w_k_swatch, farbe(v.rgb), 0);
            setze(w_k_name, v.kurzname());
            karte_sub_marke(v.hersteller);
            karte_chips({{ams_text(ams, false), 3}, {KT("Vorschlag aus dem AMS", "Vorschlag AMS"), 1}});
            g_haupt = Haupt::VORSCHLAG;
            setze(w_haupt_lbl, LV_SYMBOL_OK "  Übernehmen");
        } else {
            lv_obj_set_style_bg_color(w_k_swatch, farbe(ams ? ams.slot->rgb : C_RAND), 0);
            setze(w_k_name, "Neue Spule");
            karte_sub_text(ams && !ams.slot->name.empty() ? ams.slot->name : "UID " + tag);
            if (ams) karte_chips({{ams_text(ams, false), 3}, {KT("Keiner Sorte zugeordnet", "Nicht zugeordnet"), 2}});
            else karte_chips({{KT("Keiner Sorte zugeordnet", "Nicht zugeordnet"), 2}});
            g_haupt = Haupt::ZUORDNEN;
            setze(w_haupt_lbl, KT(LV_SYMBOL_LIST "  Sorte zuordnen", LV_SYMBOL_LIST "  Zuordnen"));
        }
        setze(w_wahl_lbl, "Wählen");
    } else {
        setze(w_k_titel, "KEINE SPULE ERKANNT");
        lv_obj_set_style_bg_color(w_k_swatch, farbe(C_RAND2), 0);
        setze(w_k_name, da ? "Ohne NFC-Tag" : "Waage ist leer");
        karte_sub_text(da ? "Sorte aus dem Lager wählen" : "Spule mit Tag auflegen");
        karte_chips({});
        g_haupt = da ? Haupt::WAEHLEN : Haupt::NICHTS;
        setze(w_haupt_lbl, da ? LV_SYMBOL_LIST "  Sorte wählen" : LV_SYMBOL_OK "  Rest speichern");
        setze(w_wahl_lbl, "Wählen");
    }
    knopf_aktiv(w_haupt, g_haupt != Haupt::NICHTS);
}

// ---------------------------------------------------------------- Lager
void lager_liste_bauen();

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

void filter_cb(lv_event_t* e) {
    g_filter = (int)(intptr_t)lv_event_get_user_data(e);
    lager_liste_bauen();
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

void baue_lager(lv_obj_t* s) {
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s, S(14), 0);
    lv_obj_set_style_pad_ver(s, S(12), 0);

    lv_obj_t* links = box(s);
    lv_obj_set_height(links, lv_pct(100));
    lv_obj_set_flex_grow(links, 1);
    lv_obj_set_flex_flow(links, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(links, S(10), 0);

    lv_obj_t* oben = box(links);
    lv_obj_set_size(oben, lv_pct(100), S(40));
    lv_obj_set_flex_flow(oben, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(oben, S(8), 0);
    l_suche = lv_textarea_create(oben);
    lv_textarea_set_one_line(l_suche, true);
    lv_textarea_set_placeholder_text(l_suche, "Suchen …");
    lv_obj_set_height(l_suche, S(40));
    lv_obj_set_flex_grow(l_suche, 1);
    lv_obj_set_style_bg_color(l_suche, farbe(C_PANEL), 0);
    lv_obj_set_style_border_color(l_suche, farbe(C_RAND2), 0);
    lv_obj_set_style_border_width(l_suche, 1, 0);
    lv_obj_set_style_radius(l_suche, S(12), 0);
    lv_obj_set_style_text_font(l_suche, F_K, 0);
    lv_obj_set_style_text_color(l_suche, farbe(C_TEXT), 0);
    lv_obj_set_style_pad_ver(l_suche, S(10), 0);
    lv_obj_add_event_cb(l_suche, suche_cb, LV_EVENT_ALL, nullptr);
    for (int i = 1; i < 5; i++) {
        l_filter[i] = knopf(oben, FILTER[i], Art::RAHMEN, filter_cb, (void*)(intptr_t)i);
        lv_obj_set_size(l_filter[i], LV_SIZE_CONTENT, S(40));
        lv_obj_set_style_pad_hor(l_filter[i], S(10), 0);
        lv_obj_set_style_radius(l_filter[i], S(20), 0);
        lv_obj_set_style_border_width(l_filter[i], 1, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(l_filter[i], 0), F_K, 0);
    }
    l_filter[0] = knopf(oben, FILTER[0], Art::RAHMEN, filter_cb, (void*)(intptr_t)0);
    lv_obj_set_size(l_filter[0], LV_SIZE_CONTENT, S(40));
    lv_obj_set_style_pad_hor(l_filter[0], S(10), 0);
    lv_obj_set_style_radius(l_filter[0], S(20), 0);
    lv_obj_set_style_border_width(l_filter[0], 1, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(l_filter[0], 0), F_K, 0);

    l_liste = box(links);
    panel_stil(l_liste, C_GRUEN);
    lv_obj_set_width(l_liste, lv_pct(100));
    lv_obj_set_flex_grow(l_liste, 1);
    lv_obj_set_style_pad_hor(l_liste, S(14), 0);
    lv_obj_set_flex_flow(l_liste, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(l_liste, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(l_liste, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l_liste, LV_SCROLLBAR_MODE_ACTIVE);

    l_reste = box(s);
#if UI_KOMPAKT
    lv_obj_add_flag(l_reste, LV_OBJ_FLAG_HIDDEN);  // kein Platz – angebrochene Rollen stehen in der Liste
#endif
    panel_stil(l_reste, C_ORANGE);
    lv_obj_set_size(l_reste, S(232), lv_pct(100));
    lv_obj_set_style_pad_hor(l_reste, S(14), 0);
    lv_obj_set_style_pad_ver(l_reste, S(12), 0);
    lv_obj_set_flex_flow(l_reste, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l_reste, S(6), 0);

    l_tastatur = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(l_tastatur, W, S(220));
    lv_obj_align(l_tastatur, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(l_tastatur, l_suche);
    lv_obj_add_event_cb(l_tastatur, tastatur_cb, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(l_tastatur, LV_OBJ_FLAG_HIDDEN);
}

void lager_zeile(lv_obj_t* liste, const Filament& f, int index) {
    lv_obj_t* z = box(liste);
    lv_obj_set_size(z, lv_pct(100), S(54));
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(z, farbe(C_LINIE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(z, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(z, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(z, 1, 0);
    lv_obj_set_style_border_color(z, farbe(C_LINIE), 0);
    lv_obj_set_flex_flow(z, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(z, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(z, S(12), 0);
    lv_obj_add_event_cb(z, zeile_cb, LV_EVENT_CLICKED, (void*)(intptr_t)index);

    farbfeld(z, S(30), S(8), f.rgb);
    lv_obj_t* mitte = spalte(z, S(2));
    lv_obj_set_flex_grow(mitte, 1);
    lv_obj_t* n = text(mitte, F_N, C_TEXT, f.kurzname().c_str());
    einzeilig(n, f.nfc.empty() ? K(300, 270) : K(250, 228));
    // Unterzeile: Herstellerlogo (oder Name) · Besonderheiten
    const int ub = f.nfc.empty() ? K(300, 270) : K(250, 228);
    lv_obj_t* u = zeile(mitte, S(6));
    int mb = 0;
    if (!f.hersteller.empty()) marke(u, f.hersteller, false, F_K, C_SUB, ub / 2, &mb);
    std::string bes;
    for (size_t i = 0; i < f.besonderheiten.size() && i < 2; i++) bes += (bes.empty() && f.hersteller.empty() ? "" : "· ") + f.besonderheiten[i] + " ";
    if (!bes.empty() && ub - mb - S(6) > 20) einzeilig(text(u, F_K, C_SUB, bes.c_str()), ub - mb - S(6));
    if (!f.nfc.empty()) chip(z, "NFC", C_CHIP_B, C_BLAU_T);
    lv_obj_t* r = spalte(z, 0);
    lv_obj_set_width(r, K(86, 72));
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    text(r, KT(F_N, F_K), f.spulen ? C_TEXT : C_ROT, spulen_text(f.spulen).c_str());
    text(r, F_K, C_SUB, rest_text(f).c_str());
}

void lager_liste_bauen() {
    if (!g_lager) return;
    for (int i = 0; i < 5; i++) {
        bool an = i == g_filter;
        lv_obj_set_style_bg_color(l_filter[i], farbe(an ? C_GRUEN : C_BG), 0);
        lv_obj_set_style_border_color(l_filter[i], farbe(an ? C_GRUEN : C_RAND), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(l_filter[i], 0), farbe(an ? C_BG : C_NAV), 0);
    }
    lv_obj_clean(l_liste);
    std::string q = klein(lv_textarea_get_text(l_suche));
    int n = 0, weitere = 0;
    for (size_t i = 0; i < g_lager->sorten.size(); i++) {
        const Filament& f = g_lager->sorten[i];
        if (!passt_filter(f, g_filter)) continue;
        if (!q.empty() && klein(f.summary + " " + f.hersteller + " " + f.farbe).find(q) == std::string::npos) continue;
        if (n >= MAX_ZEILEN) { weitere++; continue; }
        lager_zeile(l_liste, f, (int)i);
        n++;
    }
    if (n == 0) {
        lv_obj_t* l = text(l_liste, F_N, C_SUB, g_lager->sorten.empty() ? "Lager wird geladen …" : "Keine passende Sorte");
        lv_obj_set_style_pad_top(l, S(16), 0);
    }
    if (weitere) {
        lv_obj_t* l = text(l_liste, F_K, C_SUB, ("… und " + std::to_string(weitere) + " weitere – Suche verwenden").c_str());
        lv_obj_set_style_pad_ver(l, S(12), 0);
    }
    lv_obj_scroll_to_y(l_liste, 0, LV_ANIM_OFF);
}

void reste_bauen() {
    if (UI_KOMPAKT) return;
    lv_obj_clean(l_reste);
    lv_obj_t* t = text(l_reste, F_K, C_ORANGE_T, "ANGEBROCHENE ROLLEN");
    ueberschrift(t);
    if (!g_lager) return;
    struct R { int sorte; int g; };
    std::vector<R> alle;
    for (size_t i = 0; i < g_lager->sorten.size(); i++)
        for (int g : g_lager->sorten[i].reste) alle.push_back({(int)i, g});
    std::sort(alle.begin(), alle.end(), [](const R& a, const R& b) { return a.g > b.g; });
    if (alle.empty()) text(l_reste, F_K, C_SUB, "Keine angebrochenen Rollen");
    for (size_t k = 0; k < alle.size() && k < 7; k++) {
        const Filament& f = g_lager->sorten[alle[k].sorte];
        lv_obj_t* z = zeile(l_reste, S(10));
        lv_obj_set_width(z, lv_pct(100));
        lv_obj_set_style_pad_ver(z, S(4), 0);
        farbfeld(z, S(22), S(6), f.rgb);
        lv_obj_t* m = spalte(z, S(4));
        lv_obj_set_flex_grow(m, 1);
        lv_obj_t* n = text(m, F_K, C_TEXT, f.kurzname().c_str());
        einzeilig(n, S(118));
        lv_obj_t* bar = lv_bar_create(m);
        lv_obj_set_size(bar, S(118), S(4));
        lv_bar_set_range(bar, 0, std::max(1, f.gewicht));
        lv_bar_set_value(bar, std::max(alle[k].g, f.gewicht / 50), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, farbe(C_RAND2), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, farbe(alle[k].g < 250 ? C_ROT : C_ORANGE), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, S(2), LV_PART_MAIN);
        lv_obj_set_style_radius(bar, S(2), LV_PART_INDICATOR);
        lv_obj_t* g = text(z, F_K, C_TEXT, gramm(alle[k].g).c_str());
        lv_obj_set_width(g, S(50));
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
    if (!f.hersteller.empty()) marke(tn, f.hersteller, true, F_K, C_SUB, K(210, 150));

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
    lv_obj_set_style_text_color(h_wlan, farbe(s.wlan_ok ? C_SUB : C_ROT), 0);
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
    else if (name.rfind("etikett:", 0) == 0) et_oeffnen(atoi(name.c_str() + 8), 562);
    else if (name.rfind("detail:", 0) == 0) { g_detail = atoi(name.c_str() + 7); zeige_seite(DETAIL); }
    else zeige_seite(WIEGEN);
}
