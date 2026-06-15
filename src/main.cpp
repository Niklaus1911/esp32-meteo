#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <esp_sleep.h>

#include "config.h"
#include "runtime_state.h"
#include "sensors.h"
#include "util.h"

namespace {

using namespace Esp32Meteo;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void publishHomeAssistantDiscovery();
void flushMqtt(uint32_t durationMs);
void waitForRetainedStayAwakeCommand();

void markTelemetryPublishCompleted() {
  telemetryPublishCompleted = true;
  telemetryPublishCompletedMs = millis();
  Serial.printf("Telemetry publish completed at %lu ms; post-telemetry awake window starts now\n",
                static_cast<unsigned long>(telemetryPublishCompletedMs));
}

void serviceMqttAndOta() {
  ArduinoOTA.handle();
  mqttClient.loop();
}

void waitWithMqttAndOta(uint32_t durationMs, const char* description) {
  if (durationMs == 0) {
    return;
  }

  Serial.printf("%s for %lu ms\n", description, static_cast<unsigned long>(durationMs));
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    serviceMqttAndOta();
    delay(10);
  }
  Serial.printf("%s complete\n", description);
}

void waitForPostTelemetryAwakeWindow() {
  if (!telemetryPublishCompleted) {
    Serial.println("Telemetry publish was not completed; skipping post-telemetry awake wait");
    return;
  }

  const uint32_t elapsedMs = millis() - telemetryPublishCompletedMs;
  if (elapsedMs >= kPostTelemetryAwakeMs) {
    Serial.printf("Post-telemetry awake window already satisfied: %lu/%lu ms\n",
                  static_cast<unsigned long>(elapsedMs),
                  static_cast<unsigned long>(kPostTelemetryAwakeMs));
    return;
  }

  const uint32_t remainingMs = kPostTelemetryAwakeMs - elapsedMs;
  Serial.printf("Waiting %lu ms to complete post-telemetry awake window (%lu/%lu ms elapsed)\n",
                static_cast<unsigned long>(remainingMs),
                static_cast<unsigned long>(elapsedMs),
                static_cast<unsigned long>(kPostTelemetryAwakeMs));
  waitWithMqttAndOta(remainingMs, "Post-telemetry awake window wait");
}

bool configureStaticWifiIp() {
  if (!kWifiHasStaticIp) {
    Serial.println("WiFi static IP not configured; using DHCP");
    return true;
  }

  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;

  if (!localIp.fromString(kWifiStaticIp) || !gateway.fromString(kWifiGateway) || !subnet.fromString(WIFI_SUBNET)) {
    Serial.println("WiFi static IP constants are invalid; falling back to DHCP");
    return false;
  }

  const bool configured = WiFi.config(localIp, gateway, subnet);
  Serial.printf("WiFi static IP config %s, IP %s, gateway %s, subnet %s\n",
                configured ? "applied" : "FAILED",
                localIp.toString().c_str(),
                gateway.toString().c_str(),
                subnet.toString().c_str());
  return configured;
}

bool connectWifiSlot(const char* slotName, const char* ssid, const char* password) {
  Serial.printf("Connecting to WiFi %s slot with timeout %lu ms\n",
                slotName,
                static_cast<unsigned long>(kWifiConnectTimeoutMs));
  Serial.println("WiFi password will not be printed");

  WiFi.begin(ssid, password);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kWifiConnectTimeoutMs) {
    delay(kWifiRetryDelayMs);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi %s slot failed after %lu ms, status=%d\n",
                  slotName,
                  static_cast<unsigned long>(millis() - started),
                  WiFi.status());
    WiFi.disconnect(false);
    delay(250);
    return false;
  }

  Serial.printf("WiFi %s slot connected in %lu ms, RSSI %d dBm, IP %s\n",
                slotName,
                static_cast<unsigned long>(millis() - started),
                WiFi.RSSI(),
                WiFi.localIP().toString().c_str());
  return true;
}

