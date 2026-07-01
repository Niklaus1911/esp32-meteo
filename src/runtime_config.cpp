#include "runtime_config.h"

#include <cctype>
#include <cstddef>
#include <cstring>
#include <type_traits>

#if defined(ARDUINO)
#include <Arduino.h>
#include <Preferences.h>

#include "util.h"
#endif

namespace Esp32Meteo {

namespace {

constexpr size_t kRuntimeConfigRecordCrcOffset = offsetof(RuntimeConfigRecord, crc32);

static_assert(kRuntimeConfigRecordCrcOffset + sizeof(uint32_t) == sizeof(RuntimeConfigRecord),
              "RuntimeConfigRecord crc32 must be the final field");
static_assert(std::is_trivially_copyable<RuntimeConfigRecord>::value,
              "RuntimeConfigRecord must stay a plain binary storage record");

#if defined(ARDUINO)
constexpr const char* kRuntimeConfigNamespace = "esp32_meteo";
constexpr const char* kRuntimeConfigSlotAKey = "cfg_a";
constexpr const char* kRuntimeConfigSlotBKey = "cfg_b";

RuntimeConfig currentConfig;
#endif

uint32_t crc32Append(uint32_t crc, const void* data, size_t length) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = (crc & 1U) ? 0xEDB88320UL : 0U;
      crc = (crc >> 1U) ^ mask;
    }
  }
  return crc;
}

uint32_t runtimeConfigRecordCrc(const RuntimeConfigRecord& record) {
  uint32_t crc = 0xFFFFFFFFUL;
  crc = crc32Append(crc, &record.magic, sizeof(record.magic));
  crc = crc32Append(crc, &record.recordVersion, sizeof(record.recordVersion));
  crc = crc32Append(crc, &record.reserved, sizeof(record.reserved));
  crc = crc32Append(crc, &record.sequence, sizeof(record.sequence));
  crc = crc32Append(crc, &record.payload.schemaVersion, sizeof(record.payload.schemaVersion));
  crc = crc32Append(crc, &record.payload.mqttPort, sizeof(record.payload.mqttPort));
  crc = crc32Append(crc, &record.payload.batteryChemistryId, sizeof(record.payload.batteryChemistryId));
  crc = crc32Append(crc, &record.payload.hasStaticIp, sizeof(record.payload.hasStaticIp));
  crc = crc32Append(crc, record.payload.reserved, sizeof(record.payload.reserved));
  crc = crc32Append(crc, record.payload.mqttHost, sizeof(record.payload.mqttHost));
  crc = crc32Append(crc, record.payload.mqttUsername, sizeof(record.payload.mqttUsername));
  crc = crc32Append(crc, record.payload.mqttPassword, sizeof(record.payload.mqttPassword));
  crc = crc32Append(crc, record.payload.otaPassword, sizeof(record.payload.otaPassword));
  crc = crc32Append(crc, record.payload.staticIp, sizeof(record.payload.staticIp));
  crc = crc32Append(crc, record.payload.gateway, sizeof(record.payload.gateway));
  crc = crc32Append(crc, record.payload.subnet, sizeof(record.payload.subnet));
  return crc ^ 0xFFFFFFFFUL;
}

bool isNullTerminated(const char* value, size_t size) {
  return std::memchr(value, '\0', size) != nullptr;
}

bool runtimeConfigPayloadStringsTerminated(const RuntimeConfigRecordPayload& payload) {
  return isNullTerminated(payload.mqttHost, sizeof(payload.mqttHost)) &&
         isNullTerminated(payload.mqttUsername, sizeof(payload.mqttUsername)) &&
         isNullTerminated(payload.mqttPassword, sizeof(payload.mqttPassword)) &&
         isNullTerminated(payload.otaPassword, sizeof(payload.otaPassword)) &&
         isNullTerminated(payload.staticIp, sizeof(payload.staticIp)) &&
         isNullTerminated(payload.gateway, sizeof(payload.gateway)) &&
         isNullTerminated(payload.subnet, sizeof(payload.subnet));
}

