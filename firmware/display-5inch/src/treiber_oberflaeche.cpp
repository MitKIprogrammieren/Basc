// oberflaeche.cpp aus der Sensorplatinen-Firmware (Web-Oberfläche), nur für env:sc01plus_direkt.
#include "config.h"
#if SENSOR_DIREKT && DIREKT_WEB
// Die Bibliotheken hier noch einmal nennen: PlatformIO sucht sie nur in Dateien unter src/,
// nicht in der eingebundenen oberflaeche.cpp.
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "../../sensor-s3zero/src/oberflaeche.cpp"
#endif