bool connectWifi() {
  logPhase("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(kWifiTxPower);
  Serial.printf("WiFi TX power requested: %s\n", kWifiTxPowerLabel);
  configureStaticWifiIp();

  Serial.println("WiFi credentials loaded from generated header");
  if (connectWifiSlot("primary", WIFI_PRIMARY_SSID, WIFI_PRIMARY_PASSWORD)) {
    return true;
  }

  if (WIFI_HAS_BACKUP) {
    Serial.println("Primary WiFi failed; trying backup WiFi slot");
    return connectWifiSlot("backup", WIFI_BACKUP_SSID, WIFI_BACKUP_PASSWORD);
  }

  Serial.println("Primary WiFi failed and no backup WiFi slot is configured");
  return false;
}

void initializeOta() {
  logPhase("OTA");
  ArduinoOTA.setHostname(kOtaHostname);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    Serial.println("OTA update started; deep sleep is blocked until update finishes");
    if (mqttClient.connected()) {
      mqttClient.publish(kStatusTopic, "ota_updating", true);
    }
  });

  ArduinoOTA.onEnd([]() {
    Serial.println();
    Serial.println("OTA update finished; ESP32 will reboot");
    otaInProgress = false;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) {
      return;
    }
    static uint8_t lastPercent = 255;
    const uint8_t percent = static_cast<uint8_t>((progress * 100U) / total);
    if (percent != lastPercent && percent % 10 == 0) {
      Serial.printf("OTA progress: %u%%\n", percent);
      lastPercent = percent;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    Serial.printf("OTA error %u: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("auth failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("begin failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("connect failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("receive failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("end failed");
    } else {
      Serial.println("unknown");
    }
  });

  ArduinoOTA.begin();
  Serial.printf("ArduinoOTA ready, hostname %s, IP %s\n", kOtaHostname, WiFi.localIP().toString().c_str());
  Serial.println("OTA password loaded from generated header and will not be printed");
}

bool parseStayAwakePayload(const char* payload, size_t length, bool& value) {
  String normalized;
  normalized.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    if (!isspace(static_cast<unsigned char>(payload[i]))) {
      normalized += static_cast<char>(tolower(static_cast<unsigned char>(payload[i])));
    }
  }

  if (normalized == "true" || normalized == "on" || normalized == "1" || normalized == "yes") {
    value = true;
    return true;
  }
  if (normalized == "false" || normalized == "off" || normalized == "0" || normalized == "no") {
    value = false;
    return true;
  }
  return false;
}

void mqttCallback(char* receivedTopic, byte* payload, unsigned int length) {
  if (strcmp(receivedTopic, kHaStatusTopic) == 0) {
    bool isHomeAssistantOnline = length == 6;
    const char expected[] = "online";
    for (unsigned int i = 0; isHomeAssistantOnline && i < length; ++i) {
      isHomeAssistantOnline = payload[i] == expected[i];
    }

    if (isHomeAssistantOnline) {
      Serial.println("Home Assistant announced online; discovery republish queued");
      homeAssistantDiscoveryRequested = true;
    }
    return;
  }

  if (strcmp(receivedTopic, kStatusTopic) == 0) {
    if (statusConfirmationPending && payloadEquals(payload, length, statusConfirmationExpected)) {
      statusConfirmationReceived = true;
      Serial.printf("MQTT status confirmation received on %s: %s\n",
                    kStatusTopic,
                    statusConfirmationExpected);
    } else if (statusConfirmationPending) {
      Serial.printf("MQTT status confirmation ignored on %s: payload length=%u, expected=%s\n",
                    kStatusTopic,
                    length,
                    statusConfirmationExpected ? statusConfirmationExpected : "(none)");
    }
    return;
  }

  if (strcmp(receivedTopic, kStayAwakeTopic) != 0) {
    return;
  }

  bool parsedValue = false;
  if (parseStayAwakePayload(reinterpret_cast<const char*>(payload), length, parsedValue)) {
    stayAwakeRequested = parsedValue;
    stayAwakeCommandReceived = true;
    Serial.printf("Stay-awake command received on %s: %s\n",
                  kStayAwakeTopic,
                  stayAwakeRequested ? "true" : "false");
  } else {
    Serial.printf("Ignoring invalid stay-awake command payload on %s, length=%u\n", kStayAwakeTopic, length);
  }
}

