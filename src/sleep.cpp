#include "sleep.h"

#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
#include "mqtt_client.h"
#include "runtime_state.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

void releaseI2cPinsBeforeSleep() {
  Wire.end();
  pinMode(kI2cSdaPin, INPUT);
  pinMode(kI2cSclPin, INPUT);
  Serial.printf("I2C stopped and pins released before deep sleep: SDA GPIO%u, SCL GPIO%u\n",
                static_cast<unsigned int>(kI2cSdaPin),
                static_cast<unsigned int>(kI2cSclPin));
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

}  // namespace

void sleepForDefaultInterval(const char* reason) {
  logPhase("Deep sleep");
  while (otaInProgress) {
    Serial.println("OTA update in progress; delaying deep sleep");
    serviceMqttAndOta();
    delay(100);
  }

  publishBootPhase("sleep_prepare");
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
  releaseI2cPinsBeforeSleep();
  ++rtcSleepEntryCount;
  Serial.printf("RTC sleep entry counter: %lu\n", static_cast<unsigned long>(rtcSleepEntryCount));
  esp_sleep_enable_timer_wakeup(kDeepSleepSeconds * 1000000ULL);
  Serial.println("Deep-sleep timer configured");
  Serial.printf("Entering deep sleep for %llu seconds now\n", kDeepSleepSeconds);
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace Esp32Meteo
