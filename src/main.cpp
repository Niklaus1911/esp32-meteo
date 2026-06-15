#include <Arduino.h>

#include "app.h"

void setup() {
  Esp32Meteo::appSetup();
}

void loop() {
  Esp32Meteo::appLoop();
}