bool connectMqtt() {
  logPhase("MQTT");
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  const bool bufferConfigured = mqttClient.setBufferSize(kMqttBufferSize);

  Serial.printf("MQTT server: %s:%u\n", MQTT_HOST, MQTT_PORT);
  Serial.printf("MQTT buffer size %u bytes: %s\n",
                static_cast<unsigned int>(kMqttBufferSize),
                bufferConfigured ? "ok" : "FAILED");
  Serial.printf("MQTT client ID prefix: %s\n", kTopicPrefix);
  Serial.printf("MQTT status topic: %s\n", kStatusTopic);
  Serial.printf("MQTT stay-awake topic: %s\n", kStayAwakeTopic);
  if (!bufferConfigured) {
    return false;
  }
  Serial.printf("Connecting to MQTT with timeout %lu ms\n", static_cast<unsigned long>(kMqttConnectTimeoutMs));
  const uint32_t started = millis();
  uint8_t attempt = 0;
  while (!mqttClient.connected() && millis() - started < kMqttConnectTimeoutMs) {
    ++attempt;
    const String clientId = String(kTopicPrefix) + "-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
    Serial.printf("MQTT connect attempt %u using client ID %s\n", attempt, clientId.c_str());
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.printf("MQTT connected in %lu ms\n", static_cast<unsigned long>(millis() - started));
      const bool statusPublished = mqttClient.publish(kStatusTopic, "online", true);
      Serial.printf("MQTT publish %s = online retained: %s\n", kStatusTopic, statusPublished ? "ok" : "FAILED");
      const bool subscribed = mqttClient.subscribe(kStayAwakeTopic, 1);
      Serial.printf("MQTT subscribe %s qos=1: %s\n", kStayAwakeTopic, subscribed ? "ok" : "FAILED");
      if (!subscribed) {
        return false;
      }
      waitForRetainedStayAwakeCommand();
      const bool haSubscribed = mqttClient.subscribe(kHaStatusTopic, 0);
      Serial.printf("MQTT subscribe %s qos=0: %s\n", kHaStatusTopic, haSubscribed ? "ok" : "FAILED");
      if (!haSubscribed) {
        return false;
      }
      publishHomeAssistantDiscovery();
      return true;
    }

    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
    delay(kMqttRetryDelayMs);
  }

  Serial.println("MQTT connection failed");
  return false;
}

void publishFloat(const char* suffix, float value) {
  const String fullTopic = topic(suffix);
  if (isnan(value) || isinf(value)) {
    Serial.printf("MQTT skip %s: invalid reading\n", fullTopic.c_str());
    return;
  }

  char payload[24];
  dtostrf(value, 0, 8, payload);
  const bool ok = mqttClient.publish(fullTopic.c_str(), payload, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", fullTopic.c_str(), payload, ok ? "ok" : "FAILED");
}

void publishText(const char* suffix, const char* value, bool retained = true) {
  const String fullTopic = topic(suffix);
  const bool ok = mqttClient.publish(fullTopic.c_str(), value, retained);
  Serial.printf("MQTT publish %s = %s retained=%s: %s\n",
                fullTopic.c_str(),
                value,
                retained ? "true" : "false",
                ok ? "ok" : "FAILED");
}

void publishDiscoveryPayload(const char* component, const char* objectId, const char* payload) {
  char discoveryTopic[160];
  if (!formatInto(discoveryTopic,
                  sizeof(discoveryTopic),
                  "Home Assistant discovery topic",
                  "%s/%s/%s/config",
                  kHaDiscoveryPrefix,
                  component,
                  objectId)) {
    Serial.printf("HA discovery skipped for %s/%s: topic too long\n", component, objectId);
    return;
  }

  const size_t payloadLength = strlen(payload);
  const size_t packetBytes = kMqttMaxHeaderBytes + 2 + strlen(discoveryTopic) + payloadLength;
  Serial.printf("HA discovery payload %s: payload=%u bytes, packet_estimate=%u/%u bytes\n",
                discoveryTopic,
                static_cast<unsigned int>(payloadLength),
                static_cast<unsigned int>(packetBytes),
                static_cast<unsigned int>(kMqttBufferSize));
  if (packetBytes > kMqttBufferSize) {
    Serial.printf("HA discovery skipped %s: MQTT packet estimate exceeds buffer\n", discoveryTopic);
    return;
  }

  const bool ok = mqttClient.publish(discoveryTopic, payload, true);
  Serial.printf("HA discovery publish %s retained: %s\n", discoveryTopic, ok ? "ok" : "FAILED");
}

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

const char* readinessText(bool ready, const char* issue) {
  if (ready) {
    return "ready";
  }
  if (issue && issue[0]) {
    return issue;
  }
  return "unknown";
}

void publishDiagnostics() {
  Serial.println("Publishing diagnostics");
  const DeviceState& devices = deviceState();
  char rssi[16];
  snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI());
  publishText("/diagnostic/wifi_signal", rssi);
  const String wifiSsid = WiFi.SSID();
  publishText("/diagnostic/wifi_ssid", wifiSsid.c_str());
  const String ipAddress = WiFi.localIP().toString();
  publishText("/diagnostic/ip_address", ipAddress.c_str());
  publishText("/diagnostic/reset_reason", resetReasonName(esp_reset_reason()));
  publishText("/diagnostic/battery_chemistry", BATTERY_CHEMISTRY_NAME);

  char sensorReadiness[160];
  if (formatInto(sensorReadiness,
                 sizeof(sensorReadiness),
                 "sensor readiness diagnostic",
                 "solar=%s battery=%s sht4x=%s bmp3xx=%s",
                 readinessText(devices.solarInaReady, devices.solarInaIssue),
                 readinessText(devices.batteryInaReady, devices.batteryInaIssue),
                 readinessText(devices.sht41Ready, devices.sht41Issue),
                 readinessText(devices.bmp390Ready, devices.bmp390Issue))) {
    publishText("/diagnostic/sensor_readiness", sensorReadiness);
  }
}

