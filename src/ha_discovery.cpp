#include "ha_discovery.h"

#include <Arduino.h>

#include "config.h"
#include "mqtt_client.h"
#include "runtime_state.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

void publishHaSensorDiscovery(const char* objectId,
                              const char* name,
                              const char* stateTopic,
                              const char* deviceClass,
                              const char* unit,
                              const char* stateClass,
                              const char* entityCategory) {
  char payload[1024];
  char optionalFields[260] = "";

  if (deviceClass && deviceClass[0]) {
    char field[64];
    if (!formatInto(field, sizeof(field), "HA sensor device_class field", ",\"device_class\":\"%s\"", deviceClass) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional device_class too long\n", objectId);
      return;
    }
  }
  if (unit && unit[0]) {
    char field[64];
    if (!formatInto(field, sizeof(field), "HA sensor unit field", ",\"unit_of_measurement\":\"%s\"", unit) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional unit too long\n", objectId);
      return;
    }
  }
  if (stateClass && stateClass[0]) {
    char field[64];
    if (!formatInto(field, sizeof(field), "HA sensor state_class field", ",\"state_class\":\"%s\"", stateClass) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional state_class too long\n", objectId);
      return;
    }
  }
  if (entityCategory && entityCategory[0]) {
    char field[80];
    if (!formatInto(field, sizeof(field), "HA sensor entity_category field", ",\"entity_category\":\"%s\"", entityCategory) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional entity_category too long\n", objectId);
      return;
    }
  }

  if (!formatInto(payload,
                  sizeof(payload),
                  "HA sensor discovery payload",
                  "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\"%s,"
                  "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"},"
                  "\"origin\":{\"name\":\"%s\",\"sw_version\":\"%s\"}}",
                  name,
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
    return;
  }

  publishDiscoveryPayload("sensor", objectId, payload);
}

bool formatHaObjectId(char* buffer, size_t bufferSize, const char* suffix) {
  return formatInto(buffer, bufferSize, "HA discovery object id", "%s_%s", kHaUniqueIdPrefix, suffix);
}

void publishPrefixedHaSensorDiscovery(const char* objectSuffix,
                                      const char* name,
                                      const char* stateTopic,
                                      const char* deviceClass,
                                      const char* unit,
                                      const char* stateClass,
                                      const char* entityCategory) {
  char objectId[96];
  if (!formatHaObjectId(objectId, sizeof(objectId), objectSuffix)) {
    Serial.printf("HA discovery skipped for %s: object id too long\n", objectSuffix);
    return;
  }

  publishHaSensorDiscovery(objectId, name, stateTopic, deviceClass, unit, stateClass, entityCategory);
}

void publishHaSwitchDiscovery() {
  char objectId[96];
  if (!formatHaObjectId(objectId, sizeof(objectId), "stay_awake")) {
    Serial.println("HA discovery skipped for stay_awake: object id too long");
    return;
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
    return;
  }

  publishDiscoveryPayload("switch", objectId, payload);
}

}  // namespace

void publishHomeAssistantDiscovery() {
  homeAssistantDiscoveryRequested = false;
  logPhase("Home Assistant discovery");
  Serial.printf("Publishing retained discovery configs under %s/#\n", kHaDiscoveryPrefix);

  publishPrefixedHaSensorDiscovery("bmp390_temperature",
                                   "BMP390 Temperature",
                                   topic("/sensor/bmp390_temperature").c_str(),
                                   "temperature",
                                   "\\u00b0C",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("absolute_pressure",
                                   "Absolute Pressure",
                                   topic("/sensor/absolute_pressure").c_str(),
                                   "atmospheric_pressure",
                                   "hPa",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("outside_temperature",
                                   "Outside Temperature",
                                   topic("/sensor/outside_temperature").c_str(),
                                   "temperature",
                                   "\\u00b0C",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("outside_humidity",
                                   "Outside Humidity",
                                   topic("/sensor/outside_humidity").c_str(),
                                   "humidity",
                                   "%",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("battery_voltage",
                                   "Battery Voltage",
                                   topic("/sensor/battery_voltage").c_str(),
                                   "voltage",
                                   "V",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("battery_current",
                                   "Battery Current",
                                   topic("/sensor/battery_current").c_str(),
                                   "current",
                                   "mA",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("battery_power",
                                   "Battery Power",
                                   topic("/sensor/battery_power").c_str(),
                                   "power",
                                   "W",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("battery_level",
                                   "Battery Level",
                                   topic("/sensor/battery_level").c_str(),
                                   "battery",
                                   "%",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("solar_raw_voltage",
                                   "Solar Raw Voltage",
                                   topic("/sensor/solar_raw_voltage").c_str(),
                                   "voltage",
                                   "V",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("solar_panel_current",
                                   "Solar Panel Current",
                                   topic("/sensor/solar_panel_current").c_str(),
                                   "current",
                                   "mA",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("solar_raw_power",
                                   "Solar Raw Power",
                                   topic("/sensor/solar_raw_power").c_str(),
                                   "power",
                                   "W",
                                   "measurement",
                                   "");
  publishPrefixedHaSensorDiscovery("wifi_signal",
                                   "WiFi Signal",
                                   topic("/diagnostic/wifi_signal").c_str(),
                                   "signal_strength",
                                   "dBm",
                                   "measurement",
                                   "diagnostic");
  publishPrefixedHaSensorDiscovery("wifi_ssid",
                                   "WiFi SSID",
                                   topic("/diagnostic/wifi_ssid").c_str(),
                                   "",
                                   "",
                                   "",
                                   "diagnostic");
  publishPrefixedHaSensorDiscovery("ip_address",
                                   "IP Address",
                                   topic("/diagnostic/ip_address").c_str(),
                                   "",
                                   "",
                                   "",
                                   "diagnostic");
  publishPrefixedHaSensorDiscovery("reset_reason",
                                   "Reset Reason",
                                   topic("/diagnostic/reset_reason").c_str(),
                                   "",
                                   "",
                                   "",
                                   "diagnostic");
  publishPrefixedHaSensorDiscovery("sensor_readiness",
                                   "Sensor Readiness",
                                   topic("/diagnostic/sensor_readiness").c_str(),
                                   "",
                                   "",
                                   "",
                                   "diagnostic");
  publishPrefixedHaSensorDiscovery("battery_chemistry",
                                   "Battery Chemistry",
                                   topic("/diagnostic/battery_chemistry").c_str(),
                                   "",
                                   "",
                                   "",
                                   "diagnostic");
  publishPrefixedHaSensorDiscovery("status",
                                   "Status",
                                   kStatusTopic,
                                   "",
                                   "",
                                   "",
                                   "diagnostic");
  publishHaSwitchDiscovery();
}

}  // namespace Esp32Meteo
