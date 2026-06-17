#include "runtime_config.h"

#include <cctype>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#include <Preferences.h>
#endif

namespace Esp32Meteo {

namespace {

constexpr const char* kRuntimeConfigNamespace = "esp32_meteo";

#if defined(ARDUINO)
RuntimeConfig currentConfig;
#endif

bool isBlank(const char* value) {
  return !value || value[0] == '\0';
}

bool copyTrimmed(char* destination, size_t destinationSize, const char* source) {
  if (destinationSize == 0) {
    return false;
  }

  if (!source) {
    destination[0] = '\0';
    return true;
  }

  const char* begin = source;
  while (*begin && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }

  const char* end = begin + std::strlen(begin);
  while (end > begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  const size_t length = static_cast<size_t>(end - begin);
  if (length >= destinationSize) {
    destination[0] = '\0';
    return false;
  }

  std::memcpy(destination, begin, length);
  destination[length] = '\0';
  return true;
}

bool equalsIgnoreCase(const char* left, const char* right) {
  if (!left || !right) {
    return false;
  }

  while (*left && *right) {
    const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(*left)));
    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(*right)));
    if (a != b) {
      return false;
    }
    ++left;
    ++right;
  }

  return *left == '\0' && *right == '\0';
}

#if defined(ARDUINO)
bool readPreferenceString(Preferences& preferences, const char* key, char* destination, size_t destinationSize) {
  const String value = preferences.getString(key, "");
  return copyTrimmed(destination, destinationSize, value.c_str());
}

bool writePreferenceString(Preferences& preferences, const char* key, const char* value) {
  const char* safeValue = value ? value : "";
  return preferences.putString(key, safeValue) == strlen(safeValue);
}

void logRuntimeConfigSummary(const RuntimeConfig& config) {
  Serial.printf("Runtime config: MQTT %s:%u, MQTT auth=%s, OTA password=%s\n",
                config.mqttHost,
                config.mqttPort,
                config.mqttUsername[0] || config.mqttPassword[0] ? "configured" : "anonymous",
                config.otaPassword[0] ? "configured" : "missing");
  Serial.printf("Runtime config: battery chemistry %s (%s)\n",
                batteryChemistryName(config.batteryChemistryId),
                batteryChemistryKey(config.batteryChemistryId));
  if (config.hasStaticIp) {
    Serial.printf("Runtime config: static IP %s, gateway %s, subnet %s\n",
                  config.staticIp,
                  config.gateway,
                  config.subnet);
  } else {
    Serial.println("Runtime config: static IP disabled; DHCP will be used");
  }
}
#endif

}  // namespace

bool isValidMqttPort(uint32_t port) {
  return port >= 1 && port <= 65535;
}

bool isValidIpv4Address(const char* value) {
  if (isBlank(value)) {
    return false;
  }

  uint8_t octets = 0;
  const char* cursor = value;
  while (*cursor) {
    if (octets >= 4) {
      return false;
    }

    uint16_t number = 0;
    uint8_t digits = 0;
    while (*cursor && *cursor != '.') {
      if (!std::isdigit(static_cast<unsigned char>(*cursor))) {
        return false;
      }
      number = static_cast<uint16_t>(number * 10 + (*cursor - '0'));
      if (number > 255) {
        return false;
      }
      ++digits;
      ++cursor;
    }

    if (digits == 0) {
      return false;
    }
    ++octets;

    if (*cursor == '.') {
      ++cursor;
      if (*cursor == '\0') {
        return false;
      }
    }
  }

  return octets == 4;
}

bool isStaticIpConfigValid(const char* staticIp, const char* gateway, const char* subnet) {
  const bool hasStaticIp = !isBlank(staticIp);
  const bool hasGateway = !isBlank(gateway);
  const bool hasSubnet = !isBlank(subnet);
  if (!hasStaticIp && !hasGateway && !hasSubnet) {
    return true;
  }

  if (!hasStaticIp || !hasGateway || !hasSubnet) {
    return false;
  }

  return isValidIpv4Address(staticIp) && isValidIpv4Address(gateway) && isValidIpv4Address(subnet);
}

bool isValidBatteryChemistryId(uint8_t chemistryId) {
  return chemistryId == kRuntimeBatteryChemistryLiIon || chemistryId == kRuntimeBatteryChemistryLiFePO4;
}