void appendDegradedIssue(String& status, bool& degraded, const char* issue) {
  if (!issue || !issue[0]) {
    return;
  }
  if (!degraded) {
    status += "; degraded:";
    degraded = true;
  }
  status += ' ';
  status += issue;
}

void publishDeviceStatus() {
  String status = "online";
  bool degraded = false;
  const DeviceState& devices = deviceState();
  appendDegradedIssue(status, degraded, devices.solarInaIssue);
  appendDegradedIssue(status, degraded, devices.batteryInaIssue);
  appendDegradedIssue(status, degraded, devices.sht41Issue);
  appendDegradedIssue(status, degraded, devices.bmp390Issue);
  const bool ok = mqttClient.publish(kStatusTopic, status.c_str(), true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", kStatusTopic, status.c_str(), ok ? "ok" : "FAILED");
}

void publishReadings() {
  logPhase("MQTT publish cycle");
  Reading reading = readSensors();

  publishFloat("/sensor/bmp390_temperature", reading.bmpTemperatureC);
  publishFloat("/sensor/absolute_pressure", reading.absolutePressureHpa);
  publishFloat("/sensor/outside_temperature", reading.outsideTemperatureC);
  publishFloat("/sensor/outside_humidity", reading.outsideHumidityPercent);
  publishFloat("/sensor/battery_voltage", reading.batteryVoltageV);
  publishFloat("/sensor/battery_current", reading.batteryCurrentMa);
  publishFloat("/sensor/battery_power", reading.batteryPowerW);
  publishFloat("/sensor/battery_level", reading.batteryLevelPercent);
  publishFloat("/sensor/solar_raw_voltage", reading.solarRawVoltageV);
  publishFloat("/sensor/solar_panel_current", reading.solarPanelCurrentMa);
  publishFloat("/sensor/solar_raw_power", reading.solarRawPowerW);
  publishDiagnostics();
  publishDeviceStatus();
  flushMqtt(kTelemetryFlushMs);
  markTelemetryPublishCompleted();
}

void flushMqtt(uint32_t durationMs) {
  Serial.printf("Flushing MQTT for %lu ms\n", static_cast<unsigned long>(durationMs));
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    serviceMqttAndOta();
    delay(10);
  }
  Serial.println("MQTT flush complete");
}