RuntimeConfig runtimeConfigFromPayload(const RuntimeConfigRecordPayload& payload) {
  RuntimeConfig config;
  config.schemaVersion = payload.schemaVersion;
  config.mqttPort = payload.mqttPort;
  config.batteryChemistryId = payload.batteryChemistryId;
  config.hasStaticIp = payload.hasStaticIp != 0;
  std::memcpy(config.mqttHost, payload.mqttHost, sizeof(config.mqttHost));
  std::memcpy(config.mqttUsername, payload.mqttUsername, sizeof(config.mqttUsername));
  std::memcpy(config.mqttPassword, payload.mqttPassword, sizeof(config.mqttPassword));
  std::memcpy(config.otaPassword, payload.otaPassword, sizeof(config.otaPassword));
  std::memcpy(config.staticIp, payload.staticIp, sizeof(config.staticIp));
  std::memcpy(config.gateway, payload.gateway, sizeof(config.gateway));
  std::memcpy(config.subnet, payload.subnet, sizeof(config.subnet));
  return config;
}

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
  Serial.printf("Runtime config: MQTT %s%s:%u%s, MQTT auth=%s%s%s, OTA password=%s%s%s\n",
                serialStyle(SerialStyle::Value),
                config.mqttHost,
                config.mqttPort,
                serialReset(),
                serialStyle(config.mqttUsername[0] || config.mqttPassword[0] ? SerialStyle::Success
                                                                              : SerialStyle::Warning),
                config.mqttUsername[0] || config.mqttPassword[0] ? "configured" : "anonymous",
                serialReset(),
                serialStyle(config.otaPassword[0] ? SerialStyle::Success : SerialStyle::Error),
                config.otaPassword[0] ? "configured" : "missing",
                serialReset());
  Serial.printf("Runtime config: battery chemistry %s%s%s (%s)\n",
                serialStyle(SerialStyle::Value),
                batteryChemistryName(config.batteryChemistryId),
                serialReset(),
                batteryChemistryKey(config.batteryChemistryId));
  if (config.hasStaticIp) {
    Serial.printf("Runtime config: static IP %s%s%s, gateway %s%s%s, subnet %s%s%s\n",
                  serialStyle(SerialStyle::Value),
                  config.staticIp,
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  config.gateway,
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  config.subnet,
                  serialReset());
  } else {
    Serial.printf("Runtime config: static IP %s; DHCP will be used\n", serialEnabledDisabled(false));
  }
}

RuntimeConfigRecordStatus readRuntimeConfigRecordSlot(Preferences& preferences,
                                                      const char* key,
                                                      RuntimeConfigRecord& record) {
  record = RuntimeConfigRecord{};
  const size_t length = preferences.getBytesLength(key);
  if (length == 0) {
    return RuntimeConfigRecordStatus::Missing;
  }
  if (length != sizeof(RuntimeConfigRecord)) {
    return RuntimeConfigRecordStatus::WrongSize;
  }

  const size_t bytesRead = preferences.getBytes(key, &record, sizeof(record));
  if (bytesRead != sizeof(record)) {
    return RuntimeConfigRecordStatus::WrongSize;
  }
  return validateRuntimeConfigRecord(record);
}

bool writeRuntimeConfigRecordSlot(Preferences& preferences,
                                  const char* key,
                                  const RuntimeConfig& config,
                                  uint32_t sequence) {
  const RuntimeConfigRecord record = makeRuntimeConfigRecord(config, sequence);
  const size_t bytesWritten = preferences.putBytes(key, &record, sizeof(record));
  if (bytesWritten != sizeof(record)) {
    return false;
  }

  RuntimeConfigRecord verified;
  const RuntimeConfigRecordStatus status = readRuntimeConfigRecordSlot(preferences, key, verified);
  return status == RuntimeConfigRecordStatus::Valid &&
         std::memcmp(&verified, &record, sizeof(record)) == 0;
}

