#include "util.h"

#include <cstdarg>

#include "config.h"

namespace Esp32Meteo {

void logPhase(const char* phase) {
  Serial.println();
  Serial.printf("== %s ==\n", phase);
}

const char* yesNo(bool value) {
  return value ? "yes" : "no";
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

}  // namespace Esp32Meteo
