#include "ha_discovery.h"

#include <Arduino.h>

#include "config.h"
#include "mqtt_client.h"
#include "runtime_state.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

struct HaSensorDefinition {
  const char* objectSuffix;
  const char* name;
  const char* stateTopicSuffix;
  const char* stateTopic;
  const char* deviceClass;
  const char* unit;
  const char* stateClass;
  const char* entityCategory;
};

bool appendOptionalJsonField(char* buffer,
                             size_t bufferSize,
                             const char* fieldName,
                             const char* value,
                             const char* description) {
  if (!value || !value[0]) {
    return true;
  }

  char field[96];
  if (!formatInto(field, sizeof(field), description, ",\"%s\":\"%s\"", fieldName, value)) {
    return false;
  }
  return appendChecked(buffer, bufferSize, "HA sensor optional fields", field);
}

bool formatHaObjectId(char* buffer, size_t bufferSize, const char* suffix) {
  return formatInto(buffer, bufferSize, "HA discovery object id", "%s_%s", kHaUniqueIdPrefix, suffix);
}

bool publishHaSensorDiscovery(const HaSensorDefinition& definition) {
  char objectId[96];
  if (!formatHaObjectId(objectId, sizeof(objectId), definition.objectSuffix)) {
    Serial.printf("HA discovery skipped for %s: object id too long\n", definition.objectSuffix);
    return false;
  }

  char stateTopic[128];
  if (definition.stateTopic && definition.stateTopic[0]) {
    if (!formatInto(stateTopic, sizeof(stateTopic), "HA sensor state topic", "%s", definition.stateTopic)) {
      Serial.printf("HA discovery skipped for %s: state topic too long\n", objectId);
      return false;
    }
  } else if (definition.stateTopicSuffix && definition.stateTopicSuffix[0]) {
    if (!formatInto(stateTopic,
                    sizeof(stateTopic),
                    "HA sensor state topic",
                    "%s%s",
                    kTopicPrefix,
                    definition.stateTopicSuffix)) {
      Serial.printf("HA discovery skipped for %s: state topic too long\n", objectId);
      return false;
    }
  } else {
    Serial.printf("HA discovery skipped for %s: missing state topic\n", objectId);
    return false;
  }

  char optionalFields[260] = "";
  if (!appendOptionalJsonField(optionalFields,
                               sizeof(optionalFields),
                               "device_class",
                               definition.deviceClass,
                               "HA sensor device_class field") ||
      !appendOptionalJsonField(optionalFields,
                               sizeof(optionalFields),
                               "unit_of_measurement",
                               definition.unit,
                               "HA sensor unit field") ||
      !appendOptionalJsonField(optionalFields,
                               sizeof(optionalFields),
                               "state_class",
                               definition.stateClass,
                               "HA sensor state_class field") ||
      !appendOptionalJsonField(optionalFields,
                               sizeof(optionalFields),
                               "entity_category",
                               definition.entityCategory,
                               "HA sensor entity_category field")) {
    Serial.printf("HA discovery skipped for %s: optional fields too long\n", objectId);
    return false;
  }

  char payload[1024];
  if (!formatInto(payload,
                  sizeof(payload),
                  "HA sensor discovery payload",
                  "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\"%s,"
                  "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"},"
                  "\"origin\":{\"name\":\"%s\",\"sw_version\":\"%s\"}}",
                  definition.name,
                  objectId,
                  stateTopic,
                  optionalFields,
                  kHaDeviceIdentifier,
                  kHaDeviceName,
                  kHaManufacturer,
                  kHaModel,
                  kFirmwareName,
                  kFirmwareName,
                  kFirmwareName)) {
    Serial.printf("HA discovery skipped for %s: payload too long\n", objectId);
    return false;
  }

  return publishDiscoveryPayload("sensor", objectId, payload);
}

