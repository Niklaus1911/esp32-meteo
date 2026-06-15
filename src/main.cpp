#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
#include "ha_discovery.h"
#include "mqtt_client.h"
#include "ota_service.h"
#include "runtime_state.h"
#include "sensors.h"
#include "util.h"
#include "wifi_connect.h"

namespace {

using namespace Esp32Meteo;

void markTelemetryPublishCompleted() {
  telemetryPublishCompleted = true;
  telemetryPublishCompletedMs = millis();
  Serial.printf("Telemetry publish completed at %lu ms; post-telemetry awake window starts now\n",
                static_cast<unsigned long>(telemetryPublishCompletedMs));
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
  const bool ok = mqttClient().publish(kStatusTopic, status.c_str(), true);
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

void sleepForDefaultInterval(const char* reason) {
  logPhase("Deep sleep");
  while (otaInProgress) {
    Serial.println("OTA update in progress; delaying deep sleep");
    serviceMqttAndOta();
    delay(100);
  }

  Serial.printf("Preparing deep sleep for %llu seconds: %s\n", kDeepSleepSeconds, reason);
  waitForPostTelemetryAwakeWindow();

  if (mqttClient().connected()) {
    const bool sleepStatusConfirmed = publishSleepStatusWithConfirmation();
    if (sleepStatusConfirmed) {
      waitWithMqttAndOta(kPostSleepStatusGraceMs, "Post-sleep-status MQTT grace period");
    }
    mqttClient().disconnect();
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
  if (WiFi.status() != WL_CONNECTED || !mqttClient().connected()) {
    Serial.printf("Connection lost while awake: wifi_status=%d mqtt_connected=%s\n",
                  WiFi.status(),
                  yesNo(mqttClient().connected()));
    sleepForDefaultInterval("connection lost");
  }

  serviceMqttAndOta();

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
