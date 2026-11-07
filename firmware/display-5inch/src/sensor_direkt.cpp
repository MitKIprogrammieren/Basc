// Messung direkt am SC01 Plus – siehe sensor_direkt.h
#include "config.h"

#if SENSOR_DIREKT

#include "sensor_direkt.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>

#include <algorithm>

#include "../../sensor-s3zero/src/nau7802.h"
#include "../../sensor-s3zero/src/pn532_hsu.h"
#include "../../sensor-s3zero/src/nfc_schreiben.h"
#include "../../sensor-s3zero/src/sensor.h"  // Schnittstelle zur Web-Oberfläche

namespace {

Nau7802 g_nau;
Pn532Hsu g_nfc_l, g_nfc_r;
Preferences g_prefs;

std::mutex g_mx;  // schützt alles, was Oberfläche und Tasks gemeinsam lesen
std::deque<std::pair<bool, std::string>> g_meldungen;

// ---------------------------------------------------------------- Kalibrierung (eigener Bereich „waage“)
long g_offset = 0;
float g_faktor = 0;  // Rohwert pro Gramm, 0 = nicht kalibriert

void kal_laden() {
    g_prefs.begin("waage", true);
    g_offset = g_prefs.getLong("offset", 0);
    g_faktor = g_prefs.getFloat("faktor", 0);
    g_prefs.end();
}

void kal_speichern() {
    g_prefs.begin("waage", false);
    g_prefs.putLong("offset", g_offset);
    g_prefs.putFloat("faktor", g_faktor);
    g_prefs.end();
}

// ---------------------------------------------------------------- Messung (nur Mess-Task)
constexpr int MEDIAN_N = 5, STABIL_N = 10, PROBEN = 15;
long g_roh[MEDIAN_N];
int g_roh_pos = 0, g_roh_anzahl = 0;
long g_roh_median = 0;
float g_gramm = 0, g_anzeige = 0;
bool g_gramm_gueltig = false, g_stabil = false;
float g_verlauf[STABIL_N];
int g_verlauf_pos = 0, g_verlauf_anzahl = 0;
uint32_t g_letzte_messung = 0, g_letzte_suche = 0;

enum class Vorgang { KEINER, TARA, KAL };
Vorgang g_vorgang = Vorgang::KEINER;
Vorgang g_vorgang_neu = Vorgang::KEINER;  // von der Oberfläche angefordert, geschützt durch g_mx
int g_kal_gramm = 0;
int64_t g_summe = 0;
int g_proben = 0;

// ---------------------------------------------------------------- NFC (nur NFC-Task, Ergebnis unter g_mx)
std::string g_tag[2];  // 0 = links, 1 = rechts
String g_bambu[2];      // Inhalt eines Bambu-Lab-Tags als JSON (leer = keiner oder noch nicht gelesen)
String g_bambu_uid[2];  // zu welcher UID g_bambu gehört
uint8_t g_bambu_versuche[2] = {0, 0};
uint32_t g_tag_zeit[2] = {0, 0};
uint32_t g_nfc_versuch[2] = {0, 0};

long median() {
    long t[MEDIAN_N];
    int n = g_roh_anzahl;
    for (int i = 0; i < n; i++) t[i] = g_roh[i];
    std::sort(t, t + n);
    return t[n / 2];
}

void messen(long roh) {
    g_letzte_messung = millis();
    g_roh[g_roh_pos] = roh;
    g_roh_pos = (g_roh_pos + 1) % MEDIAN_N;
    if (g_roh_anzahl < MEDIAN_N) g_roh_anzahl++;
    g_roh_median = median();
    if (g_faktor == 0) {
        g_gramm = g_anzeige = 0;
        g_stabil = false;
        return;
    }
    float neu = (g_roh_median - g_offset) / g_faktor;
    // große Sprünge (Spule aufgelegt) sofort übernehmen, sonst glätten
    if (!g_gramm_gueltig || fabsf(neu - g_gramm) > 30) g_gramm = neu;
    else g_gramm += GLAETTUNG * (neu - g_gramm);
    g_gramm_gueltig = true;
    g_verlauf[g_verlauf_pos] = g_gramm;
    g_verlauf_pos = (g_verlauf_pos + 1) % STABIL_N;
    if (g_verlauf_anzahl < STABIL_N) g_verlauf_anzahl++;
    float mn = 1e9, mx = -1e9;
    for (int i = 0; i < g_verlauf_anzahl; i++) {
        mn = std::min(mn, g_verlauf[i]);
        mx = std::max(mx, g_verlauf[i]);
    }
    g_stabil = g_verlauf_anzahl == STABIL_N && (mx - mn) < STABIL_BAND_G;
    if (!g_stabil || fabsf(g_gramm - g_anzeige) >= ANZEIGE_HYST_G) g_anzeige = g_gramm;
}

// ---------------------------------------------------------------- Nullpunkt-Nachführung
// Wie auf der Sensorplatine: ruhig, nahe 0 g, kein Tag → Nullpunkt langsam nachziehen (höchstens 3 g/min).
constexpr float NULL_BAND_G = 10.0f;
constexpr uint32_t NULL_WARTE_MS = 30000;
constexpr float NULL_MAX_G = 0.005f;
uint32_t g_null_seit = 0, g_null_gespeichert = 0;
bool g_null_geaendert = false;

void nullpunkt_nachfuehren() {
    bool tag;
    {
        std::lock_guard<std::mutex> l(g_mx);
        tag = !g_tag[0].empty() || !g_tag[1].empty();
    }
    if (!g_null_auto || g_faktor == 0 || g_vorgang != Vorgang::KEINER || !g_stabil || tag || fabsf(g_gramm) >= NULL_BAND_G) {
        g_null_seit = 0;
    } else if (!g_null_seit) {
        g_null_seit = millis();
    } else if (millis() - g_null_seit >= NULL_WARTE_MS && fabsf(g_gramm) > 0.05f) {
        float korr = g_gramm > NULL_MAX_G ? NULL_MAX_G : g_gramm < -NULL_MAX_G ? -NULL_MAX_G : g_gramm;
        float roh_abw = korr * g_faktor;
        long schritt = lroundf(roh_abw);
        if (schritt == 0) schritt = roh_abw > 0 ? 1 : -1;
        g_offset += schritt;
        float d = schritt / g_faktor;
        g_gramm -= d;
        g_anzeige -= d;
        for (int i = 0; i < g_verlauf_anzahl; i++) g_verlauf[i] -= d;
        g_null_geaendert = true;
    }
    if (g_null_geaendert && millis() - g_null_gespeichert >= 30UL * 60 * 1000) {
        g_null_gespeichert = millis();
        g_null_geaendert = false;
        kal_speichern();
    }
}

// NAU7802 suchen – auch mit vertauschtem SDA/SCL
void waage_suchen() {
    static bool gemeldet = false;
    if (g_nau.begin(WAAGE_SDA, WAAGE_SCL, Wire1)) {
        Serial.printf("[waage] NAU7802 gefunden (SDA=GPIO%d, SCL=GPIO%d)\n", WAAGE_SDA, WAAGE_SCL);
        return;
    }
    if (g_nau.begin(WAAGE_SCL, WAAGE_SDA, Wire1)) {
        Serial.println("[waage] NAU7802 gefunden – SDA und SCL sind vertauscht angeschlossen, geht trotzdem");
        return;
    }
    if (!gemeldet) {
        gemeldet = true;
        Serial.printf("[waage] kein NAU7802 an GPIO%d/%d – Verkabelung prüfen, suche alle 5 s weiter\n", WAAGE_SDA,
                      WAAGE_SCL);
    }
}

}  // namespace

