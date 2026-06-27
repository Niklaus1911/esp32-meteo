#pragma once

#include <Arduino.h>
#include <esp_sleep.h>

#include "serial_style.h"

namespace Esp32Meteo {

void logPhase(const char* phase);
const char* yesNo(bool value);
const char* serialStyle(SerialStyle style);
const char* serialReset();
const char* serialResult(bool ok);
const char* serialReady(bool ready);
const char* serialOkFailed(bool ok);
const char* serialCompleteFailed(bool ok);
const char* serialYesNo(bool value);
const char* serialTrueFalse(bool value);
const char* serialEnabledDisabled(bool value);
const char* serialPresentMissing(bool present);
const char* serialReadyMissing(bool ready);
bool formatInto(char* buffer, size_t bufferSize, const char* description, const char* format, ...);
bool appendChecked(char* destination, size_t destinationSize, const char* description, const char* value);
String topic(const char* suffix);
bool payloadEquals(const byte* payload, unsigned int length, const char* expected);
const char* resetReasonName(esp_reset_reason_t reason);
const char* wakeupCauseName(esp_sleep_wakeup_cause_t cause);

}  // namespace Esp32Meteo
