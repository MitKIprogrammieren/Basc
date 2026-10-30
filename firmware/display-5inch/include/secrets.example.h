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

// Optional: Sensorplatine per WLAN statt RS485-Kabel (IP aus der FritzBox).
// Die Platine bedient immer nur eine WLAN-Gegenstelle – Pad 7 oder Pi dann nicht gleichzeitig per WLAN verbinden.
// #define SENSOR_HOST "192.168.1.96"
