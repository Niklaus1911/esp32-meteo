#include "app.h"

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
#include "firmware_logic.h"
#include "ha_discovery.h"
#include "local_button.h"
#include "mqtt_client.h"
#include "ota_service.h"
#include "provisioning.h"
#include "runtime_config.h"
#include "runtime_state.h"
#include "sensors.h"
#include "sleep.h"
#include "telemetry.h"
#include "util.h"
#include "wifi_connect.h"

namespace Esp32Meteo {

namespace {

bool isDeepSleepWake(esp_reset_reason_t resetReason, esp_sleep_wakeup_cause_t wakeupCause) {
  return resetReason == ESP_RST_DEEPSLEEP || wakeupCause == ESP_SLEEP_WAKEUP_TIMER;
}

bool shouldClearRtcCounters(esp_reset_reason_t resetReason) {
  return resetReason == ESP_RST_POWERON;
}

void waitWithLocalButton(uint32_t durationMs) {
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    serviceLocalButton();
    delay(10);
  }
}

void releaseI2cPinsForRecovery() {
  pinMode(kI2cSdaPin, INPUT_PULLUP);
  pinMode(kI2cSclPin, INPUT_PULLUP);
  delay(2);
}

void recoverI2cBusAfterDeepSleep() {
  logPhase("Deep-sleep wake stabilization");
  Serial.printf("Waiting %lu ms before I2C/WiFi startup after deep-sleep wake\n",
                static_cast<unsigned long>(kDeepSleepWakeStabilizeMs));
  waitWithLocalButton(kDeepSleepWakeStabilizeMs);

  releaseI2cPinsForRecovery();
  Serial.printf("I2C recovery initial levels: SDA=%d SCL=%d\n",
                digitalRead(kI2cSdaPin),
                digitalRead(kI2cSclPin));

  if (digitalRead(kI2cSdaPin) == LOW) {
    Serial.printf("I2C SDA is held low; pulsing SCL %u times before Wire.begin()\n",
                  static_cast<unsigned int>(kI2cRecoveryClockPulses));
    for (uint8_t pulse = 0; pulse < kI2cRecoveryClockPulses; ++pulse) {
      pinMode(kI2cSclPin, OUTPUT);
      digitalWrite(kI2cSclPin, LOW);
      delayMicroseconds(kI2cRecoveryPulseDelayUs);
      pinMode(kI2cSclPin, INPUT_PULLUP);
      delayMicroseconds(kI2cRecoveryPulseDelayUs);
    }

    pinMode(kI2cSdaPin, OUTPUT);
    digitalWrite(kI2cSdaPin, LOW);
    delayMicroseconds(kI2cRecoveryPulseDelayUs);
    pinMode(kI2cSclPin, INPUT_PULLUP);
    delayMicroseconds(kI2cRecoveryPulseDelayUs);
    pinMode(kI2cSdaPin, INPUT_PULLUP);
    delayMicroseconds(kI2cRecoveryPulseDelayUs);
  }

  Serial.printf("I2C recovery final levels: SDA=%d SCL=%d\n",
                digitalRead(kI2cSdaPin),
                digitalRead(kI2cSclPin));
}

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
  const esp_reset_reason_t resetReason = esp_reset_reason();
  const esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
  const bool clearRtcCounters = shouldClearRtcCounters(resetReason);
  if (clearRtcCounters) {
    rtcBootCount = 0;
    rtcSleepEntryCount = 0;
  }
  ++rtcBootCount;

  setCpuFrequencyMhz(kCpuFrequencyMhz);
  initializeLocalButton();
  waitWithLocalButton(200);