bool publishHaSwitchDiscovery() {
  char objectId[96];
  if (!formatHaObjectId(objectId, sizeof(objectId), "stay_awake")) {
    Serial.println("HA discovery skipped for stay_awake: object id too long");
    return false;
  }

  char payload[1024];
  if (!formatInto(payload,
                  sizeof(payload),
                  "HA switch discovery payload",
                  "{\"name\":\"Stay Awake\",\"unique_id\":\"%s\","
                  "\"command_topic\":\"%s\",\"state_topic\":\"%s\","
                  "\"payload_on\":\"true\",\"payload_off\":\"false\","
                  "\"state_on\":\"true\",\"state_off\":\"false\",\"retain\":true,"
                  "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"},"
                  "\"origin\":{\"name\":\"%s\",\"sw_version\":\"%s\"}}",
                  objectId,
                  kStayAwakeTopic,
                  kStayAwakeTopic,
                  kHaDeviceIdentifier,
                  kHaDeviceName,
                  kHaManufacturer,
                  kHaModel,
                  kFirmwareName,
                  kFirmwareName,
                  kFirmwareName)) {
    Serial.printf("HA discovery skipped for %s: payload too long\n", objectId);
    return false;
  }

  return publishDiscoveryPayload("switch", objectId, payload);
}

constexpr HaSensorDefinition kHaSensorDefinitions[] = {
    {"bmp390_temperature", "BMP390 Temperature", "/sensor/bmp390_temperature", nullptr, "temperature", "\\u00b0C", "measurement", ""},
    {"absolute_pressure", "Absolute Pressure", "/sensor/absolute_pressure", nullptr, "atmospheric_pressure", "hPa", "measurement", ""},
    {"outside_temperature", "Outside Temperature", "/sensor/outside_temperature", nullptr, "temperature", "\\u00b0C", "measurement", ""},
    {"outside_humidity", "Outside Humidity", "/sensor/outside_humidity", nullptr, "humidity", "%", "measurement", ""},
    {"battery_voltage", "Battery Voltage", "/sensor/battery_voltage", nullptr, "voltage", "V", "measurement", ""},
    {"battery_current", "Battery Current", "/sensor/battery_current", nullptr, "current", "mA", "measurement", ""},
    {"battery_power", "Battery Power", "/sensor/battery_power", nullptr, "power", "W", "measurement", ""},
    {"battery_level", "Battery Level", "/sensor/battery_level", nullptr, "battery", "%", "measurement", ""},
    {"solar_raw_voltage", "Solar Raw Voltage", "/sensor/solar_raw_voltage", nullptr, "voltage", "V", "measurement", ""},
    {"solar_panel_current", "Solar Panel Current", "/sensor/solar_panel_current", nullptr, "current", "mA", "measurement", ""},
    {"solar_raw_power", "Solar Raw Power", "/sensor/solar_raw_power", nullptr, "power", "W", "measurement", ""},
    {"wifi_signal", "WiFi Signal", "/diagnostic/wifi_signal", nullptr, "signal_strength", "dBm", "measurement", "diagnostic"},
    {"wifi_ssid", "WiFi SSID", "/diagnostic/wifi_ssid", nullptr, "", "", "", "diagnostic"},
    {"ip_address", "IP Address", "/diagnostic/ip_address", nullptr, "", "", "", "diagnostic"},
    {"reset_reason", "Reset Reason", "/diagnostic/reset_reason", nullptr, "", "", "", "diagnostic"},
    {"sensor_readiness", "Sensor Readiness", "/diagnostic/sensor_readiness", nullptr, "", "", "", "diagnostic"},
    {"battery_chemistry", "Battery Chemistry", "/diagnostic/battery_chemistry", nullptr, "", "", "", "diagnostic"},
    {"status", "Status", nullptr, kStatusTopic, "", "", "", "diagnostic"},
};

}  // namespace

bool publishHomeAssistantDiscovery() {
  homeAssistantDiscoveryRequested = false;
  logPhase("Home Assistant discovery");
  Serial.printf("Publishing retained discovery configs under %s/#\n", kHaDiscoveryPrefix);

  bool allOk = true;
  for (const HaSensorDefinition& definition : kHaSensorDefinitions) {
    allOk &= publishHaSensorDiscovery(definition);
  }
  allOk &= publishHaSwitchDiscovery();

  Serial.printf("Home Assistant discovery result: %s\n", allOk ? "complete" : "FAILED");
  return allOk;
}

}  // namespace Esp32Meteo