bool publishRetainedStatusAndWaitForEcho(const char* payload, uint32_t timeoutMs) {
  statusConfirmationExpected = payload;
  statusConfirmationReceived = false;
  statusConfirmationPending = true;

  const bool statusOk = mqttClient.publish(kStatusTopic, payload, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n",
                kStatusTopic,
                payload,
                statusOk ? "ok" : "FAILED");
  if (!statusOk) {
    statusConfirmationPending = false;
    statusConfirmationExpected = nullptr;
    return false;
  }

  if (!statusConfirmationSubscribed) {
    const bool subscribed = mqttClient.subscribe(kStatusTopic, 0);
    Serial.printf("MQTT subscribe %s qos=0 for status confirmation: %s\n",
                  kStatusTopic,
                  subscribed ? "ok" : "FAILED");
    statusConfirmationSubscribed = subscribed;
    if (!subscribed) {
      statusConfirmationPending = false;
      statusConfirmationExpected = nullptr;
      return false;
    }
  }

  Serial.printf("Waiting up to %lu ms for broker echo of %s = %s\n",
                static_cast<unsigned long>(timeoutMs),
                kStatusTopic,
                payload);
  const uint32_t started = millis();
  while (!statusConfirmationReceived && mqttClient.connected() && millis() - started < timeoutMs) {
    serviceMqttAndOta();
    delay(10);
  }

  const bool confirmed = statusConfirmationReceived;
  statusConfirmationPending = false;
  statusConfirmationExpected = nullptr;
  Serial.printf("MQTT broker echo for %s = %s: %s\n",
                kStatusTopic,
                payload,
                confirmed ? "confirmed" : "not confirmed");
  return confirmed;
}

bool publishSleepStatusWithConfirmation() {
  for (uint8_t attempt = 1; attempt <= kSleepStatusConfirmAttempts; ++attempt) {
    Serial.printf("Publishing sleeping status with broker confirmation, attempt %u/%u\n",
                  attempt,
                  kSleepStatusConfirmAttempts);
    if (publishRetainedStatusAndWaitForEcho("sleeping", kSleepStatusConfirmTimeoutMs)) {
      return true;
    }

    if (attempt < kSleepStatusConfirmAttempts) {
      waitWithMqttAndOta(kMqttRetryDelayMs, "Sleeping status confirmation retry wait");
    }
  }

  Serial.println("WARNING: MQTT broker did not confirm retained sleeping status; sleeping anyway to protect battery");
  return false;
}

