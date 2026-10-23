#include "model.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdlib>

namespace {

constexpr int LEER_GRENZE_G = 10;   // darunter gilt eine Spule als leer
constexpr int VOLL_TOLERANZ_G = 15; // so nah am Sollgewicht gilt sie als voll
constexpr int MESS_TOLERANZ_G = 5;  // Rest darf beim Wiegen leicht „zunehmen“

int als_int(JsonVariantConst v, int standard) {
    if (v.is<int>()) return v.as<int>();
    if (v.is<float>()) return (int)(v.as<float>() + 0.5f);
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        if (s && *s) return atoi(s);
    }
    return standard;
}

std::vector<int> reste_lesen(JsonVariantConst v, int gewicht) {
    std::vector<int> out;
    if (v.is<JsonArrayConst>()) {
        for (JsonVariantConst x : v.as<JsonArrayConst>()) {
            int g = als_int(x, -1);
            if (g > 0 && (gewicht <= 0 || g < gewicht)) out.push_back(g);
        }
    }
    return out;
}

void reste_schreiben(JsonDocument& doc, const std::vector<int>& reste) {
    JsonArray arr = doc["reste"].to<JsonArray>();
    for (int g : reste) arr.add(g);
}

// Index des Rests, der am nächsten an ziel liegt
int naechster(const std::vector<int>& reste, int ziel) {
    int best = -1, bd = 1 << 30;
    for (size_t i = 0; i < reste.size(); i++) {
        int d = std::abs(reste[i] - ziel);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

bool lesen(const std::string& beschreibung, JsonDocument& doc) {
    if (beschreibung.empty()) { doc.to<JsonObject>(); return true; }
    DeserializationError e = deserializeJson(doc, beschreibung);
    if (e || !doc.is<JsonObject>()) return false;
    return true;
}

std::string schreiben(const JsonDocument& doc) {
    std::string s;
    serializeJson(doc, s);
    return s;
}

std::string grammtext(int g) { return std::to_string(g) + " g"; }

}  // namespace

uint32_t hex_zu_rgb(const std::string& hex, uint32_t standard) {
    if (hex.size() != 7 || hex[0] != '#') return standard;
    char* ende = nullptr;
    unsigned long v = strtoul(hex.c_str() + 1, &ende, 16);
    if (!ende || *ende) return standard;
    return (uint32_t)v;
}

std::string Filament::kurzname() const {
    std::string s = material;
    if (!farbe.empty()) s += (s.empty() ? "" : " ") + farbe;
    if (s.empty()) s = summary;
    return s;
}

int Filament::bestand_g() const {
    int voll = std::max(0, spulen - (int)reste.size());
    int sum = 0;
    for (int r : reste) sum += r;
    return voll * gewicht + sum;
}

bool Filament::hat_nfc(const std::string& tag) const {
    if (tag.empty()) return false;
    return std::find(nfc.begin(), nfc.end(), tag) != nfc.end();
}

void Lager::eintrag(const std::string& uid, const std::string& summary, const std::string& description) {
    JsonDocument doc;
    if (!lesen(description, doc)) doc.to<JsonObject>();
    // Merkzeilen und Einstellungen der Karte sind keine Sorten
    if (!doc["typ"].isNull()) return;

    Filament f;
    f.uid = uid;
    f.summary = summary;
    f.description = description;
    f.hersteller = doc["hersteller"] | "";
    f.material = doc["material"] | "";
    f.farbe = doc["farbe"] | "";
    if (f.material.empty() && f.farbe.empty()) f.farbe = summary;
    f.rgb = hex_zu_rgb(doc["hex"] | "");
    for (JsonVariantConst b : doc["besonderheiten"].as<JsonArrayConst>()) {
        const char* s = b.as<const char*>();
        if (s) f.besonderheiten.push_back(s);
    }
    f.gewicht = als_int(doc["gewicht"], 1000);
    if (f.gewicht <= 0) f.gewicht = 1000;
    f.spulen = std::max(0, als_int(doc["spulen"], 1));
    f.reste = reste_lesen(doc["reste"], f.gewicht);
    f.refills = std::max(0, std::min(als_int(doc["refills"], 0), f.spulen));
    f.duese_min = als_int(doc["duese_min"], -1);
    f.duese_max = als_int(doc["duese_max"], -1);
    f.bett_min = als_int(doc["bett_min"], -1);
    f.bett_max = als_int(doc["bett_max"], -1);
    f.trocknen_c = als_int(doc["trocknen_c"], -1);
    f.trocknen_h = als_int(doc["trocknen_h"], -1);
    f.leer_g = als_int(doc["leer_g"], -1);
    if (doc["durchmesser"].is<float>()) f.durchmesser = doc["durchmesser"].as<float>();
    f.fcode = doc["fcode"] | "";
    f.bambu_id = doc["bambu_id"] | "";
    f.ort = doc["ort"] | "";
    f.getrocknet = doc["getrocknet"] | "";
    for (JsonVariantConst c : doc["codes"].as<JsonArrayConst>()) {
        if (c.is<const char*>()) f.codes.push_back(c.as<const char*>());
    }
    for (JsonVariantConst c : doc["nfc"].as<JsonArrayConst>()) {
        if (c.is<const char*>()) f.nfc.push_back(c.as<const char*>());
    }
    sorten.push_back(std::move(f));
}

bool Lager::lade_service_antwort(const char* json, size_t len, const char* entity) {
    // Nur die benötigten Felder behalten, das spart viel Speicher
    JsonDocument filter;
    JsonObject item = filter["service_response"][entity]["items"][0].to<JsonObject>();
    item["uid"] = true;
    item["summary"] = true;
    item["description"] = true;
    item["status"] = true;

    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
    if (e) return false;
    JsonArrayConst items = doc["service_response"][entity]["items"].as<JsonArrayConst>();
    if (items.isNull()) return false;
    sorten.clear();
    sorten.reserve(items.size());
    for (JsonObjectConst it : items) {
        const char* status = it["status"] | "needs_action";
        if (std::string(status) == "completed") continue;
        eintrag(it["uid"] | "", it["summary"] | "", it["description"] | "");
    }
    return true;
}

int Lager::finde_nfc(const std::string& tag) const {
    if (tag.empty()) return -1;
    for (size_t i = 0; i < sorten.size(); i++)
        if (sorten[i].hat_nfc(tag)) return (int)i;
    return -1;
}

int Lager::finde_uid(const std::string& uid) const {
    for (size_t i = 0; i < sorten.size(); i++)
        if (sorten[i].uid == uid) return (int)i;
    return -1;
}

Aenderung rest_speichern(const std::string& beschreibung, int netto, const std::string& tag) {
    Aenderung a;
    JsonDocument doc;
    if (!lesen(beschreibung, doc)) { a.meldung = "Eintrag nicht lesbar"; return a; }

    int gewicht = als_int(doc["gewicht"], 1000);
    if (gewicht <= 0) gewicht = 1000;
    int spulen = std::max(0, als_int(doc["spulen"], 1));
    std::vector<int> reste = reste_lesen(doc["reste"], gewicht);
    int alt = -1;
    if (!tag.empty() && doc["waage"][tag].is<int>()) alt = doc["waage"][tag].as<int>();

    if (netto >= gewicht - VOLL_TOLERANZ_G) {
        if (alt > 0) {
            // diese Spule galt als angebrochen, ist jetzt wieder voll: Rest streichen
            int i = naechster(reste, alt);
            if (i >= 0) reste.erase(reste.begin() + i);
            doc["waage"].remove(tag);
            reste_schreiben(doc, reste);
            a.geaendert = true;
            a.meldung = "Spule ist voll – Rest gestrichen";
        } else {
            a.meldung = "Spule ist voll – nichts geändert";
            return a;
        }
    } else if (netto <= LEER_GRENZE_G) {
        int i = alt > 0 ? naechster(reste, alt) : -1;
        if (i < 0 && !reste.empty())
            i = (int)(std::min_element(reste.begin(), reste.end()) - reste.begin());
        if (i >= 0) reste.erase(reste.begin() + i);
        spulen = std::max(0, spulen - 1);
        doc["spulen"] = spulen;
        int refills = als_int(doc["refills"], 0);
        if (refills > spulen) doc["refills"] = spulen;
        if (!tag.empty()) doc["waage"].remove(tag);
        reste_schreiben(doc, reste);
        a.geaendert = true;
        a.meldung = "Spule leer – ausgebucht";
    } else {
        int i = -1;
        if (alt > 0) i = naechster(reste, alt);
        if (i < 0) {
            // unbekannte Spule: den kleinsten Rest nehmen, der noch größer ist
            int best = -1;
            for (size_t k = 0; k < reste.size(); k++)
                if (reste[k] >= netto - MESS_TOLERANZ_G && (best < 0 || reste[k] < reste[best])) best = (int)k;
            i = best;
        }
        if (i < 0 && (int)reste.size() < spulen) {
            reste.push_back(netto);  // eine volle Spule wurde angebrochen
            a.meldung = "Neue angebrochene Spule: " + grammtext(netto);
        } else {
            if (i < 0) i = naechster(reste, netto);
            if (i < 0) {
                // keine Spule mehr im Bestand, aber eine liegt auf der Waage
                spulen += 1;
                doc["spulen"] = spulen;
                reste.push_back(netto);
                a.meldung = "Spule ergänzt: " + grammtext(netto);
            } else {
                int vorher = reste[i];
                reste[i] = netto;
                a.meldung = "Rest " + grammtext(vorher) + " → " + grammtext(netto);
            }
        }
        if (!tag.empty()) {
            if (!doc["waage"].is<JsonObject>()) doc["waage"].to<JsonObject>();
            doc["waage"][tag] = netto;
        }
        reste_schreiben(doc, reste);
        a.geaendert = true;
    }
    if (doc["waage"].is<JsonObject>() && doc["waage"].size() == 0) doc.remove("waage");
    a.beschreibung = schreiben(doc);
    return a;
}

Aenderung tag_zuordnen(const std::string& beschreibung, const std::string& tag) {
    Aenderung a;
    JsonDocument doc;
    if (!lesen(beschreibung, doc) || tag.empty()) { a.meldung = "Eintrag nicht lesbar"; return a; }
    if (!doc["nfc"].is<JsonArray>()) doc["nfc"].to<JsonArray>();
    for (JsonVariantConst v : doc["nfc"].as<JsonArrayConst>()) {
        if (v.is<const char*>() && tag == v.as<const char*>()) {
            a.meldung = "Tag war schon zugeordnet";
            return a;
        }
    }
    doc["nfc"].add(tag);
    a.geaendert = true;
    a.beschreibung = schreiben(doc);
    a.meldung = "Tag gespeichert";
    return a;
}

Aenderung tag_entfernen(const std::string& beschreibung, const std::string& tag) {
    Aenderung a;
    JsonDocument doc;
    if (!lesen(beschreibung, doc)) { a.meldung = "Eintrag nicht lesbar"; return a; }
    JsonArray arr = doc["nfc"].as<JsonArray>();
    if (arr.isNull()) return a;
    for (size_t i = 0; i < arr.size(); i++) {
        if (arr[i].is<const char*>() && tag == arr[i].as<const char*>()) {
            arr.remove(i);
            a.geaendert = true;
            break;
        }
    }
    if (!a.geaendert) return a;
    if (arr.size() == 0) doc.remove("nfc");
    doc["waage"].remove(tag);
    if (doc["waage"].is<JsonObject>() && doc["waage"].size() == 0) doc.remove("waage");
    a.beschreibung = schreiben(doc);
    a.meldung = "Tag entfernt";
    return a;
}

Aenderung spulen_aendern(const std::string& beschreibung, int delta) {
    Aenderung a;
    JsonDocument doc;
    if (!lesen(beschreibung, doc)) { a.meldung = "Eintrag nicht lesbar"; return a; }
    int gewicht = als_int(doc["gewicht"], 1000);
    int spulen = std::max(0, als_int(doc["spulen"], 1));
    int neu = std::max(0, spulen + delta);
    if (neu == spulen) { a.meldung = "Keine Spule mehr da"; return a; }
    doc["spulen"] = neu;
    std::vector<int> reste = reste_lesen(doc["reste"], gewicht);
    if ((int)reste.size() > neu) {
        // die kleinste angebrochene Spule zuerst ausbuchen
        std::sort(reste.begin(), reste.end());
        reste.erase(reste.begin());
        reste_schreiben(doc, reste);
    }
    int refills = als_int(doc["refills"], 0);
    if (refills > neu) doc["refills"] = neu;
    a.geaendert = true;
    a.beschreibung = schreiben(doc);
    a.meldung = std::to_string(neu) + (neu == 1 ? " Spule" : " Spulen");
    return a;
}

Aenderung feld_setzen(const std::string& beschreibung, const std::string& feld, const std::string& wert) {
    Aenderung a;
    if (feld != "ort" && feld != "getrocknet") { a.meldung = "Unbekanntes Feld"; return a; }
    if (wert.size() > 60) { a.meldung = "Text zu lang"; return a; }
    if (feld == "getrocknet" && !wert.empty()) {
        bool ok = wert.size() == 10 && wert[4] == '-' && wert[7] == '-';
        for (size_t i = 0; ok && i < 10; i++) ok = (i == 4 || i == 7) || (wert[i] >= '0' && wert[i] <= '9');
        if (!ok) { a.meldung = "Datum ungültig"; return a; }
    }
    JsonDocument doc;
    if (!lesen(beschreibung, doc)) { a.meldung = "Eintrag nicht lesbar"; return a; }
    std::string alt = doc[feld] | "";
    if (alt == wert) { a.meldung = "Unverändert"; return a; }
    if (wert.empty()) doc.remove(feld);
    else doc[feld] = wert;
    a.geaendert = true;
    a.beschreibung = schreiben(doc);
    a.meldung = feld == "ort" ? (wert.empty() ? "Lagerort entfernt" : "Lagerort gespeichert")
                              : (wert.empty() ? "Trocknungsdatum entfernt" : "Als getrocknet vermerkt");
    return a;
}
