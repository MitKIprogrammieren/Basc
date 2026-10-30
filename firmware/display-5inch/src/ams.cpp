#include "ams.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

// Gleicher Text wie AMS_VORLAGE in pi/filamentwaage/pi_system.py
const char* const AMS_VORLAGE =
    "{% set ns = namespace(s=[], d=[]) %}"
    "{% for s in states.sensor if s.entity_id is search('_(ams_\\d+_slot_\\d+|externalspool\\d*_externe_spule)$')"
    " or s.entity_id is search('_ams_\\d+_(luftfeuchtigkeit|index_der_luftfeuchtigkeit)(_\\d+)?$') %}"
    "{% set a = s.attributes %}"
    "{% set ns.s = ns.s + [{'e': s.entity_id, 's': s.state, 'name': a.get('name'), 'color': a.get('color'),"
    " 'fid': a.get('filament_id'), 'empty': a.get('empty'), 'active': a.get('active'), 'remain': a.get('remain'),"
    " 'tag': a.get('tag_uid'), 'type': a.get('type')}] %}{% endfor %}"
    "{% for s in states.sensor if s.entity_id is search('^sensor\\.[^_]+_[^_]+_druckstatus$') %}"
    "{% set ns.d = ns.d + [{'p': s.entity_id[7:-12], 'name': device_attr(device_id(s.entity_id), 'name_by_user')"
    " or '', 'online': s.state not in ['unavailable', 'unknown']}] %}{% endfor %}"
    "{{ {'slots': ns.s, 'drucker': ns.d,"
    " 'abbuchung': states('automation.ernsthaft_gelagert_abbuchung_nach_druckende')} | to_json }}";