bool parseBatteryChemistry(const char* value, uint8_t& chemistryId) {
  char normalized[16];
  if (!copyTrimmed(normalized, sizeof(normalized), value) || normalized[0] == '\0') {
    chemistryId = kRuntimeBatteryChemistryLiIon;
    return true;
  }

  for (char* cursor = normalized; *cursor; ++cursor) {
    if (*cursor == '-') {
      *cursor = '_';
    } else {
      *cursor = static_cast<char>(std::tolower(static_cast<unsigned char>(*cursor)));
    }
  }

  if (equalsIgnoreCase(normalized, "li_ion") ||
      equalsIgnoreCase(normalized, "liion") ||
      equalsIgnoreCase(normalized, "lithium_ion")) {
    chemistryId = kRuntimeBatteryChemistryLiIon;
    return true;
  }

  if (equalsIgnoreCase(normalized, "lifepo4") || equalsIgnoreCase(normalized, "life_po4")) {
    chemistryId = kRuntimeBatteryChemistryLiFePO4;
    return true;
  }

  return false;
}

const char* batteryChemistryKey(uint8_t chemistryId) {
  return chemistryId == kRuntimeBatteryChemistryLiFePO4 ? "lifepo4" : "li_ion";
}

const char* batteryChemistryName(uint8_t chemistryId) {
  return chemistryId == kRuntimeBatteryChemistryLiFePO4 ? "LiFePO4" : "Li-ion";
}

bool populateRuntimeConfig(RuntimeConfig& config,
                           const char* mqttHost,
                           uint32_t mqttPort,
                           const char* mqttUsername,
                           const char* mqttPassword,
                           const char* otaPassword,
                           const char* batteryChemistry,
                           const char* staticIp,
                           const char* gateway,
                           const char* subnet) {
  RuntimeConfig next;
  next.schemaVersion = kRuntimeConfigSchemaVersion;
  next.mqttPort = isValidMqttPort(mqttPort) ? static_cast<uint16_t>(mqttPort) : 0;

  uint8_t chemistryId = kRuntimeBatteryChemistryLiIon;
  if (!parseBatteryChemistry(batteryChemistry, chemistryId)) {
    next.batteryChemistryId = 255;
  } else {
    next.batteryChemistryId = chemistryId;
  }

  if (!copyTrimmed(next.mqttHost, sizeof(next.mqttHost), mqttHost) ||
      !copyTrimmed(next.mqttUsername, sizeof(next.mqttUsername), mqttUsername) ||
      !copyTrimmed(next.mqttPassword, sizeof(next.mqttPassword), mqttPassword) ||
      !copyTrimmed(next.otaPassword, sizeof(next.otaPassword), otaPassword) ||
      !copyTrimmed(next.staticIp, sizeof(next.staticIp), staticIp) ||
      !copyTrimmed(next.gateway, sizeof(next.gateway), gateway) ||
      !copyTrimmed(next.subnet, sizeof(next.subnet), subnet)) {
    return false;
  }

  next.hasStaticIp = next.staticIp[0] || next.gateway[0] || next.subnet[0];
  config = next;
  return true;
}

RuntimeConfigValidation validateRuntimeConfig(const RuntimeConfig& config) {
  if (config.schemaVersion != kRuntimeConfigSchemaVersion) {
    return {false, "schema_version"};
  }
  if (isBlank(config.mqttHost)) {
    return {false, "mqtt_host_missing"};
  }
  if (!isValidMqttPort(config.mqttPort)) {
    return {false, "mqtt_port_invalid"};
  }
  const bool hasMqttUsername = !isBlank(config.mqttUsername);
  const bool hasMqttPassword = !isBlank(config.mqttPassword);
  if (hasMqttUsername != hasMqttPassword) {
    return {false, "mqtt_auth_partial"};
  }
  if (isBlank(config.otaPassword)) {
    return {false, "ota_password_missing"};
  }
  if (!isValidBatteryChemistryId(config.batteryChemistryId)) {
    return {false, "battery_chemistry_invalid"};
  }
  if (!isStaticIpConfigValid(config.staticIp, config.gateway, config.subnet)) {
    return {false, "static_ip_invalid"};
  }
  if (config.hasStaticIp && (isBlank(config.staticIp) || isBlank(config.gateway) || isBlank(config.subnet))) {
    return {false, "static_ip_invalid"};
  }
  if (!config.hasStaticIp && (!isBlank(config.staticIp) || !isBlank(config.gateway) || !isBlank(config.subnet))) {
    return {false, "static_ip_invalid"};
  }
  return {true, "ok"};
}

