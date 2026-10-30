// AMS-Aufbereitung gegen die Python-Version prüfen: gibt das Ergebnis als JSON aus (build.sh vergleicht).
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../../src/ams.h"

static std::string lesen(const char* p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    std::string a = lesen(argv[1]), l = lesen(argv[2]);
    AmsStand s;
    assert(ams_aufbereiten(a.c_str(), a.size(), s));
    Lager lager;
    assert(lager.lade_service_antwort(l.c_str(), l.size(), "todo.filament"));
    // Tag der Blue Gray (NTAG-UID, 7 Byte) steckt im X1C, AMS A, Slot 1
    AmsTreffer t = ams_slot_fuer_tag(s, "04C0FFEE112233");
    assert(t && t.drucker->name == "X1C" && ams_slot_kurz(*t.ams, *t.slot) == "A1" && t.slot->rest == 56);
    // Unbekannter Tag in Slot 2 → Vorschlag Mistletoe Green (GFA00, ähnliche Farbe)
    AmsTreffer u = ams_slot_fuer_tag(s, "04DEADBEEF0001");
    assert(u);
    int v = ams_vorschlag(lager, *u.slot);
    assert(v >= 0 && lager.sorten[v].kurzname().find("Mistletoe") != std::string::npos);
    // Auswahl der Drucker beachten
    assert(!ams_slot_fuer_tag(s, "04C0FFEE112233", {"h2d_0948bd5a2000"}));
    assert(!ams_slot_fuer_tag(s, ""));
    JsonDocument d;
    ams_als_json(s, d.to<JsonObject>());
    d["abbuchung_aktiv"] = s.abbuchung_aktiv;
    serializeJson(d, std::cout);
    std::cout << "\n";
    fprintf(stderr, "AMS: alle Tests bestanden\n");
    return 0;
}