void logRuntimeConfigSlotStatus(char slotLabel,
                                RuntimeConfigRecordStatus status,
                                const RuntimeConfigRecord& record) {
  const SerialStyle style = status == RuntimeConfigRecordStatus::Valid ? SerialStyle::Success
                                                                       : SerialStyle::Warning;
  Serial.printf("Runtime config protected slot %c: %s%s%s",
                slotLabel,
                serialStyle(style),
                runtimeConfigRecordStatusName(status),
                serialReset());
  if (status == RuntimeConfigRecordStatus::Valid) {
    Serial.printf(", sequence %s%lu%s",
                  serialStyle(SerialStyle::Value),
                  static_cast<unsigned long>(record.sequence),
                  serialReset());
  }
  Serial.println();
}

bool readLegacyRuntimeConfig(Preferences& preferences, RuntimeConfig& loaded, const char*& reason) {
  loaded = RuntimeConfig{};
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
  if (!stringsOk) {
    reason = "stored_value_too_long";
    return false;
  }

  loaded.hasStaticIp = loaded.staticIp[0] || loaded.gateway[0] || loaded.subnet[0];
  const RuntimeConfigValidation validation = validateRuntimeConfig(loaded);
  if (!validation.valid) {
    reason = validation.reason;
    return false;
  }

  reason = "ok";
  loaded = normalizedRuntimeConfig(loaded);
  return true;
}

bool writeLegacyRuntimeConfig(Preferences& preferences, const RuntimeConfig& config) {
  bool ok = true;
  ok &= preferences.putUShort("schema", config.schemaVersion) > 0;
  ok &= writePreferenceString(preferences, "mqtt_host", config.mqttHost);
  ok &= preferences.putUShort("mqtt_port", config.mqttPort) > 0;
  ok &= writePreferenceString(preferences, "mqtt_user", config.mqttUsername);
  ok &= writePreferenceString(preferences, "mqtt_pass", config.mqttPassword);
  ok &= writePreferenceString(preferences, "ota_pass", config.otaPassword);
  ok &= preferences.putUChar("batt_chem", config.batteryChemistryId) > 0;
  ok &= writePreferenceString(preferences, "static_ip", config.staticIp);
  ok &= writePreferenceString(preferences, "gateway", config.gateway);
  ok &= writePreferenceString(preferences, "subnet", config.subnet);
  return ok;
}

const char* runtimeConfigSlotKey(int slotIndex) {
  return slotIndex == 0 ? kRuntimeConfigSlotAKey : kRuntimeConfigSlotBKey;
}

char runtimeConfigSlotLabel(int slotIndex) {
  return slotIndex == 0 ? 'A' : 'B';
}

uint32_t nextRuntimeConfigSequence(const RuntimeConfigRecord& first,
                                   RuntimeConfigRecordStatus firstStatus,
                                   const RuntimeConfigRecord& second,
                                   RuntimeConfigRecordStatus secondStatus) {
  uint32_t highest = 0;
  if (firstStatus == RuntimeConfigRecordStatus::Valid && first.sequence > highest) {
    highest = first.sequence;
  }
  if (secondStatus == RuntimeConfigRecordStatus::Valid && second.sequence > highest) {
    highest = second.sequence;
  }
  return highest >= 0xFFFFFFFEUL ? 1 : highest + 1;
}