  Serial.println();
  Serial.printf("%s boot\n", kFirmwareName);
  Serial.printf("CPU frequency requested: %lu MHz, active: %u MHz\n",
                static_cast<unsigned long>(kCpuFrequencyMhz),
                getCpuFrequencyMhz());
  Serial.printf("Reset reason: %s\n", resetReasonName(resetReason));
  Serial.printf("Wakeup cause: %s\n", wakeupCauseName(wakeupCause));
  Serial.printf("RTC counters: boots=%lu sleep_entries=%lu\n",
                static_cast<unsigned long>(rtcBootCount),
                static_cast<unsigned long>(rtcSleepEntryCount));
  Serial.printf("RTC counter reset policy: %s\n", clearRtcCounters ? "cleared after power-on" : "preserved");
  Serial.printf("MQTT topic prefix: %s\n", kTopicPrefix);
  Serial.printf("Stay-awake publish interval: %lu ms\n", static_cast<unsigned long>(kStayAwakePublishIntervalMs));
  Serial.printf("Default deep sleep: %llu seconds\n", kDeepSleepSeconds);
  Serial.printf("Local force stay-awake flag: %s\n", kForceStayAwakeForTesting ? "enabled" : "disabled");
  Serial.println("Safety: I2C pullups must be tied to 3.3 V only.");
  Serial.println("Safety: charger chemistry, charge voltage, charge current, and protection must match the installed cell.");
  Serial.println("Safety: Li-ion charger output above about 4.25 V is unsafe/untrusted.");
  Serial.println("Safety: do not power the ESP32 3.3 V rail directly from LiFePO4; use a low-Iq regulator or buck-boost.");
  Serial.println("Safety: solar panel rating, TP5000 variant, charge current, and ESP32 regulator behavior are not assumed.");

  const bool deepSleepWake = isDeepSleepWake(resetReason, wakeupCause);
  if (deepSleepWake) {
    recoverI2cBusAfterDeepSleep();
  }

  if (!ensureRuntimeProvisioning()) {
    sleepForDefaultInterval("runtime provisioning incomplete");
  }

  Serial.printf("Battery chemistry: %s (%s)\n",
                batteryChemistryName(runtimeConfig().batteryChemistryId),
                batteryChemistryKey(runtimeConfig().batteryChemistryId));

  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Serial.printf("Waiting %lu ms for I2C peripherals to stabilize after wake\n",
                static_cast<unsigned long>(kI2cPowerStabilizeDelayMs));
  waitWithLocalButton(kI2cPowerStabilizeDelayMs);
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
  waitWithLocalButton(kBeforeFirstReadDelayMs);
  const bool initialTelemetryOk = publishReadingsWithRetry("initial boot/wake publish");
  lastPublishMs = millis();

  if (initialTelemetryOk && shouldPublishRoutineDiscovery(deepSleepWake, stayAwakeRequested)) {
    configureMqttResetControls();
    publishHomeAssistantDiscovery();
  } else if (initialTelemetryOk) {
    Serial.println("Skipping routine Home Assistant discovery because this wake is sleep-bound");
    publishBootPhase("ha_discovery_skipped_sleep_bound");
  } else {
    Serial.println("Skipping post-telemetry MQTT setup because telemetry did not complete cleanly");
    publishBootPhase("post_telemetry_setup_skipped");
  }

  if (!stayAwakeRequested) {
    Serial.println("Stay-awake mode is disabled; device will sleep after first publish");
    sleepForDefaultInterval("stay_awake disabled or absent");
  }

  Serial.printf("Stay-awake mode enabled; publishing every %lu ms\n",
                static_cast<unsigned long>(kStayAwakePublishIntervalMs));
}

void appLoop() {
  serviceLocalButton();

  if (WiFi.status() != WL_CONNECTED || !mqttClient().connected()) {
    Serial.printf("Connection lost while awake: wifi_status=%d mqtt_connected=%s\n",
                  WiFi.status(),
                  yesNo(mqttClient().connected()));
    sleepForDefaultInterval("connection lost");
  }

  serviceMqttAndOta();

  if (!stayAwakeRequested) {
    Serial.println("Stay-awake changed to false while awake; publishing once before sleep");
    publishReadingsWithRetry("stay_awake disabled before sleep");
    sleepForDefaultInterval("stay_awake disabled");
  }

  if (homeAssistantDiscoveryRequested) {
    configureMqttResetControls();
    publishHomeAssistantDiscovery();
  }

  if (millis() - lastPublishMs >= kStayAwakePublishIntervalMs) {
    Serial.printf("Stay-awake publish interval reached after %lu ms\n",
                  static_cast<unsigned long>(millis() - lastPublishMs));
    publishReadingsWithRetry("stay-awake interval");
    lastPublishMs = millis();
  }

  waitWithLocalButton(20);
}

}  // namespace Esp32Meteo
