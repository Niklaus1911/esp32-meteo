#include "util.h"

#include <cstdarg>

#include "config.h"

namespace Esp32Meteo {

void logPhase(const char* phase) {
  Serial.println();
  Serial.printf("%s== %s ==%s\n", serialStyle(SerialStyle::Phase), phase, serialReset());
}

const char* yesNo(bool value) {
  return value ? "yes" : "no";
}

const char* serialStyle(SerialStyle style) {
  return serialStyleCode(style, kSerialAnsiColors);
}

const char* serialReset() {
  return serialResetCode(kSerialAnsiColors);
}

const char* serialResult(bool ok) {
  return serialStyle(serialResultStyle(ok));
}

const char* serialReady(bool ready) {
  return serialStyle(serialReadyStyle(ready));
}

const char* serialOkFailed(bool ok) {
  return serialOkFailedText(ok, kSerialAnsiColors);
}

const char* serialCompleteFailed(bool ok) {
  return serialCompleteFailedText(ok, kSerialAnsiColors);
}

const char* serialYesNo(bool value) {
  return serialYesNoText(value, kSerialAnsiColors);
}

const char* serialTrueFalse(bool value) {
  return serialTrueFalseText(value, kSerialAnsiColors);
}

const char* serialEnabledDisabled(bool value) {
  return serialEnabledDisabledText(value, kSerialAnsiColors);
}

const char* serialPresentMissing(bool present) {
  return serialPresentMissingText(present, kSerialAnsiColors);
}

const char* serialReadyMissing(bool ready) {
  return serialReadyMissingText(ready, kSerialAnsiColors);
}

bool formatInto(char* buffer, size_t bufferSize, const char* description, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(buffer, bufferSize, format, args);
  va_end(args);

  if (written < 0) {
    Serial.printf("Formatting failed for %s\n", description);
    return false;
  }
  if (static_cast<size_t>(written) >= bufferSize) {
    Serial.printf("Formatting truncated for %s: needed %u bytes, buffer has %u bytes\n",
                  description,
                  static_cast<unsigned int>(written + 1),
                  static_cast<unsigned int>(bufferSize));
    return false;
  }
  return true;
}

bool appendChecked(char* destination, size_t destinationSize, const char* description, const char* value) {
  const size_t used = strlen(destination);
  const size_t valueLength = strlen(value);
  if (used + valueLength >= destinationSize) {
    Serial.printf("Formatting truncated for %s: needed %u bytes, buffer has %u bytes\n",
                  description,
                  static_cast<unsigned int>(used + valueLength + 1),
                  static_cast<unsigned int>(destinationSize));
    return false;
  }

  strlcat(destination, value, destinationSize);
  return true;
}

String topic(const char* suffix) {
  String full(kTopicPrefix);
  full += suffix;
  return full;
}

bool payloadEquals(const byte* payload, unsigned int length, const char* expected) {
  if (!expected) {
    return false;
  }
  const size_t expectedLength = strlen(expected);
  if (length != expectedLength) {
    return false;
  }
  for (unsigned int i = 0; i < length; ++i) {
    if (payload[i] != static_cast<byte>(expected[i])) {
      return false;
    }
  }
  return true;
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "unknown";
  }
}

const char* wakeupCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "undefined";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "external ext0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "external ext1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ulp";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "gpio";
    case ESP_SLEEP_WAKEUP_UART:
      return "uart";
    case ESP_SLEEP_WAKEUP_WIFI:
      return "wifi";
    case ESP_SLEEP_WAKEUP_COCPU:
      return "coprocessor";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
      return "coprocessor trap";
    case ESP_SLEEP_WAKEUP_BT:
      return "bluetooth";
    default:
      return "unknown";
  }
}

}  // namespace Esp32Meteo
