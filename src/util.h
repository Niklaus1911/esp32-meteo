#pragma once

#include <Arduino.h>
#include <esp_sleep.h>

namespace Esp32Meteo {

void logPhase(const char* phase);
const char* yesNo(bool value);
bool formatInto(char* buffer, size_t bufferSize, const char* description, const char* format, ...);
bool appendChecked(char* destination, size_t destinationSize, const char* description, const char* value);
String topic(const char* suffix);
bool payloadEquals(const byte* payload, unsigned int length, const char* expected);
const char* resetReasonName(esp_reset_reason_t reason);
const char* wakeupCauseName(esp_sleep_wakeup_cause_t cause);

}  // namespace Esp32Meteo
