// Home Assistant über die REST-API: Lager lesen und Einträge ändern.
#pragma once

#include <string>

#include "model.h"

class HaClient {
public:
    void begin(const char* url, const char* token, const char* todo);
    // todo.get_items mit return_response
    bool lager_laden(Lager& lager, std::string& fehler);
    // todo.update_item: nur die Beschreibung ersetzen
    bool beschreibung_setzen(const std::string& item_uid, const std::string& beschreibung, std::string& fehler);
    // POST /api/states/<entity>: Zustand eines eigenen Sensors setzen (body: {"state":…,"attributes":{…}})
    bool zustand_setzen(const std::string& entity, const std::string& body, std::string& fehler);
    // POST /api/events/<name>: Ereignis auslösen (für Automationen)
    bool ereignis_senden(const std::string& name, const std::string& body, std::string& fehler);
    // todo.get_items für eine beliebige Liste, Rohantwort (z. B. todo.filament_verbrauch)
    bool liste_laden(const std::string& entity, std::string& antwort, std::string& fehler);

private:
    bool post(const std::string& pfad, const std::string& body, std::string& antwort, std::string& fehler);
    std::string url_, token_, todo_;
};
