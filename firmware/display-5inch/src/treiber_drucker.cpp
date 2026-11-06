// drucker.cpp aus der Sensorplatinen-Firmware (Etikettendruck), nur für env:sc01plus_direkt.
#include "config.h"
#if SENSOR_DIREKT
#include <HTTPClient.h>
#include <NimBLEDevice.h>  // für PlatformIO, siehe treiber_oberflaeche.cpp

#include "../../sensor-s3zero/src/drucker.cpp"
#endif
