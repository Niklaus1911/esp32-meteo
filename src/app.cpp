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
  Serial.printf("%s%s boot%s\n", serialStyle(SerialStyle::Phase), kFirmwareName, serialReset());
  Serial.printf("CPU frequency requested: %lu MHz, active: %u MHz\n",
                static_cast<unsigned long>(kCpuFrequencyMhz),
                getCpuFrequencyMhz());
  Serial.printf("Reset reason: %s%s%s\n",
                serialStyle(resetReason == ESP_RST_BROWNOUT || resetReason == ESP_RST_PANIC ? SerialStyle::Error
                                                                                             : SerialStyle::Value),
                resetReasonName(resetReason),
                serialReset());
  Serial.printf("Wakeup cause: %s%s%s\n",
                serialStyle(SerialStyle::Value),
                wakeupCauseName(wakeupCause),
                serialReset());
  Serial.printf("RTC counters: boots=%lu sleep_entries=%lu\n",
                static_cast<unsigned long>(rtcBootCount),
                static_cast<unsigned long>(rtcSleepEntryCount));
  Serial.printf("RTC counter reset policy: %s%s%s\n",
                serialStyle(SerialStyle::Value),
                clearRtcCounters ? "cleared after power-on" : "preserved",
                serialReset());
  Serial.printf("MQTT topic prefix: %s%s%s\n", serialStyle(SerialStyle::Topic), kTopicPrefix, serialReset());
  Serial.printf("Stay-awake publish interval: %lu ms\n", static_cast<unsigned long>(kStayAwakePublishIntervalMs));
  Serial.printf("Default deep sleep: %llu seconds\n", kDeepSleepSeconds);
  Serial.printf("Serial ANSI colors: %s\n", serialEnabledDisabled(kSerialAnsiColors));
  Serial.printf("Local force stay-awake flag: %s\n", serialEnabledDisabled(kForceStayAwakeForTesting));
  Serial.printf("%sSafety%s: I2C pullups must be tied to 3.3 V only.\n", serialStyle(SerialStyle::Warning), serialReset());
  Serial.printf("%sSafety%s: charger chemistry, charge voltage, charge current, and protection must match the installed cell.\n",
                serialStyle(SerialStyle::Warning),
                serialReset());
  Serial.printf("%sSafety%s: Li-ion charger output above about 4.25 V is unsafe/untrusted.\n",
                serialStyle(SerialStyle::Warning),
                serialReset());
  Serial.printf("%sSafety%s: do not power the ESP32 3.3 V rail directly from LiFePO4; use a low-Iq regulator or buck-boost.\n",
                serialStyle(SerialStyle::Warning),
                serialReset());
  Serial.printf("%sSafety%s: solar panel rating, TP5000 variant, charge current, and ESP32 regulator behavior are not assumed.\n",
                serialStyle(SerialStyle::Warning),
                serialReset());

  const bool deepSleepWake = isDeepSleepWake(resetReason, wakeupCause);
  if (deepSleepWake) {
    recoverI2cBusAfterDeepSleep();
  }

  if (!ensureRuntimeProvisioning()) {
    sleepForDefaultInterval("runtime provisioning incomplete");
  }

  Serial.printf("Battery chemistry: %s%s%s (%s)\n",
                serialStyle(SerialStyle::Value),
                batteryChemistryName(runtimeConfig().batteryChemistryId),
                serialReset(),
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

  if (!initialTelemetryOk) {
    Serial.printf("%sInitial telemetry did not complete cleanly after retry%s\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
  }

  if (!stayAwakeRequested) {
    Serial.printf("Stay-awake mode is %s; device will sleep after first publish\n",
                  serialEnabledDisabled(false));
    sleepForDefaultInterval("stay_awake disabled or absent");
  }

  Serial.printf("Stay-awake mode %s; publishing every %s%lu ms%s\n",
                serialEnabledDisabled(true),
                serialStyle(SerialStyle::Value),
                static_cast<unsigned long>(kStayAwakePublishIntervalMs),
                serialReset());
}

void appLoop() {
  serviceLocalButton();

  if (WiFi.status() != WL_CONNECTED || !mqttClient().connected()) {
    Serial.printf("%sConnection lost while awake%s: wifi_status=%d mqtt_connected=%s\n",
                  serialStyle(SerialStyle::Error),
                  serialReset(),
                  WiFi.status(),
                  serialYesNo(mqttClient().connected()));
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
