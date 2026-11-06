// Treiber PN532 im HSU-Modus aus der Sensorplatinen-Firmware, nur für env:sc01plus_direkt.
// Eigene Datei je Treiber, damit sich deren interne Namen nicht in die Quere kommen.
#include "config.h"
#if SENSOR_DIREKT
#include "../../sensor-s3zero/src/pn532_hsu.cpp"
#endif