void SensorDirekt::begin() {
    kal_laden();
    waage_suchen();
    g_letzte_suche = millis();
    bool r = g_nfc_r.begin(Serial1, NFC_R_RX, NFC_R_TX, NFC_R_RST);
    bool l = g_nfc_l.begin(Serial2, NFC_L_RX, NFC_L_TX, NFC_L_RST);
    Serial.printf("[nfc] links %s, rechts %s\n", l ? "bereit" : "fehlt", r ? "bereit" : "fehlt");
}

void SensorDirekt::starten() {
    xTaskCreatePinnedToCore(mess_task, "waage", 4096, this, 2, nullptr, 0);
    xTaskCreatePinnedToCore(nfc_task, "nfc", 4096, this, 1, nullptr, 0);
}

void SensorDirekt::mess_task(void* selbst) {
    for (;;) {
        ((SensorDirekt*)selbst)->mess_schritt();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void SensorDirekt::nfc_task(void* selbst) {
    for (;;) {
        ((SensorDirekt*)selbst)->nfc_schritt();
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void SensorDirekt::melden(bool ok, const std::string& text) {
    {
        std::lock_guard<std::mutex> l(g_mx);
        if (g_meldungen.size() < 8) g_meldungen.push_back({ok, text});
    }
    if (g_vorgang_fertig) g_vorgang_fertig(ok, String(text.c_str()));  // Web-Oberfläche
}

void SensorDirekt::mess_schritt() {
    if (!g_nau.da()) {
        if (millis() - g_letzte_suche >= 5000) {
            g_letzte_suche = millis();
            waage_suchen();
        }
        return;
    }
    // neuer Vorgang von der Oberfläche?
    {
        std::lock_guard<std::mutex> l(g_mx);
        if (g_vorgang_neu != Vorgang::KEINER && g_vorgang == Vorgang::KEINER) {
            g_vorgang = g_vorgang_neu;
            g_summe = 0;
            g_proben = 0;
        }
        g_vorgang_neu = Vorgang::KEINER;
    }
    if (!g_nau.bereit()) {
        // 10 Messungen je Sekunde – kommt 2 s nichts, gilt der Wandler als weg und wird neu gesucht
        if (millis() - g_letzte_messung > 2000 && millis() - g_letzte_suche >= 5000) {
            g_letzte_suche = millis();
            waage_suchen();
        }
        return;
    }
    long roh = g_nau.lesen();
    {
        std::lock_guard<std::mutex> l(g_mx);
        messen(roh);
    }
    if (g_vorgang != Vorgang::KEINER) {
        g_summe += roh;
        if (++g_proben >= PROBEN) {
            long mittel = (long)(g_summe / g_proben);
            if (g_vorgang == Vorgang::TARA) {
                {
                    std::lock_guard<std::mutex> l(g_mx);
                    g_offset = mittel;
                    g_gramm = g_anzeige = 0;
                    g_gramm_gueltig = false;
                    g_verlauf_anzahl = 0;
                }
                kal_speichern();
                melden(true, "Tara gesetzt");
            } else {
                float f = (float)(mittel - g_offset) / (float)g_kal_gramm;
                if (fabsf(f) < 1.0f) {
                    melden(false, "Kein Gewicht erkannt – erst Tara bei leerem Teller, dann Gewicht auflegen");
                } else {
                    {
                        std::lock_guard<std::mutex> l(g_mx);
                        g_faktor = f;
                        g_gramm_gueltig = false;
                        g_verlauf_anzahl = 0;
                    }
                    kal_speichern();
                    melden(true, "Kalibriert mit " + std::to_string(g_kal_gramm) + " g");
                }
            }
            g_vorgang = Vorgang::KEINER;
        }
    }
    nullpunkt_nachfuehren();
}

void SensorDirekt::nfc_schritt() {
    Pn532Hsu* leser[2] = {&g_nfc_l, &g_nfc_r};
    for (int i = 0; i < 2; i++) {
        if (!leser[i]->bereit()) {
            {
                std::lock_guard<std::mutex> l(g_mx);
                g_tag[i].clear();
            }
            if (millis() - g_nfc_versuch[i] >= NFC_NEUSTART_MS) {
                g_nfc_versuch[i] = millis();
                leser[i]->neu_starten();
            }
            continue;
        }
        String uid = leser[i]->lese_uid();
        // Neuer Tag: bis zu dreimal versuchen, ihn als Bambu-Lab-Tag auszulesen (wie auf der Sensorplatine)
        String bambu;
        bool bambu_neu = false;
        if (uid.length() && uid != g_bambu_uid[i]) {
            g_bambu_uid[i] = uid;
            g_bambu_versuche[i] = 0;
            bambu_neu = true;
        }
        bool leer;
        {
            std::lock_guard<std::mutex> l(g_mx);
            leer = g_bambu[i].isEmpty();
        }
        if (uid.length() && g_bambu_versuche[i] < 3 && (bambu_neu || leer)) {
            g_bambu_versuche[i]++;
            BambuTag t;
            if (leser[i]->lese_bambu(t)) {
                JsonDocument b;
                b["uid"] = uid;
                b["material_id"] = t.material_id;
                b["variante"] = t.variante;
                b["typ"] = t.typ;
                b["detail"] = t.detail;
                b["farbe"] = t.farbe;
                b["gewicht"] = t.gewicht;
                b["durchmesser"] = roundf(t.durchmesser * 100) / 100;
                if (t.trocknen_c >= 30 && t.trocknen_c <= 120) b["trocknen_c"] = t.trocknen_c;
                if (t.trocknen_h >= 1 && t.trocknen_h <= 48) b["trocknen_h"] = t.trocknen_h;
                if (t.bett >= 20 && t.bett <= 130) b["bett"] = t.bett;
                if (t.duese_max >= 150 && t.duese_max <= 400) b["duese_max"] = t.duese_max;
                if (t.duese_min >= 150 && t.duese_min <= 400) b["duese_min"] = t.duese_min;
                serializeJson(b, bambu);
                g_bambu_versuche[i] = 3;  // fertig
            }
            bambu_neu = true;
        }
        // NFC-Aufkleber beschreiben, wenn SchichtLohn das für genau diesen Tag möchte
        nfc_schreiben::tag_stand(i, uid, uid.length() && leser[i]->ist_ntag());
        if (uid.length()) {
            String ziel;
            if (nfc_schreiben::auftrag_fuer(i, uid, ziel)) {
                String grund;
                bool ok = leser[i]->schreibe_url(ziel, grund);
                nfc_schreiben::ergebnis(uid, ok, grund);
            }
        }
        uint32_t jetzt = millis();
        std::lock_guard<std::mutex> l(g_mx);
        if (bambu_neu) g_bambu[i] = bambu;
        if (uid.length()) {
            g_tag[i] = uid.c_str();
            g_tag_zeit[i] = jetzt;
        } else if (!g_tag[i].empty() && jetzt - g_tag_zeit[i] > TAG_WEG_MS) {
            g_tag[i].clear();
            g_bambu[i] = "";
            g_bambu_uid[i] = "";
        }
    }
}

SensorDaten SensorDirekt::daten() {
    SensorDaten d;
    std::lock_guard<std::mutex> l(g_mx);
    d.gramm = roundf(g_anzeige * 10) / 10;
    d.stabil = g_stabil;
    d.kalibriert = g_faktor != 0;
    d.tag_l = g_tag[0];
    d.tag_r = g_tag[1];
    // „verbunden“ heißt hier: Wandler da und liefert Messwerte
    d.verbunden = g_nau.da() && g_letzte_messung && millis() - g_letzte_messung < 1000;
    return d;
}

void SensorDirekt::tara() {
    std::lock_guard<std::mutex> l(g_mx);
    g_vorgang_neu = Vorgang::TARA;
}

void SensorDirekt::kalibrieren(int gramm) {
    if (gramm <= 0) {
        melden(false, "Kalibriergewicht fehlt");
        return;
    }
    std::lock_guard<std::mutex> l(g_mx);
    if (g_faktor == 0 && g_offset == 0) {
        g_meldungen.push_back({false, "Erst Tara bei leerem Teller"});
        return;
    }
    g_kal_gramm = gramm;
    g_vorgang_neu = Vorgang::KAL;
}

bool SensorDirekt::meldung_holen(bool& ok, std::string& text) {
    std::lock_guard<std::mutex> l(g_mx);
    if (g_meldungen.empty()) return false;
    ok = g_meldungen.front().first;
    text = g_meldungen.front().second;
    g_meldungen.pop_front();
    return true;
}

bool SensorDirekt::waage_da() { return g_nau.da(); }
String SensorDirekt::bambu(int seite) {
    std::lock_guard<std::mutex> l(g_mx);
    return g_bambu[seite ? 1 : 0];
}
bool SensorDirekt::beschaeftigt() {
    std::lock_guard<std::mutex> l(g_mx);
    return g_vorgang != Vorgang::KEINER || g_vorgang_neu != Vorgang::KEINER;
}
bool SensorDirekt::waage_liefert() { return g_nau.da() && g_letzte_messung && millis() - g_letzte_messung < 1000; }
void SensorDirekt::kalibrierung_loeschen() {
    {
        std::lock_guard<std::mutex> l(g_mx);
        g_offset = 0;
        g_faktor = 0;
    }
    kal_speichern();
}
bool SensorDirekt::nfc_bereit(int seite) { return (seite ? g_nfc_r : g_nfc_l).bereit(); }
long SensorDirekt::roh() {
    std::lock_guard<std::mutex> l(g_mx);
    return g_roh_median;
}

// ---------------------------------------------------------------- für die Web-Oberfläche (sensor.h)
volatile bool g_null_auto = true;
void (*g_vorgang_fertig)(bool ok, const String& text) = nullptr;

#endif  // SENSOR_DIREKT
