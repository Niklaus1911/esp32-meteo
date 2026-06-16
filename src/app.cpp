#include "app.h"

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
#include "sleep.h"
#include "telemetry.h"
#include "util.h"
#include "wifi_connect.h"

namespace Esp32Meteo {

namespace {

bool publishReadingsWithRetry(const char* reason) {
  Serial.printf("Publishing readings: %s\n", reason);
  if (publishReadings()) {
    return true;
  }

  Serial.printf("Telemetry publish failed; retrying once after %lu ms\n",
                static_cast<unsigned long>(kTelemetryPublishRetryDelayMs));
  waitWithMqttAndOta(kTelemetryPublishRetryDelayMs, "Telemetry publish retry wait");
  const bool retryOk = publishReadings();
  Serial.printf("Telemetry publish retry result: %s\n", retryOk ? "complete" : "FAILED");
  return retryOk;
}

}  // namespace

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
  publishReadingsWithRetry("initial boot/wake publish");
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

  mqttClient().loop();
  handleOta();

  if (homeAssistantDiscoveryRequested) {
    publishHomeAssistantDiscovery();
  }

  if (!stayAwakeRequested) {
    Serial.println("Stay-awake changed to false while awake; publishing once before sleep");
    publishReadingsWithRetry("stay_awake disabled before sleep");
    sleepForDefaultInterval("stay_awake disabled");
  }

  if (millis() - lastPublishMs >= kStayAwakePublishIntervalMs) {
    Serial.printf("Stay-awake publish interval reached after %lu ms\n",
                  static_cast<unsigned long>(millis() - lastPublishMs));
    publishReadingsWithRetry("stay-awake interval");
    lastPublishMs = millis();
  }

  delay(20);
}

}  // namespace Esp32Meteo
