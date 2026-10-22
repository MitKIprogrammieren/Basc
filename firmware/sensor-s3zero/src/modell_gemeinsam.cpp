// Datenmodell und Home-Assistant-Anbindung teilt sich die Sensorplatine mit der 5"-Firmware.
// Eine Quelle, damit die Regeln (Reste, Spulen, NFC) überall gleich sind.
// Die Bibliotheken hier direkt nennen, damit PlatformIO sie findet (die Quellen liegen außerhalb von src/).
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../../display-5inch/src/model.cpp"
#include "../../display-5inch/src/ha_client.cpp"