RuntimeConfig normalizedRuntimeConfig(const RuntimeConfig& config) {
  RuntimeConfig normalized = config;
  normalized.schemaVersion = kRuntimeConfigSchemaVersion;
  normalized.hasStaticIp = normalized.staticIp[0] || normalized.gateway[0] || normalized.subnet[0];
  return normalized;
}

#if defined(ARDUINO)
const RuntimeConfig& runtimeConfig() {
  return currentConfig;
}

bool loadRuntimeConfig() {
  Preferences preferences;
  if (!preferences.begin(kRuntimeConfigNamespace, true)) {
    Serial.println("Runtime config load failed: Preferences namespace unavailable");
    currentConfig = RuntimeConfig{};
    return false;
  }

  RuntimeConfig loaded;
  loaded.schemaVersion = preferences.getUShort("schema", 0);
  loaded.mqttPort = preferences.getUShort("mqtt_port", 0);
  loaded.batteryChemistryId = preferences.getUChar("batt_chem", 255);

  const bool stringsOk = readPreferenceString(preferences, "mqtt_host", loaded.mqttHost, sizeof(loaded.mqttHost)) &&
                         readPreferenceString(preferences, "mqtt_user", loaded.mqttUsername, sizeof(loaded.mqttUsername)) &&
                         readPreferenceString(preferences, "mqtt_pass", loaded.mqttPassword, sizeof(loaded.mqttPassword)) &&
                         readPreferenceString(preferences, "ota_pass", loaded.otaPassword, sizeof(loaded.otaPassword)) &&
                         readPreferenceString(preferences, "static_ip", loaded.staticIp, sizeof(loaded.staticIp)) &&
                         readPreferenceString(preferences, "gateway", loaded.gateway, sizeof(loaded.gateway)) &&
                         readPreferenceString(preferences, "subnet", loaded.subnet, sizeof(loaded.subnet));
  preferences.end();

  loaded.hasStaticIp = loaded.staticIp[0] || loaded.gateway[0] || loaded.subnet[0];
  currentConfig = loaded;

  if (!stringsOk) {
    Serial.println("Runtime config load failed: stored value exceeds firmware limit");
    return false;
  }

  const RuntimeConfigValidation validation = validateRuntimeConfig(currentConfig);
  if (!validation.valid) {
    Serial.printf("Runtime config invalid or missing: %s\n", validation.reason);
    return false;
  }

  Serial.println("Runtime config loaded from NVS");
  logRuntimeConfigSummary(currentConfig);
  return true;
}

bool saveRuntimeConfig(const RuntimeConfig& config) {
  RuntimeConfig normalized = normalizedRuntimeConfig(config);

  const RuntimeConfigValidation validation = validateRuntimeConfig(normalized);
  if (!validation.valid) {
    Serial.printf("Runtime config save rejected: %s\n", validation.reason);
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kRuntimeConfigNamespace, false)) {
    Serial.println("Runtime config save failed: Preferences namespace unavailable");
    return false;
  }

  bool ok = preferences.clear();
  ok &= preferences.putUShort("schema", normalized.schemaVersion) > 0;
  ok &= writePreferenceString(preferences, "mqtt_host", normalized.mqttHost);
  ok &= preferences.putUShort("mqtt_port", normalized.mqttPort) > 0;
  ok &= writePreferenceString(preferences, "mqtt_user", normalized.mqttUsername);
  ok &= writePreferenceString(preferences, "mqtt_pass", normalized.mqttPassword);
  ok &= writePreferenceString(preferences, "ota_pass", normalized.otaPassword);
  ok &= preferences.putUChar("batt_chem", normalized.batteryChemistryId) > 0;
  ok &= writePreferenceString(preferences, "static_ip", normalized.staticIp);
  ok &= writePreferenceString(preferences, "gateway", normalized.gateway);
  ok &= writePreferenceString(preferences, "subnet", normalized.subnet);
  preferences.end();

  if (!ok) {
    Serial.println("Runtime config save failed");
    return false;
  }

  currentConfig = normalized;
  Serial.println("Runtime config saved to NVS");
  logRuntimeConfigSummary(currentConfig);
  return true;
}

bool clearRuntimeConfig() {
  Preferences preferences;
  if (!preferences.begin(kRuntimeConfigNamespace, false)) {
    Serial.println("Runtime config clear failed: Preferences namespace unavailable");
    return false;
  }

  const bool ok = preferences.clear();
  preferences.end();
  currentConfig = RuntimeConfig{};

  Serial.printf("Runtime config clear from NVS: %s\n", ok ? "ok" : "FAILED");
  return ok;
}
#endif

}  // namespace Esp32Meteo
