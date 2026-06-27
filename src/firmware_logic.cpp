#include "firmware_logic.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace Esp32Meteo {

namespace {

bool appendText(char* buffer, size_t bufferSize, const char* value) {
  if (!buffer || bufferSize == 0 || !value) {
    return false;
  }

  const size_t used = strlen(buffer);
  const size_t valueLength = strlen(value);
  if (used + valueLength >= bufferSize) {
    return false;
  }

  memcpy(buffer + used, value, valueLength + 1);
  return true;
}

bool equals(const char* left, const char* right) {
  return strcmp(left, right) == 0;
}

bool appendIssue(char* buffer, size_t bufferSize, bool& degraded, const char* issue) {
  if (!issue || !issue[0]) {
    return true;
  }

  if (!degraded) {
    if (!appendText(buffer, bufferSize, "; degraded:")) {
      return false;
    }
    degraded = true;
  }

  return appendText(buffer, bufferSize, " ") && appendText(buffer, bufferSize, issue);
}

}  // namespace

bool parseStayAwakePayload(const char* payload, size_t length, bool& value) {
  if (!payload) {
    return false;
  }

  char normalized[6] = "";
  size_t normalizedLength = 0;

  for (size_t i = 0; i < length; ++i) {
    const unsigned char input = static_cast<unsigned char>(payload[i]);
    if (std::isspace(input)) {
      continue;
    }

    if (normalizedLength + 1 >= sizeof(normalized)) {
      return false;
    }

    normalized[normalizedLength++] = static_cast<char>(std::tolower(input));
  }
  normalized[normalizedLength] = '\0';

  if (equals(normalized, "true") || equals(normalized, "on") || equals(normalized, "1") ||
      equals(normalized, "yes")) {
    value = true;
    return true;
  }

  if (equals(normalized, "false") || equals(normalized, "off") || equals(normalized, "0") ||
      equals(normalized, "no")) {
    value = false;
    return true;
  }

  return false;
}

bool parseResetCredentialsPayload(const char* payload, size_t length) {
  constexpr const char kExpectedPayload[] = "reset";
  constexpr size_t kExpectedLength = sizeof(kExpectedPayload) - 1;
  return payload && length == kExpectedLength && memcmp(payload, kExpectedPayload, kExpectedLength) == 0;
}

const char* readinessText(bool ready, const char* issue) {
  if (ready) {
    return "ready";
  }
  if (issue && issue[0]) {
    return issue;
  }
  return "unknown";
}

bool formatSensorReadiness(char* buffer,
                           size_t bufferSize,
                           SensorReadiness solar,
                           SensorReadiness battery,
                           SensorReadiness sht4x,
                           SensorReadiness bmp3xx) {
  if (!buffer || bufferSize == 0) {
    return false;
  }

  const int written = snprintf(buffer,
                               bufferSize,
                               "solar=%s battery=%s sht4x=%s bmp3xx=%s",
                               readinessText(solar.ready, solar.issue),
                               readinessText(battery.ready, battery.issue),
                               readinessText(sht4x.ready, sht4x.issue),
                               readinessText(bmp3xx.ready, bmp3xx.issue));
  return written >= 0 && static_cast<size_t>(written) < bufferSize;
}

bool formatDeviceStatus(char* buffer,
                        size_t bufferSize,
                        const char* solarIssue,
                        const char* batteryIssue,
                        const char* sht4xIssue,
                        const char* bmp3xxIssue) {
  if (!buffer || bufferSize == 0) {
    return false;
  }

  buffer[0] = '\0';
  bool degraded = false;
  return appendText(buffer, bufferSize, "online") &&
         appendIssue(buffer, bufferSize, degraded, solarIssue) &&
         appendIssue(buffer, bufferSize, degraded, batteryIssue) &&
         appendIssue(buffer, bufferSize, degraded, sht4xIssue) &&
         appendIssue(buffer, bufferSize, degraded, bmp3xxIssue);
}

bool mqttPacketFits(const char* topic, size_t payloadLength, size_t bufferSize, size_t maxHeaderBytes) {
  if (!topic) {
    return false;
  }

  const size_t packetBytes = maxHeaderBytes + 2 + strlen(topic) + payloadLength;
  return packetBytes <= bufferSize;
}

bool shouldPublishRoutineDiscovery(bool deepSleepWake, bool stayAwakeRequested) {
  (void)deepSleepWake;
  return stayAwakeRequested;
}

}  // namespace Esp32Meteo