namespace {

bool nur_ziffern(const std::string& s, size_t a, size_t b) {
    if (a >= b) return false;
    for (size_t i = a; i < b; i++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

bool endet_mit(const std::string& s, const std::string& e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

// ^sensor\.(.+?)_ams_(\d+)_slot_(\d+)$
bool ist_slot(const std::string& e, std::string& praefix, int& nr, int& fach) {
    if (e.rfind("sensor.", 0) != 0) return false;
    std::string r = e.substr(7);
    for (size_t p = r.find("_ams_"); p != std::string::npos; p = r.find("_ams_", p + 1)) {
        if (p == 0) continue;
        size_t a = p + 5, s = r.find("_slot_", a);
        if (s == std::string::npos || !nur_ziffern(r, a, s) || !nur_ziffern(r, s + 6, r.size())) continue;
        praefix = r.substr(0, p);
        nr = atoi(r.c_str() + a);
        fach = atoi(r.c_str() + s + 6);
        return true;
    }
    return false;
}

// ^sensor\.(.+?)_externalspool(\d*)_externe_spule$
bool ist_extern(const std::string& e, std::string& praefix, std::string& nummer) {
    static const std::string ende = "_externe_spule";
    if (e.rfind("sensor.", 0) != 0 || !endet_mit(e, ende)) return false;
    std::string r = e.substr(7, e.size() - 7 - ende.size());
    for (size_t p = r.find("_externalspool"); p != std::string::npos; p = r.find("_externalspool", p + 1)) {
        if (p == 0) continue;
        size_t a = p + 14;
        if (a != r.size() && !nur_ziffern(r, a, r.size())) continue;
        praefix = r.substr(0, p);
        nummer = r.substr(a);
        return true;
    }
    return false;
}

// ^sensor\.(.+?)_ams_(\d+)_(luftfeuchtigkeit|index_der_luftfeuchtigkeit)(_\d+)?$
bool ist_feuchte(const std::string& e, std::string& praefix, int& nr, bool& prozent) {
    if (e.rfind("sensor.", 0) != 0) return false;
    std::string r = e.substr(7);
    for (size_t p = r.find("_ams_"); p != std::string::npos; p = r.find("_ams_", p + 1)) {
        if (p == 0) continue;
        size_t a = p + 5, u = r.find('_', a);
        if (u == std::string::npos || !nur_ziffern(r, a, u)) continue;
        std::string rest = r.substr(u + 1);
        for (const char* art : {"index_der_luftfeuchtigkeit", "luftfeuchtigkeit"}) {
            size_t n = strlen(art);
            if (rest.compare(0, n, art) != 0) continue;
            std::string ende = rest.substr(n);
            if (!ende.empty() && !(ende[0] == '_' && nur_ziffern(ende, 1, ende.size()))) continue;
            praefix = r.substr(0, p);
            nr = atoi(r.c_str() + a);
            prozent = strcmp(art, "luftfeuchtigkeit") == 0;
            return true;
        }
    }
    return false;
}

// Modell_Seriennummer – auch bei doppeltem Präfix wie „h2c_h2c_31b8…“
std::string drucker_id(const std::string& teil) {
    size_t b = teil.rfind('_');
    if (b == std::string::npos || b == 0) return teil;
    size_t a = teil.rfind('_', b - 1);
    return a == std::string::npos ? teil : teil.substr(a + 1);
}

std::string modell_name(const std::string& m) {
    static const std::map<std::string, std::string> M = {{"h2c", "H2C"}, {"h2s", "H2S"}, {"h2d", "H2D"}, {"x1c", "X1C"},
        {"x1e", "X1E"}, {"p1s", "P1S"}, {"p1p", "P1P"}, {"p2s", "P2S"}, {"a1", "A1"}, {"a1mini", "A1 mini"}};
    auto it = M.find(m);
    if (it != M.end()) return it->second;
    std::string g = m;
    for (auto& c : g) c = (char)toupper((unsigned char)c);
    return g;
}

std::string ams_kurz(int nr) {
    if (nr >= 128) return "HT" + std::to_string(nr - 127);
    if (nr >= 1 && nr <= 8) return std::string(1, "ABCDEFGH"[nr - 1]);
    return std::to_string(nr);
}

std::string klein(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string gross(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

// Wert als Zahl, auch wenn Home Assistant ihn als Text liefert ("45" oder "45.0")
bool zahl(JsonVariantConst v, double& aus) {
    if (v.is<double>() || v.is<long>()) {
        aus = v.as<double>();
        return true;
    }
    if (v.is<const char*>()) {
        const char* t = v.as<const char*>();
        char* ende = nullptr;
        aus = strtod(t, &ende);
        return ende && ende != t && *ende == 0;
    }
    return false;
}

std::string text(JsonVariantConst v) {
    if (v.is<const char*>()) return v.as<const char*>();
    if (v.isNull()) return "";
    std::string s;
    serializeJson(v, s);
    return s;
}

}  // namespace

bool ams_aufbereiten(const char* json, size_t laenge, AmsStand& aus) {
    JsonDocument doc;
    if (deserializeJson(doc, json, laenge)) return false;
    // Manche Wege liefern das Template-Ergebnis als String mit JSON darin
    if (doc.is<const char*>()) {
        std::string innen = doc.as<const char*>();
        JsonDocument d2;
        if (deserializeJson(d2, innen)) return false;
        doc = std::move(d2);
    }
    std::map<std::string, AmsDrucker> drucker;
    std::vector<std::string> reihenfolge;
    auto holen = [&](const std::string& id, const std::string& name, bool online) -> AmsDrucker& {
        auto it = drucker.find(id);
        if (it == drucker.end()) {
            reihenfolge.push_back(id);
            AmsDrucker d;
            d.id = id;
            d.name = name;
            d.online = online;
            it = drucker.emplace(id, d).first;
        }
        return it->second;
    };
    for (JsonVariantConst d : doc["drucker"].as<JsonArrayConst>()) {
        std::string p = d["p"] | "";
        AmsDrucker& x = holen(p, d["name"] | "", d["online"] | false);
        x.name = d["name"] | "";
        x.online = d["online"] | false;
    }
    // Namen ohne eigenen Namen: Modell, bei mehreren gleichen mit Nummer
    std::map<std::string, std::vector<std::string>> gleiche;
    {
        std::vector<std::string> ids;
        for (auto& kv : drucker) ids.push_back(kv.first);
        std::sort(ids.begin(), ids.end());
        for (auto& p : ids) gleiche[p.substr(0, p.find('_'))].push_back(p);
    }
    for (auto& kv : drucker) {
        AmsDrucker& d = kv.second;
        if (!d.name.empty()) continue;
        std::string m = kv.first.substr(0, kv.first.find('_'));
        auto& liste = gleiche[m];
        d.name = modell_name(m);
        if (liste.size() > 1) {
            size_t i = std::find(liste.begin(), liste.end(), kv.first) - liste.begin();
            d.name += " " + std::to_string(i + 1);
        }
    }

    // Luftfeuchte je AMS: Prozent vor Index (1–5)
    std::map<std::pair<std::string, int>, int> feuchte;
    for (JsonVariantConst s : doc["slots"].as<JsonArrayConst>()) {
        std::string e = s["e"] | "", praefix;
        int nr = 0;
        bool prozent = false;
        if (!ist_feuchte(e, praefix, nr, prozent)) continue;
        double v;
        if (!zahl(s["s"], v)) continue;
        auto k = std::make_pair(drucker_id(praefix), nr);
        if (prozent || feuchte.find(k) == feuchte.end()) feuchte[k] = (int)(v + (v >= 0 ? 0.5 : -0.5));
    }

    for (JsonVariantConst s : doc["slots"].as<JsonArrayConst>()) {
        std::string e = s["e"] | "", praefix, nummer;
        int nr = 0, fach = 0;
        bool slot = ist_slot(e, praefix, nr, fach);
        bool ext = !slot && ist_extern(e, praefix, nummer);
        if (!slot && !ext) continue;
        std::string p = drucker_id(praefix);
        std::string zustand = klein(text(s["s"]));
        bool leer = (s["empty"].is<bool>() && s["empty"].as<bool>()) || zustand == "empty" || zustand == "leer" ||
                    zustand == "unavailable" || zustand == "unknown" || zustand.empty() || zustand == "?";
        if (ext && leer) continue;  // leere externe Spulen nur zeigen, wenn etwas eingelegt ist
        AmsDrucker& d = holen(p, p, false);
        int schluessel = slot ? nr : 999;
        auto a = std::find_if(d.ams.begin(), d.ams.end(), [&](const AmsEinheit& x) { return x.nr == schluessel; });
        if (a == d.ams.end()) {
            AmsEinheit neu;
            neu.nr = schluessel;
            if (slot) {
                neu.kurz = ams_kurz(nr);
                neu.name = nr >= 128 ? "AMS HT " + std::to_string(nr - 127) : "AMS " + neu.kurz;
                auto f = feuchte.find(std::make_pair(p, nr));
                if (f != feuchte.end()) neu.feuchte = f->second;
            } else {
                neu.kurz = "Ext";
                neu.name = "Externe Spule";
            }
            d.ams.push_back(neu);
            a = d.ams.end() - 1;
        }
        AmsSlot sl;
        if (slot) {
            sl.slot_zahl = nr < 128;
            sl.slot = nr >= 128 ? "" : std::to_string(fach);
            sl.fach = fach;
        } else {
            sl.slot = nummer;
        }
        sl.leer = leer;
        std::string farbe = s["color"].is<const char*>() ? s["color"].as<const char*>() : "#888888";
        if (farbe.empty()) farbe = "#888888";
        sl.hex = farbe[0] == '#' ? farbe.substr(0, 7) : "#" + farbe.substr(0, 6);
        sl.rgb = hex_zu_rgb(sl.hex);
        sl.name = s["name"].is<const char*>() && *s["name"].as<const char*>() ? s["name"].as<const char*>()
                                                                               : (s["type"] | "");
        double r;
        if (!s["remain"].is<const char*>() && zahl(s["remain"], r)) sl.rest = (int)r;
        JsonVariantConst ak = s["active"];
        sl.aktiv = ak.is<bool>() ? ak.as<bool>() : ak.is<const char*>() ? *ak.as<const char*>() != 0 : (ak | 0.0) != 0.0;
        std::string tag = s["tag"] | "";
        sl.tag = tag.find_first_not_of('0') == std::string::npos ? "" : tag;
        sl.filament_id = s["fid"] | "";
        a->slots.push_back(sl);
    }

    aus.drucker.clear();
    std::vector<std::string> ids;
    for (auto& kv : drucker) ids.push_back(kv.first);
    std::stable_sort(ids.begin(), ids.end(),
                     [&](const std::string& x, const std::string& y) { return klein(drucker[x].name) < klein(drucker[y].name); });
    for (auto& id : ids) {
        AmsDrucker d = drucker[id];
        std::sort(d.ams.begin(), d.ams.end(), [](const AmsEinheit& x, const AmsEinheit& y) { return x.nr < y.nr; });
        for (auto& a : d.ams)
            std::stable_sort(a.slots.begin(), a.slots.end(), [](const AmsSlot& x, const AmsSlot& y) { return x.fach < y.fach; });
        aus.drucker.push_back(std::move(d));
    }
    aus.abbuchung_aktiv = text(doc["abbuchung"]) == "on";
    aus.geladen = true;
    return true;
}

void ams_als_json(const AmsStand& s, JsonObject ziel) {
    JsonArray dr = ziel["drucker"].to<JsonArray>();
    for (const auto& d : s.drucker) {
        JsonObject o = dr.add<JsonObject>();
        o["id"] = d.id;
        o["name"] = d.name;
        o["online"] = d.online;
        JsonArray ams = o["ams"].to<JsonArray>();
        for (const auto& a : d.ams) {
            JsonObject ao = ams.add<JsonObject>();
            ao["name"] = a.name;
            ao["kurz"] = a.kurz;
            if (a.feuchte >= 0) ao["feuchte"] = a.feuchte;
            else ao["feuchte"] = nullptr;
            JsonArray sl = ao["slots"].to<JsonArray>();
            for (const auto& x : a.slots) {
                JsonObject so = sl.add<JsonObject>();
                if (x.slot_zahl) so["slot"] = atoi(x.slot.c_str());
                else so["slot"] = x.slot;
                so["leer"] = x.leer;
                so["hex"] = x.hex;
                so["name"] = x.name;
                so["rest"] = x.rest;
                so["aktiv"] = x.aktiv;
                so["tag"] = x.tag;
                so["filament_id"] = x.filament_id;
            }
        }
    }
}

AmsTreffer ams_slot_fuer_tag(const AmsStand& s, const std::string& tag, const std::vector<std::string>& nur_drucker) {
    AmsTreffer t;
    if (tag.empty()) return t;
    std::string kopf = gross(tag.substr(0, 8));
    for (const auto& d : s.drucker) {
        if (!nur_drucker.empty() && std::find(nur_drucker.begin(), nur_drucker.end(), d.id) == nur_drucker.end()) continue;
        for (const auto& a : d.ams)
            for (const auto& sl : a.slots)
                if (!sl.tag.empty() && gross(sl.tag).compare(0, kopf.size(), kopf) == 0) {
                    t.drucker = &d;
                    t.ams = &a;
                    t.slot = &sl;
                    return t;
                }
    }
    return t;
}

static long farb_abstand(uint32_t a, uint32_t b) {
    long s = 0;
    for (int i = 0; i < 3; i++) {
        long d = (long)((a >> (8 * i)) & 0xFF) - (long)((b >> (8 * i)) & 0xFF);
        s += d * d;
    }
    return s;
}

int ams_vorschlag(const Lager& lager, const AmsSlot& slot) {
    if (slot.filament_id.empty()) return -1;
    int best = -1;
    long abstand = 0;
    for (size_t i = 0; i < lager.sorten.size(); i++) {
        const Filament& f = lager.sorten[i];
        if (f.bambu_id != slot.filament_id) continue;
        long a = farb_abstand(f.rgb, slot.rgb);
        if (best < 0 || a < abstand) {
            best = (int)i;
            abstand = a;
        }
    }
    return best >= 0 && abstand < 19200 ? best : -1;
}

std::string ams_slot_kurz(const AmsEinheit& a, const AmsSlot& s) { return a.kurz + s.slot; }
