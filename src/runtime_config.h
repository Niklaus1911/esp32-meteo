#pragma once

#include <cstddef>
#include <cstdint>

#include "battery_curve.h"

namespace Esp32Meteo {

constexpr uint16_t kRuntimeConfigSchemaVersion = 1;
constexpr size_t kRuntimeConfigMqttHostMaxLength = 63;
constexpr size_t kRuntimeConfigMqttUsernameMaxLength = 63;
constexpr size_t kRuntimeConfigMqttPasswordMaxLength = 95;
constexpr size_t kRuntimeConfigOtaPasswordMaxLength = 63;
constexpr size_t kRuntimeConfigIpv4MaxLength = 15;
constexpr uint16_t kRuntimeConfigDefaultMqttPort = 1883;
constexpr const char* kRuntimeConfigDefaultSubnet = "255.255.255.0";
constexpr uint8_t kRuntimeBatteryChemistryLiIon = kBatteryCurveChemistryLiIon;
constexpr uint8_t kRuntimeBatteryChemistryLiFePO4 = kBatteryCurveChemistryLiFePO4;

struct RuntimeConfig {
  uint16_t schemaVersion = kRuntimeConfigSchemaVersion;
  char mqttHost[kRuntimeConfigMqttHostMaxLength + 1] = "";
  uint16_t mqttPort = kRuntimeConfigDefaultMqttPort;
  char mqttUsername[kRuntimeConfigMqttUsernameMaxLength + 1] = "";
  char mqttPassword[kRuntimeConfigMqttPasswordMaxLength + 1] = "";
  char otaPassword[kRuntimeConfigOtaPasswordMaxLength + 1] = "";
  uint8_t batteryChemistryId = kRuntimeBatteryChemistryLiIon;
  bool hasStaticIp = false;
  char staticIp[kRuntimeConfigIpv4MaxLength + 1] = "";
  char gateway[kRuntimeConfigIpv4MaxLength + 1] = "";
  char subnet[kRuntimeConfigIpv4MaxLength + 1] = "";
};

struct RuntimeConfigValidation {
  bool valid;
  const char* reason;
};

bool isValidMqttPort(uint32_t port);
bool isValidIpv4Address(const char* value);
bool isStaticIpConfigValid(const char* staticIp, const char* gateway, const char* subnet);
bool isValidBatteryChemistryId(uint8_t chemistryId);
bool parseBatteryChemistry(const char* value, uint8_t& chemistryId);
const char* batteryChemistryKey(uint8_t chemistryId);
const char* batteryChemistryName(uint8_t chemistryId);
bool populateRuntimeConfig(RuntimeConfig& config,
                           const char* mqttHost,
                           uint32_t mqttPort,
                           const char* mqttUsername,
                           const char* mqttPassword,
                           const char* otaPassword,
                           const char* batteryChemistry,
                           const char* staticIp,
                           const char* gateway,
                           const char* subnet);
RuntimeConfigValidation validateRuntimeConfig(const RuntimeConfig& config);
RuntimeConfig normalizedRuntimeConfig(const RuntimeConfig& config);

#if defined(ARDUINO)
const RuntimeConfig& runtimeConfig();
bool loadRuntimeConfig();
bool saveRuntimeConfig(const RuntimeConfig& config);
bool clearRuntimeConfig();
#endif

}  // namespace Esp32Meteo
