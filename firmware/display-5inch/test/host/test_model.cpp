// Host-Test für das Datenmodell: g++ -std=c++17 -I<ArduinoJson>/src ...
#include <cassert>
#include <cstdio>
#include <string>

#include <ArduinoJson.h>

#include "../../src/model.h"

static int fehler = 0;
#define PRUEFE(bed)                                                     \
    do {                                                                \
        if (!(bed)) {                                                   \
            printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #bed);     \
            fehler++;                                                   \
        }                                                               \
    } while (0)

static JsonDocument json(const std::string& s) {
    JsonDocument d;
    deserializeJson(d, s);
    return d;
}

// echte Einträge aus todo.filament
static const char* BLUE_GRAY =
    R"({"v":2,"hersteller":"Bambu Lab","material":"PLA","farbe":"Blue Gray","hex":"#5b6579",)"
    R"("besonderheiten":["Basic"],"gewicht":1000,"spulen":5,"refills":0,"reste":[560],"codes":[],)"
    R"("codes_refill":[],"durchmesser":1.75,"ausfuehrung":"spule","duese_min":null,"duese_max":null,)"
    R"("bett_min":null,"bett_max":null,"trocknen_c":null,"trocknen_h":null,"fcode":"","bambu_id":"GFA00"})";
static const char* ASH_GRAY =
    R"({"v":2,"hersteller":"Bambu Lab","material":"PLA","farbe":"Ash Gray","hex":"#9b9ea0",)"
    R"("besonderheiten":["Matt"],"gewicht":1000,"spulen":4,"refills":1,"reste":[],)"
    R"("codes":["6975337030058","6975337031352"],"codes_refill":["6975337030058"],"durchmesser":1.75,)"
    R"("ausfuehrung":"spule","duese_min":190,"duese_max":230,"bett_min":null,"bett_max":null,)"
    R"("trocknen_c":55,"trocknen_h":8,"fcode":"11102","bambu_id":"GFA01"})";

int main() {
    // Einlesen der Service-Antwort
    std::string antwort;
    {
        JsonDocument d;
        JsonArray items = d["service_response"]["todo.filament"]["items"].to<JsonArray>();
        JsonObject a = items.add<JsonObject>();
        a["summary"] = "Bambu Lab PLA Blue Gray (Basic)";
        a["uid"] = "u1";
        a["status"] = "needs_action";
        a["description"] = BLUE_GRAY;
        JsonObject b = items.add<JsonObject>();
        b["summary"] = "Bambu Lab PLA Ash Gray (Matt)";
        b["uid"] = "u2";
        b["status"] = "needs_action";
        b["description"] = ASH_GRAY;
        JsonObject c = items.add<JsonObject>();
        c["summary"] = "⚙ Einstellungen";
        c["uid"] = "u3";
        c["status"] = "needs_action";
        c["description"] = R"({"typ":"einstellungen_plus","v":1})";
        antwort.clear();
        serializeJson(d, antwort);
    }
    Lager lager;
    PRUEFE(lager.lade_service_antwort(antwort.c_str(), antwort.size(), "todo.filament"));
    PRUEFE(lager.sorten.size() == 2);  // Einstellungen ist keine Sorte
    const Filament& bg = lager.sorten[0];
    PRUEFE(bg.kurzname() == "PLA Blue Gray");
    PRUEFE(bg.rgb == 0x5b6579);
    PRUEFE(bg.spulen == 5 && bg.reste.size() == 1 && bg.reste[0] == 560);
    PRUEFE(bg.bestand_g() == 4 * 1000 + 560);
    PRUEFE(lager.sorten[1].duese_min == 190 && lager.sorten[1].refills == 1);
    PRUEFE(lager.sorten[1].trocknen_c == 55);

    // unbekannte Spule ohne Tag, 540 g: ersetzt den nächstgrößeren Rest 560
    Aenderung a = rest_speichern(BLUE_GRAY, 540, "");
    PRUEFE(a.geaendert);
    JsonDocument d = json(a.beschreibung);
    PRUEFE(d["reste"].size() == 1 && d["reste"][0] == 540);
    PRUEFE(d["spulen"] == 5);
    PRUEFE(d["bambu_id"] == "GFA00");             // Fremdfelder bleiben erhalten
    PRUEFE(d["duese_min"].isNull());               // null bleibt null
    PRUEFE(d["waage"].isNull());

    // unbekannte Spule mit Tag, 800 g: neue angebrochene Spule (800 > 560)
    a = rest_speichern(BLUE_GRAY, 800, "04AA");
    d = json(a.beschreibung);
    PRUEFE(d["reste"].size() == 2 && d["reste"][1] == 800);
    PRUEFE(d["waage"]["04AA"] == 800);

    // dieselbe Spule nochmal gewogen: ersetzt genau ihren Rest
    a = rest_speichern(a.beschreibung, 640, "04AA");
    d = json(a.beschreibung);
    PRUEFE(d["reste"].size() == 2 && d["reste"][0] == 560 && d["reste"][1] == 640);
    PRUEFE(d["waage"]["04AA"] == 640);

    // leer: Spule ausgebucht, ihr Rest verschwindet
    a = rest_speichern(a.beschreibung, 3, "04AA");
    d = json(a.beschreibung);
    PRUEFE(d["spulen"] == 4);
    PRUEFE(d["reste"].size() == 1 && d["reste"][0] == 560);
    PRUEFE(d["waage"].isNull());

    // voll und unbekannt: nichts ändern
    a = rest_speichern(ASH_GRAY, 995, "");
    PRUEFE(!a.geaendert);

    // leer bei refills: refills wird gekappt
    std::string eine = R"({"gewicht":1000,"spulen":1,"refills":1,"reste":[]})";
    a = rest_speichern(eine, 0, "");
    d = json(a.beschreibung);
    PRUEFE(d["spulen"] == 0 && d["refills"] == 0);

    // Spule auf der Waage, Bestand 0: wird ergänzt
    std::string null_bestand = R"({"gewicht":1000,"spulen":0,"reste":[]})";
    a = rest_speichern(null_bestand, 400, "");
    d = json(a.beschreibung);
    PRUEFE(d["spulen"] == 1 && d["reste"][0] == 400);

    // Tag zuordnen / entfernen
    a = tag_zuordnen(ASH_GRAY, "04BB");
    PRUEFE(a.geaendert);
    lager.sorten[1].description = a.beschreibung;
    Lager l2;
    l2.eintrag("u2", "x", a.beschreibung);
    PRUEFE(l2.finde_nfc("04BB") == 0);
    PRUEFE(!tag_zuordnen(a.beschreibung, "04BB").geaendert);
    a = tag_entfernen(a.beschreibung, "04BB");
    PRUEFE(a.geaendert && json(a.beschreibung)["nfc"].isNull());

    // Spulen ändern
    a = spulen_aendern(BLUE_GRAY, -1);
    PRUEFE(json(a.beschreibung)["spulen"] == 4);
    a = spulen_aendern(R"({"spulen":1,"reste":[300]})", -1);
    d = json(a.beschreibung);
    PRUEFE(d["spulen"] == 0 && d["reste"].size() == 0);
    PRUEFE(!spulen_aendern(R"({"spulen":0})", -1).geaendert);

    printf(fehler ? "%d Fehler\n" : "Modell: alle Tests bestanden\n", fehler);
    return fehler ? 1 : 0;
}