void sleepForDefaultInterval(const char* reason) {
  logPhase("Deep sleep");
  while (otaInProgress) {
    Serial.println("OTA update in progress; delaying deep sleep");
    serviceMqttAndOta();
    delay(100);
  }

  Serial.printf("Preparing deep sleep for %llu seconds: %s\n", kDeepSleepSeconds, reason);
  waitForPostTelemetryAwakeWindow();

  if (mqttClient.connected()) {
    const bool sleepStatusConfirmed = publishSleepStatusWithConfirmation();
    if (sleepStatusConfirmed) {
      waitWithMqttAndOta(kPostSleepStatusGraceMs, "Post-sleep-status MQTT grace period");
    }
    mqttClient.disconnect();
    Serial.println("MQTT disconnected");
  } else {
    Serial.println("MQTT not connected; skipping sleeping status publish");
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disconnected and radio turned off");
  esp_sleep_enable_timer_wakeup(kDeepSleepSeconds * 1000000ULL);
  Serial.println("Deep-sleep timer configured");
  Serial.printf("Entering deep sleep for %llu seconds now\n", kDeepSleepSeconds);
  Serial.flush();
  esp_deep_sleep_start();
}

void waitForRetainedStayAwakeCommand() {
  logPhase("Stay-awake command");
  Serial.println("MQTT commands cannot wake the ESP32 while it is already in deep sleep.");
  Serial.println("A retained stay-awake command will be applied after the next timer wake and MQTT reconnect.");

  if (kForceStayAwakeForTesting) {
    stayAwakeRequested = true;
    stayAwakeCommandReceived = true;
    Serial.println("Local force stay-awake flag is enabled; bypassing retained MQTT command wait");
    return;
  }

  Serial.printf("Waiting up to %lu ms for retained command on %s\n",
                static_cast<unsigned long>(kRetainedCommandWaitMs),
                kStayAwakeTopic);
  const uint32_t started = millis();
  while (!stayAwakeCommandReceived && millis() - started < kRetainedCommandWaitMs) {
    ArduinoOTA.handle();
    mqttClient.loop();
    delay(10);
  }

  if (!stayAwakeCommandReceived) {
    Serial.println("No retained stay-awake command received before timeout; defaulting to one publish then sleep");
  } else {
    Serial.printf("Retained stay-awake command applied: %s\n", stayAwakeRequested ? "true" : "false");
    Serial.printf("Stay-awake retained value means this boot will %s\n",
                  stayAwakeRequested ? "remain awake" : "return to deep sleep after publishing");
  }

  if (homeAssistantDiscoveryRequested) {
    publishHomeAssistantDiscovery();
  }
}

void appSetup() {
  Serial.begin(kSerialBaud);
  delay(200);
  setCpuFrequencyMhz(kCpuFrequencyMhz);

  Serial.println();
  Serial.printf("%s boot\n", kFirmwareName);
  Serial.printf("CPU frequency requested: %lu MHz, active: %u MHz\n",
                static_cast<unsigned long>(kCpuFrequencyMhz),
                getCpuFrequencyMhz());
  Serial.printf("Reset reason: %s\n", resetReasonName(esp_reset_reason()));
  Serial.printf("MQTT topic prefix: %s\n", kTopicPrefix);
  Serial.printf("Battery chemistry: %s (%s)\n", BATTERY_CHEMISTRY_NAME, BATTERY_CHEMISTRY_KEY);
  Serial.printf("Stay-awake publish interval: %lu ms\n", static_cast<unsigned long>(kStayAwakePublishIntervalMs));
  Serial.printf("Default deep sleep: %llu seconds\n", kDeepSleepSeconds);
  Serial.printf("Local force stay-awake flag: %s\n", kForceStayAwakeForTesting ? "enabled" : "disabled");
  Serial.println("Safety: I2C pullups must be tied to 3.3 V only.");
  Serial.println("Safety: charger chemistry, charge voltage, charge current, and protection must match the installed cell.");
  Serial.println("Safety: Li-ion charger output above about 4.25 V is unsafe/untrusted.");
  Serial.println("Safety: do not power the ESP32 3.3 V rail directly from LiFePO4; use a low-Iq regulator or buck-boost.");
  Serial.println("Safety: solar panel rating, TP5000 variant, charge current, and ESP32 regulator behavior are not assumed.");

  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Serial.printf("Waiting %lu ms for I2C peripherals to stabilize after wake\n",
                static_cast<unsigned long>(kI2cPowerStabilizeDelayMs));
  delay(kI2cPowerStabilizeDelayMs);
  scanI2cBus();
  initializeSensors();

  if (!connectWifi()) {
    sleepForDefaultInterval("WiFi unavailable");
  }

  initializeOta();

  if (!connectMqtt()) {
    sleepForDefaultInterval("MQTT unavailable");
  }

  Serial.printf("Waiting %lu ms before first sensor read after boot/wake\n",
                static_cast<unsigned long>(kBeforeFirstReadDelayMs));
  delay(kBeforeFirstReadDelayMs);
  publishReadings();
  lastPublishMs = millis();

  if (!stayAwakeRequested) {
    Serial.println("Stay-awake mode is disabled; device will sleep after first publish");
    sleepForDefaultInterval("stay_awake disabled or absent");
  }

  Serial.printf("Stay-awake mode enabled; publishing every %lu ms\n",
                static_cast<unsigned long>(kStayAwakePublishIntervalMs));
}

void appLoop() {
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) {
    Serial.printf("Connection lost while awake: wifi_status=%d mqtt_connected=%s\n",
                  WiFi.status(),
                  yesNo(mqttClient.connected()));
    sleepForDefaultInterval("connection lost");
  }

  mqttClient.loop();
  ArduinoOTA.handle();

  if (homeAssistantDiscoveryRequested) {
    publishHomeAssistantDiscovery();
  }

  if (!stayAwakeRequested) {
    Serial.println("Stay-awake changed to false while awake; publishing once before sleep");
    publishReadings();
    sleepForDefaultInterval("stay_awake disabled");
  }

  if (millis() - lastPublishMs >= kStayAwakePublishIntervalMs) {
    Serial.printf("Stay-awake publish interval reached after %lu ms\n",
                  static_cast<unsigned long>(millis() - lastPublishMs));
    publishReadings();
    lastPublishMs = millis();
  }

  delay(20);
}

}  // namespace

void setup() {
  appSetup();
}

void loop() {
  appLoop();
}
