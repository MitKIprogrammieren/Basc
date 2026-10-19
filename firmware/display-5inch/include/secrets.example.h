// Kopieren nach include/secrets.h und ausfüllen. secrets.h steht in .gitignore.
#pragma once

#define WLAN_SSID "MeinWLAN"
#define WLAN_PASS "geheim"

// Home Assistant: lokale Adresse, ohne / am Ende.
// https geht auch, dann wird das Zertifikat nicht geprüft (HA_HTTPS_UNGEPRUEFT).
#define HA_URL "http://homeassistant.local:8123"
// Langlebiges Zugriffstoken: HA → Profil → Sicherheit → Langlebige Zugriffstokens
#define HA_TOKEN "eyJ..."
#define HA_TODO "todo.filament"