int firstRuntimeConfigSaveSlot(const RuntimeConfigRecord& first,
                               RuntimeConfigRecordStatus firstStatus,
                               const RuntimeConfigRecord& second,
                               RuntimeConfigRecordStatus secondStatus) {
  if (firstStatus != RuntimeConfigRecordStatus::Valid) {
    return 0;
  }
  if (secondStatus != RuntimeConfigRecordStatus::Valid) {
    return 1;
  }
  return first.sequence <= second.sequence ? 0 : 1;
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
  if (!copyTrimmed(normalized, sizeof(normalized), value)) {
    return false;
  }
  if (normalized[0] == '\0') {
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

RuntimeConfigRecord makeRuntimeConfigRecord(const RuntimeConfig& config, uint32_t sequence) {
  const RuntimeConfig normalized = normalizedRuntimeConfig(config);
  RuntimeConfigRecord record{};
  std::memset(&record, 0, sizeof(record));
  record.magic = kRuntimeConfigRecordMagic;
  record.recordVersion = kRuntimeConfigRecordVersion;
  record.reserved = 0;
  record.sequence = sequence;
  record.payload.schemaVersion = normalized.schemaVersion;
  record.payload.mqttPort = normalized.mqttPort;
  record.payload.batteryChemistryId = normalized.batteryChemistryId;
  record.payload.hasStaticIp = normalized.hasStaticIp ? 1 : 0;
  record.payload.reserved[0] = 0;
  record.payload.reserved[1] = 0;
  std::memcpy(record.payload.mqttHost, normalized.mqttHost, sizeof(record.payload.mqttHost));
  std::memcpy(record.payload.mqttUsername, normalized.mqttUsername, sizeof(record.payload.mqttUsername));
  std::memcpy(record.payload.mqttPassword, normalized.mqttPassword, sizeof(record.payload.mqttPassword));
  std::memcpy(record.payload.otaPassword, normalized.otaPassword, sizeof(record.payload.otaPassword));
  std::memcpy(record.payload.staticIp, normalized.staticIp, sizeof(record.payload.staticIp));
  std::memcpy(record.payload.gateway, normalized.gateway, sizeof(record.payload.gateway));
  std::memcpy(record.payload.subnet, normalized.subnet, sizeof(record.payload.subnet));
  record.crc32 = runtimeConfigRecordCrc(record);
  return record;
}

RuntimeConfigRecordStatus validateRuntimeConfigRecord(const RuntimeConfigRecord& record) {
  if (record.magic != kRuntimeConfigRecordMagic) {
    return RuntimeConfigRecordStatus::BadMagic;
  }
  if (record.recordVersion != kRuntimeConfigRecordVersion) {
    return RuntimeConfigRecordStatus::UnsupportedVersion;
  }
  if (record.crc32 != runtimeConfigRecordCrc(record)) {
    return RuntimeConfigRecordStatus::CrcMismatch;
  }
  if (!runtimeConfigPayloadStringsTerminated(record.payload)) {
    return RuntimeConfigRecordStatus::SemanticInvalid;
  }

  const RuntimeConfig config = runtimeConfigFromPayload(record.payload);
  const RuntimeConfigValidation validation = validateRuntimeConfig(config);
  if (!validation.valid || record.payload.hasStaticIp > 1) {
    return RuntimeConfigRecordStatus::SemanticInvalid;
  }
  return RuntimeConfigRecordStatus::Valid;
}

bool runtimeConfigFromRecord(const RuntimeConfigRecord& record, RuntimeConfig& config) {
  if (validateRuntimeConfigRecord(record) != RuntimeConfigRecordStatus::Valid) {
    return false;
  }

  config = runtimeConfigFromPayload(record.payload);
  return true;
}

int selectRuntimeConfigRecord(const RuntimeConfigRecord& first,
                              RuntimeConfigRecordStatus firstStatus,
                              const RuntimeConfigRecord& second,
                              RuntimeConfigRecordStatus secondStatus) {
  const bool firstValid = firstStatus == RuntimeConfigRecordStatus::Valid;
  const bool secondValid = secondStatus == RuntimeConfigRecordStatus::Valid;
  if (firstValid && secondValid) {
    return second.sequence > first.sequence ? 1 : 0;
  }
  if (firstValid) {
    return 0;
  }
  if (secondValid) {
    return 1;
  }
  return -1;
}

const char* runtimeConfigRecordStatusName(RuntimeConfigRecordStatus status) {
  switch (status) {
    case RuntimeConfigRecordStatus::Valid:
      return "valid";
    case RuntimeConfigRecordStatus::Missing:
      return "missing";
    case RuntimeConfigRecordStatus::WrongSize:
      return "wrong_size";
    case RuntimeConfigRecordStatus::BadMagic:
      return "bad_magic";
    case RuntimeConfigRecordStatus::UnsupportedVersion:
      return "unsupported_version";
    case RuntimeConfigRecordStatus::CrcMismatch:
      return "crc_mismatch";
    case RuntimeConfigRecordStatus::SemanticInvalid:
      return "semantic_invalid";
  }
  return "unknown";
}

#if defined(ARDUINO)
const RuntimeConfig& runtimeConfig() {
  return currentConfig;
}

bool loadRuntimeConfig() {
  Preferences preferences;
  if (!preferences.begin(kRuntimeConfigNamespace, false)) {
    Serial.printf("%sRuntime config load failed%s: Preferences namespace unavailable\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
    currentConfig = RuntimeConfig{};
    return false;
  }

  RuntimeConfigRecord slotA;
  RuntimeConfigRecord slotB;
  const RuntimeConfigRecordStatus slotAStatus =
      readRuntimeConfigRecordSlot(preferences, kRuntimeConfigSlotAKey, slotA);
  const RuntimeConfigRecordStatus slotBStatus =
      readRuntimeConfigRecordSlot(preferences, kRuntimeConfigSlotBKey, slotB);
  logRuntimeConfigSlotStatus('A', slotAStatus, slotA);
  logRuntimeConfigSlotStatus('B', slotBStatus, slotB);

  const int selectedSlot = selectRuntimeConfigRecord(slotA, slotAStatus, slotB, slotBStatus);
  if (selectedSlot >= 0) {
    const RuntimeConfigRecord& selectedRecord = selectedSlot == 0 ? slotA : slotB;
    RuntimeConfig loaded;
    if (!runtimeConfigFromRecord(selectedRecord, loaded)) {
      preferences.end();
      Serial.printf("%sRuntime config protected load failed%s: selected slot did not decode\n",
                    serialStyle(SerialStyle::Error),
                    serialReset());
      currentConfig = RuntimeConfig{};
      return false;
    }

    currentConfig = normalizedRuntimeConfig(loaded);
    Serial.printf("%sRuntime config loaded from protected NVS slot %c%s\n",
                  serialStyle(SerialStyle::Success),
                  runtimeConfigSlotLabel(selectedSlot),
                  serialReset());

    const int otherSlot = selectedSlot == 0 ? 1 : 0;
    const RuntimeConfigRecordStatus otherStatus = selectedSlot == 0 ? slotBStatus : slotAStatus;
    if (otherStatus != RuntimeConfigRecordStatus::Valid) {
      const uint32_t repairSequence =
          selectedRecord.sequence >= 0xFFFFFFFEUL ? 1 : selectedRecord.sequence + 1;
      const bool repaired = writeRuntimeConfigRecordSlot(preferences,
                                                         runtimeConfigSlotKey(otherSlot),
                                                         currentConfig,
                                                         repairSequence);
      Serial.printf("Runtime config protected slot %c repair: %s\n",
                    runtimeConfigSlotLabel(otherSlot),
                    serialOkFailed(repaired));
    }

    preferences.end();
    logRuntimeConfigSummary(currentConfig);
    return true;
  }

  RuntimeConfig legacyConfig;
  const char* legacyReason = "unknown";
  const bool legacyOk = readLegacyRuntimeConfig(preferences, legacyConfig, legacyReason);
  if (!legacyOk) {
    preferences.end();
    Serial.printf("%sRuntime config invalid or missing%s: protected slots unusable, legacy reason %s\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset(),
                  legacyReason);
    currentConfig = RuntimeConfig{};
    return false;
  }

  const bool slotAOk = writeRuntimeConfigRecordSlot(preferences, kRuntimeConfigSlotAKey, legacyConfig, 1);
  const bool slotBOk = writeRuntimeConfigRecordSlot(preferences, kRuntimeConfigSlotBKey, legacyConfig, 2);
  preferences.end();

  currentConfig = legacyConfig;
  Serial.printf("%sRuntime config loaded from legacy NVS%s; protected migration slot A: %s, slot B: %s\n",
                serialStyle(SerialStyle::Success),
                serialReset(),
                serialOkFailed(slotAOk),
                serialOkFailed(slotBOk));
  logRuntimeConfigSummary(currentConfig);
  return true;
}

bool saveRuntimeConfig(const RuntimeConfig& config) {
  RuntimeConfig normalized = normalizedRuntimeConfig(config);

  const RuntimeConfigValidation validation = validateRuntimeConfig(normalized);
  if (!validation.valid) {
    Serial.printf("%sRuntime config save rejected%s: %s\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset(),
                  validation.reason);
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kRuntimeConfigNamespace, false)) {
    Serial.printf("%sRuntime config save failed%s: Preferences namespace unavailable\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
    return false;
  }

  RuntimeConfigRecord slotA;
  RuntimeConfigRecord slotB;
  const RuntimeConfigRecordStatus slotAStatus =
      readRuntimeConfigRecordSlot(preferences, kRuntimeConfigSlotAKey, slotA);
  const RuntimeConfigRecordStatus slotBStatus =
      readRuntimeConfigRecordSlot(preferences, kRuntimeConfigSlotBKey, slotB);
  const int firstSlot = firstRuntimeConfigSaveSlot(slotA, slotAStatus, slotB, slotBStatus);
  const int secondSlot = firstSlot == 0 ? 1 : 0;
  const uint32_t firstSequence = nextRuntimeConfigSequence(slotA, slotAStatus, slotB, slotBStatus);
  const uint32_t secondSequence = firstSequence >= 0xFFFFFFFEUL ? 1 : firstSequence + 1;

  const bool firstOk = writeRuntimeConfigRecordSlot(preferences,
                                                    runtimeConfigSlotKey(firstSlot),
                                                    normalized,
                                                    firstSequence);
  const bool secondOk = firstOk &&
                        writeRuntimeConfigRecordSlot(preferences,
                                                     runtimeConfigSlotKey(secondSlot),
                                                     normalized,
                                                     secondSequence);
  const bool legacyOk = firstOk && secondOk && writeLegacyRuntimeConfig(preferences, normalized);
  preferences.end();

  if (!firstOk || !secondOk) {
    Serial.printf("%sRuntime config save failed%s\n", serialStyle(SerialStyle::Error), serialReset());
    return false;
  }

  currentConfig = normalized;
  Serial.printf("%sRuntime config saved to protected NVS%s: slot %c sequence %lu, slot %c sequence %lu\n",
                serialStyle(SerialStyle::Success),
                serialReset(),
                runtimeConfigSlotLabel(firstSlot),
                static_cast<unsigned long>(firstSequence),
                runtimeConfigSlotLabel(secondSlot),
                static_cast<unsigned long>(secondSequence));
  if (!legacyOk) {
    Serial.printf("%sRuntime config legacy mirror update failed%s; protected slots remain valid\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
  }
  logRuntimeConfigSummary(currentConfig);
  return true;
}

bool clearRuntimeConfig() {
  Preferences preferences;
  if (!preferences.begin(kRuntimeConfigNamespace, false)) {
    Serial.printf("%sRuntime config clear failed%s: Preferences namespace unavailable\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
    return false;
  }

  const bool ok = preferences.clear();
  preferences.end();
  currentConfig = RuntimeConfig{};

  Serial.printf("Runtime config clear from NVS: %s\n", serialOkFailed(ok));
  return ok;
}
#endif

}  // namespace Esp32Meteo
