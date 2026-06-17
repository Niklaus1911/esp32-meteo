#pragma once

#include <cstddef>
#include <cstdint>

namespace Esp32Meteo {

struct SensorReadiness {
  bool ready;
  const char* issue;
};

bool parseStayAwakePayload(const char* payload, size_t length, bool& value);
bool parseResetCredentialsPayload(const char* payload, size_t length);
const char* readinessText(bool ready, const char* issue);
bool formatSensorReadiness(char* buffer,
                           size_t bufferSize,
                           SensorReadiness solar,
                           SensorReadiness battery,
                           SensorReadiness sht4x,
                           SensorReadiness bmp3xx);
bool formatDeviceStatus(char* buffer,
                        size_t bufferSize,
                        const char* solarIssue,
                        const char* batteryIssue,
                        const char* sht4xIssue,
                        const char* bmp3xxIssue);
bool mqttPacketFits(const char* topic, size_t payloadLength, size_t bufferSize, size_t maxHeaderBytes);

}  // namespace Esp32Meteo
